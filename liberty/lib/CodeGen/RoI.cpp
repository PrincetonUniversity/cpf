#define DEBUG_TYPE "roi"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"


#include "liberty/CodeGen/RoI.h"

namespace liberty
{
namespace SpecPriv
{

STATISTIC(numSideCloned,  "RoI functions cloned to resolve side-entrances");

void RoI::clear()
{
  bbs.clear();
  fcns.clear();
}

void RoI::print(raw_ostream &fout) const
{
  fout << "RoI contains these blocks:\n";
  for(BBSet::const_iterator i=bbs.begin(), e=bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    Function *fcn = bb->getParent();
    fout << " - roi: " << fcn->getName() << " :: " << bb->getName() << '\n';
  }
  fout << '\n';
}

void RoI::dump() const
{
  print( errs() );
}

void RoI::sweep(BasicBlock *bb)
{
  if( bbs.count(bb) )
    return;
  bbs.insert(bb);

  for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
  {
    CallSite cs = getCallSite(&*i);
    if( !cs.getInstruction() )
      continue;

    Function *callee = cs.getCalledFunction();
    if( !callee )
      continue;

    if( callee->isDeclaration() )
      continue;

    sweep(callee->begin(), callee->end());
    fcns.insert(callee);
  }
}

bool RoI::resolveSideEntrances(UpdateOnClone &changes, FoldManager &fmgr, Selector &selector)
{
  bool modified = false;

  F2F  cloned;
  F2VM vmaps;
  FSet roots_in_roi;

  // fix if the function for the "root" loop is included in RoI, because if that's the case
  // following transformations will make the "root" loop unreachable
  modified = cloneRootsIfNecessary(changes, fmgr, cloned, vmaps, roots_in_roi);

  for(;;)
    if( resolveOneSideEntrance(changes, fmgr, cloned, vmaps) )
      modified = true;
    else
      break;

  if (modified)
  {
    // swap uses of rootfcns with its clones, then adjust RoI accordingly
    swapRootFcnUses(changes, fmgr, cloned, vmaps, roots_in_roi);

    // create functions that maps original functions to clone functions
    createO2CFunctions(cloned, selector);
  }

  for(F2VM::iterator i=vmaps.begin(), e=vmaps.end(); i!=e; ++i)
    delete i->second;

  return modified;
}

bool RoI::isReachableFromOutsideOfRoI(Function* fcn)
{
  if ( reachability_cache.count(fcn) )
    return reachability_cache[fcn];

  std::set<Instruction*>  visited;
  std::list<Instruction*> fringe;
  for(Value::user_iterator j=fcn->user_begin(), z=fcn->user_end(); j!=z; ++j)
    if ( Instruction* inst = dyn_cast< Instruction >(&**j) )
      fringe.push_back(inst);

  while ( !fringe.empty() )
  {
    Instruction *inst = fringe.front();
    fringe.pop_front();

    if ( visited.count(inst) )
      continue;

    visited.insert(inst);
    Function* caller = inst->getParent()->getParent();
    if ( !fcns.count(caller) )
    {
      reachability_cache[fcn] = true;
      return true;
    }

    for(Value::user_iterator j=caller->user_begin(), z=caller->user_end(); j!=z; ++j)
      if ( Instruction* inst = dyn_cast< Instruction >(&**j) )
        fringe.push_back(inst);
  }

  reachability_cache[fcn] = false;
  return false;
}

bool RoI::isCloneOrCloned(Function *fcn, F2F &cloned)
{
  for (F2F::iterator fi = cloned.begin() ; fi != cloned.end() ; fi++)
    if ( fi->first == fcn || fi->second == fcn )
      return true;
  return false;
}

Function* RoI::getOriginal(Function *fcn, F2F &cloned)
{
  for (F2F::iterator fi = cloned.begin() ; fi != cloned.end() ; fi++)
    if ( fi->second == fcn )
      return const_cast<Function*>(fi->first);
  return NULL;
}

bool RoI::cloneRootsIfNecessary(UpdateOnClone &changes, FoldManager &fmgr, F2F &cloned, F2VM &vmaps,
    FSet& roots_in_roi)
{
  bool modified = false;

  // find functions for root blocks
  FSet rootfcns;
  for (BBSet::iterator i=roots.begin(), e=roots.end(); i!=e; ++i)
    rootfcns.insert( (*i)->getParent() );

  // for each rootfcn, check if included in RoI
  for (FSet::iterator i=rootfcns.begin(), e=rootfcns.end(); i!=e; ++i)
  {
    Function* fcn = *i;
    if ( fcns.find(fcn) == fcns.end() )
      continue;

    // fcn should be cloned

    roots_in_roi.insert(fcn);
    DEBUG(
        errs() << "Function " << fcn->getName() << ", which is a parent of a root block"
        << ", is included in RoI\n";
    );

    assert( isReachableFromOutsideOfRoI( fcn ) );
    assert( !cloned.count( fcn ) );

    vmaps[fcn] = new ValueToValueMapTy;
    ValueToValueMapTy *vmap = vmaps[fcn];
    Function* clone = CloneFunction(fcn, *vmap);
    //Function* clone = CloneFunction(fcn, *vmap, false);
    //Module *mod = fcn->getParent();

    fcn->setLinkage( GlobalValue::InternalLinkage );
    clone->setName( fcn->getName() + ".within.parallel.region" );

    // sot: CloneFunction in LLVM 5.0 inserts the cloned function in the function's module
    //mod->getFunctionList().push_back(clone);
    (*vmap)[fcn] = clone;

    cloned[fcn] = clone;
    modified = true;

    assert( clone && vmap );

    // mark clones of roots as RoI
    for (BBSet::iterator ri=roots.begin(), re=roots.end(); ri!=re; ++ri)
      if ( vmap->find(*ri) != vmap->end() )
        bbs.insert( cast<BasicBlock>( (*vmap)[*ri] ) );

    // go over users of fcn
    // - if a user is outside of RoI, do nothing (following transformation will handle it)
    // - if a user is inside of RoI,
    //   - if a user is reachable from outside of RoI, clone the parent(parent(user))
    //   - if not, do nothing

    std::vector<User*> uses;
    for(Value::user_iterator j=fcn->user_begin(), z=fcn->user_end(); j!=z; ++j)
      uses.push_back(&**j);

    for(unsigned j = 0 ; j < uses.size() ; j++)
    {
      User *use = uses[j];

      bool isInsideRoI = false;
      Instruction *inst = dyn_cast< Instruction >( use );
      if( inst )
        isInsideRoI = bbs.count( inst->getParent() );

      if ( isInsideRoI )
      {
        Function* callfcn = inst->getParent()->getParent();

        if ( isReachableFromOutsideOfRoI( callfcn ) && !isCloneOrCloned( callfcn, cloned) )
        {
          // clone the caller
          DEBUG(
            errs() << "  . Function " << callfcn->getName() << ", which is a caller of a rootfcn "
            << ", is included in RoI and reachable from outside\n";
          );

          vmaps[callfcn] = new ValueToValueMapTy;
          ValueToValueMapTy &vmap_callfcn = *vmaps[callfcn];
          //Function *clone_callfcn = CloneFunction(callfcn, vmap_callfcn, false);
          Function *clone_callfcn = CloneFunction(callfcn, vmap_callfcn);
          //Module *mod = callfcn->getParent();

          clone_callfcn->takeName(callfcn);
          callfcn->setName( clone_callfcn->getName() + ".within.parallel.region" );
          callfcn->setLinkage( GlobalValue::InternalLinkage );

          // sot: CloneFunction in LLVM 5.0 inserts the cloned function in the function's module
          //mod->getFunctionList().push_back(clone_callfcn);
          vmap_callfcn[callfcn] = clone_callfcn;

          cloned[callfcn] = clone_callfcn;
        }
      }
    }
  }

  return modified;
}

bool RoI::resolveOneSideEntrance(UpdateOnClone &changes, FoldManager &fmgr, F2F &cloned, F2VM &vmaps)
{
  // Foreach function in the RoI
  for(FSet::iterator i=fcns.begin(), e=fcns.end(); i!=e; ++i)
  {
    Function *fcn = *i;
    if( fcn->isDeclaration() )
      continue;

    std::vector<User*> uses;
    //for(Value::user_iterator j=fcn->user_begin(), z=fcn->user_end(); j!=z; ++j)
    for(Value::user_iterator j=fcn->user_begin(), z=fcn->user_end(); j!=z; ++j)
      uses.push_back(&**j);

    // What are the uses of this function?
    for(unsigned j = 0 ; j < uses.size() ; j++)
    {
      User *use = uses[j];

      // Does it have a use from OUTSIDE of the RoI?
      bool isInsideRoI = true;
      Instruction *inst = dyn_cast< Instruction >( use );
      if( inst )
        isInsideRoI = bbs.count( inst->getParent() );

      if( isInsideRoI )
        continue;

      BasicBlock *callbb = inst->getParent();
      Function *callfcn = callbb->getParent();

      DEBUG(
        errs() << "Function " << fcn->getName() << " is referenced by " << *use
               << ", outside of RoI at " << callfcn->getName() << " :: " << callbb->getName() << '\n';
      );

      // We found a use OUTSIDE of the RoI.
      // Replace this with a clone of that function
      if( !cloned.count( fcn ) )
      {
        vmaps[fcn] = new ValueToValueMapTy;
        ValueToValueMapTy &vmap = *vmaps[fcn];
        //Function *clone = CloneFunction(fcn, vmap, false);
        Function *clone = CloneFunction(fcn, vmap);
        //Module *mod = fcn->getParent();

        clone->takeName(fcn);
        fcn->setName( clone->getName() + ".within.parallel.region" );
        fcn->setLinkage( GlobalValue::InternalLinkage );

        // sot: CloneFunction in LLVM 5.0 inserts the cloned function in the function's module
        //mod->getFunctionList().push_back(clone);
        vmap[fcn] = clone;

        DEBUG(errs() << "  o Cloned function to eliminate side entrance\n");

        cloned[fcn] = clone;
        ++numSideCloned;
      }

      DEBUG(errs() << "  o Changing " << *use << '\n');
      Function *clone = cloned[fcn];
      use->replaceUsesOfWith(fcn, clone);
      DEBUG(errs() << "  o       to " << *use << '\n');


      // Update the analyses.

      // Find every context in which 'fcn' is immediately called by 'callfcn'
      typedef std::vector<const Ctx *> Ctxs;
      Ctxs affectedContexts;
      for(FoldManager::ctx_iterator k=fmgr.ctx_begin(), z=fmgr.ctx_end(); k!=z; ++k)
      {
        const Ctx *ctx = &*k;
        if( ctx->type != Ctx_Fcn )
          continue;
        if( ctx->fcn != fcn )
          continue;

//        DEBUG(errs() << "  meh? " << *ctx << '\n');

        const Ctx *parent = ctx->parent;
        if( !parent )
          continue;
        if( parent->getFcn() != callfcn )
          continue;

        affectedContexts.push_back( ctx );
      }

      // rename these contexts
      DEBUG(errs() << "  . - Updating intermediate analyses...\n");
      const ValueToValueMapTy &vmap = *vmaps[ fcn ];
      for(Ctxs::const_iterator k=affectedContexts.begin(), z=affectedContexts.end(); k!=z; ++k)
      {
        const Ctx *ctx = *k;
        DEBUG(errs() << "  . . - ctx: " << *ctx << '\n');

        CtxToCtxMap cmap;
        AuToAuMap amap;
        fmgr.cloneContext(ctx, vmap, cmap, amap);
        changes.contextRenamedViaClone(ctx, vmap, cmap, amap);
      }
      DEBUG(errs() << "  . - done.\n");

      return true;
    }
  }
  return false;
}

void RoI::swapRootFcnUses(UpdateOnClone &changes, FoldManager &fmgr, F2F &cloned, F2VM &vmaps,
    FSet &roots_in_roi)
{
  // for each rootfcn, check if included in RoI
  for (FSet::iterator i=roots_in_roi.begin(), e=roots_in_roi.end(); i!=e; ++i)
  {
    Function* fcn = *i;

    // should have been cloned

    assert( cloned.count( fcn ) );
    Function* clone = cloned[fcn];

    // swap uses

    std::vector<User*> fcn_uses;
    //for(Value::user_iterator j=fcn->user_begin(), z=fcn->user_end(); j!=z; ++j)
    for(Value::user_iterator j=fcn->user_begin(), z=fcn->user_end(); j!=z; ++j)
      fcn_uses.push_back(&**j);

    std::vector<User*> clone_uses;
    //for(Value::user_iterator j=clone->user_begin(), z=clone->user_end(); j!=z; ++j)
    for(Value::user_iterator j=clone->user_begin(), z=clone->user_end(); j!=z; ++j)
      clone_uses.push_back(&**j);

    for (unsigned j = 0 ; j < fcn_uses.size() ; j++)
    {
      User        *use = fcn_uses[j];
      Instruction *use_inst = dyn_cast<Instruction>(use);

      if ( use_inst && roots_in_roi.count( use_inst->getParent()->getParent() ) )
        continue;

      DEBUG(errs() << "  o Changing " << *use << '\n');
      use->replaceUsesOfWith(fcn, clone);
      DEBUG(errs() << "  o       to " << *use << '\n');
    }

    for (unsigned j = 0 ; j < clone_uses.size() ; j++)
    {
      User        *use = clone_uses[j];
      Instruction *use_inst = dyn_cast<Instruction>(use);
      Function    *org = getOriginal(use_inst->getParent()->getParent(), cloned);

      if ( use_inst && roots_in_roi.count( org ) )
        continue;

      DEBUG(errs() << "  o Changing " << *use << '\n');
      use->replaceUsesOfWith(clone, fcn);
      DEBUG(errs() << "  o       to " << *use << '\n');
    }

    // replace callee of call instructions within blocks that was within parallel region before but
    // now in sequential region

    for (inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
    {
      CallSite cs = getCallSite(&*ii);
      if( !cs.getInstruction() )
        continue;

      // cs still in parallel region

      if ( roots.count( cs.getInstruction()->getParent() ) )
        continue;

      Function *callee = cs.getCalledFunction();
      if( !callee )
        continue;

      // if callee is within RoI, its entry block will eventually be outside of parallel region

      if ( roots_in_roi.count(callee) )
        continue;

      if ( isCloneOrCloned(callee, cloned) )
      {
        Function* newcallee = NULL;

        // as callsite was originally within the parallel region, original callee should have
        // existed within the parallel region too.

        for (F2F::iterator fi = cloned.begin() ; fi != cloned.end() ; fi++)
          if ( fi->first == callee )
            newcallee = const_cast<Function*>(fi->second);

        assert( newcallee );
        cs.setCalledFunction(newcallee);
      }
    }

    // For call instructions within 'clone', the instruction should call the version within the
    // parallel region, as 'clone' itself is within the parallel region now

    for (inst_iterator ii = inst_begin(clone) ; ii != inst_end(clone) ; ii++)
    {
      CallSite cs = getCallSite(&*ii);
      if( !cs.getInstruction() )
        continue;

      Function *callee = cs.getCalledFunction();
      if( !callee )
        continue;

      Function* newcallee = NULL;

      for (F2F::iterator fi = cloned.begin() ; fi != cloned.end() ; fi++)
        if ( fi->second == callee )
          newcallee = const_cast<Function*>(fi->first);

      // if newcallee is within roots_in_roi, it will eventually be a sequential one

      if ( newcallee && !roots_in_roi.count(newcallee) )
        cs.setCalledFunction(newcallee);
    }

    // erase fcn from RoI, but add clone to RoI
    assert( fcns.count(fcn) );
    fcns.erase(fcn);
    assert( !fcns.count(clone) );
    fcns.insert(clone);

    // erase non root basic blocks within fcn from
    for (Function::iterator bi = fcn->begin() ; bi != fcn->end() ; bi++)
      if ( !roots.count(&*bi) )
        bbs.erase(&*bi);

    // include all basic blocks within clone to bbs
    for (Function::iterator bi = clone->begin() ; bi != clone->end() ; bi++)
      bbs.insert(&*bi);

    // update the analysis
    // Find every context in which 'fcn' is immediately called by 'callfcn'
    typedef std::vector<const Ctx *> Ctxs;
    Ctxs affectedContexts;
    for(FoldManager::ctx_iterator k=fmgr.ctx_begin(), z=fmgr.ctx_end(); k!=z; ++k)
    {
      const Ctx *ctx = &*k;
      if( ctx->type != Ctx_Fcn )
        continue;
      if( (ctx->fcn != fcn) && (ctx->fcn != clone) )
        continue;

      affectedContexts.push_back( ctx );
    }

    // rename these contexts
    DEBUG(errs() << "  . - Updating intermediate analyses...\n");
    for(Ctxs::const_iterator k=affectedContexts.begin(), z=affectedContexts.end(); k!=z; ++k)
    {
      const Ctx *ctx = *k;
      DEBUG(errs() << "  . . - ctx: " << *ctx << '\n');

      const ValueToValueMapTy *vmap;
      if ( ctx->fcn == fcn )
        vmap = vmaps[ fcn ];
      else if ( ctx->fcn == clone )
        vmap = vmaps[ clone ];
      else
        assert(false);

      CtxToCtxMap cmap;
      AuToAuMap amap;
      fmgr.cloneContext(ctx, *vmap, cmap, amap);
      changes.contextRenamedViaClone(ctx, *vmap, cmap, amap);
    }
    DEBUG(errs() << "  . - done.\n");
  }
}

Function* RoI::genMappingFunction(Module* m, std::string name, Type* type, F2F& org2clone)
{
  assert(name == "__c2o" || name == "__o2c");

  FunctionType *fty = FunctionType::get( type, ArrayRef<Type*>(type), false );
  Function     *fcn = Function::Create( fty, GlobalVariable::ExternalLinkage, name, m);

  Value *arg = &*( fcn->arg_begin() );
  BasicBlock *fallthrough = BasicBlock::Create( m->getContext(), "cmp", fcn );

  for ( F2F::iterator i = org2clone.begin() ; i != org2clone.end() ; i++)
  {
    BasicBlock* bb = fallthrough;

    Value* op = NULL;
    if (name == "__c2o")
      op = i->second;
    else // __o2c
      op = const_cast<Function*>(i->first);
    assert(op);

    if (op->getType() != arg->getType())
      op = CastInst::CreatePointerCast(op, arg->getType(), "cast", bb);

    CmpInst* cmp = new ICmpInst(*bb, ICmpInst::ICMP_EQ, op, arg, "");

    BasicBlock* ret = BasicBlock::Create( m->getContext(), "ret", fcn );

    Value* retvalue = NULL;
    if (name == "__c2o")
      retvalue = const_cast<Function*>(i->first);
    else
      retvalue = i->second;
    assert(retvalue);

    if (retvalue->getType() != fcn->getReturnType())
      retvalue = CastInst::CreatePointerCast(retvalue, fcn->getReturnType(), "cast", ret);

    ReturnInst::Create(m->getContext(), retvalue, ret);

    fallthrough = BasicBlock::Create( m->getContext(), "cmp", fcn );
    BranchInst::Create( ret, fallthrough, cmp, bb );
  }

  ReturnInst::Create( m->getContext(), arg, fallthrough);

  return fcn;
}

void RoI::replaceIndirectCall(std::map<Type*, Function*>& m, CallInst* inst, Selector& selector)
{
  Value *calledvalue = inst->getCalledValue();
  Type  *ty = calledvalue->getType();

  if ( !m.count(ty) ) return;

  Function     *fcn = m[ty];
  InstInsertPt pt = InstInsertPt::Before(inst);

  Value* arg = calledvalue;
  if (ty != fcn->getReturnType())
  {
    CastInst* cast = CastInst::CreatePointerCast(arg, fcn->getReturnType(), "cast");
    pt << cast;
    addToLPS(selector, cast, inst);
    arg = cast;
  }

  CallInst *ci = CallInst::Create(fcn, ArrayRef<Value*>(arg), "replace-indirect-caller");
  pt << ci;
  addToLPS(selector, ci, inst);

  Value* ret = ci;
  if (ty != fcn->getReturnType())
  {
    CastInst* cast = CastInst::CreatePointerCast(ret, ty, "cast");
    pt << cast;
    addToLPS(selector, cast, inst);
    ret = cast;
  }

  std::vector<User*> uses;
  //for (Value::user_iterator ui=calledvalue->user_begin() ; ui != calledvalue->user_end() ; ui++)
  for (Value::user_iterator ui=calledvalue->user_begin() ; ui != calledvalue->user_end() ; ui++)
    uses.push_back(*ui);

  for (unsigned i = 0 ; i < uses.size() ; i++)
  {
    if (uses[i] != inst) continue;
    uses[i]->replaceUsesOfWith(calledvalue, ret);
  }
}

void RoI::createO2CFunctions(F2F &cloned, Selector &selector)
{
  // sort cloned by the function name

  std::map< std::string, std::pair<const Function*, Function*> > sorted_cloned;

  for (F2F::iterator fi = cloned.begin() ; fi != cloned.end() ; fi++)
  {
    const Function*   org = fi->first;
    const std::string name = org->getName().str();

    sorted_cloned[name] = *fi;
  }

  std::map<Type*, F2F>       pairs;
  std::map<Type*, Function*> c2os;
  std::map<Type*, Function*> o2cs;

  // use of function is not an instruction: likely to be a global array that stores function
  // pointers. If indirect function call uses the pointer stored in such an array, and if the call
  // is out of RoI, 'cloned' version of the function should be called.

  std::set<Type*> hasNonInstUse;

  Module* m = NULL;

  std::map< std::string, std::pair<const Function*, Function*> >::iterator fi = sorted_cloned.begin();
  std::vector< Type* > typeorder;

  for ( ; fi != sorted_cloned.end() ; fi++)
  {
    const Function* org = fi->second.first;
    Function*       clone = fi->second.second;

    m = clone->getParent();
    pairs[ org->getType() ].insert( std::make_pair(org, clone) );
    typeorder.push_back( org->getType() );

    for (Value::const_user_iterator ui=org->user_begin() ; ui != org->user_end() ; ui++)
      if (!isa<Instruction>(*ui))
        hasNonInstUse.insert(org->getType());
  }

  assert(m);

  // There are same types with different names. Merge them altogether.

  std::map<Type*, Type*> type2rep;
  std::set<Type*>        representative;

  for (Module::iterator fi = m->begin() ; fi != m->end() ; fi++)
  {
    for (inst_iterator ii = inst_begin(&*fi), ie = inst_end(&*fi); ii != ie ; ++ii)
    {
      CastInst* ci = dyn_cast<CastInst>(&*ii);
      if (!ci) continue;
      if ( !pairs.count(ci->getType()) ) continue;

      Type* dsttype = ci->getDestTy();
      Type* srctype = ci->getSrcTy();

      if ( representative.count(dsttype) )
        type2rep[srctype] = dsttype;
      else if ( representative.count(srctype) )
        type2rep[dsttype] = srctype;
      else
      {
        type2rep[srctype] = dsttype;
        representative.insert(dsttype);
      }
    }
  }

  for (std::map<Type*, Type*>::iterator ti = type2rep.begin(), te = type2rep.end() ; ti != te ; ti++)
  {
    Type* type = ti->first;
    Type* rep = ti->second;

    F2F& org2clone = pairs[type];
    F2F& org2clone_rep = pairs[rep];
    org2clone_rep.insert( org2clone.begin(), org2clone.end() );

    pairs.erase(type);
  }

  // create runtime functions to map the function pointer of the original function to
  // that of the cloned function
  for (unsigned i = 0 ; i < typeorder.size() ; i++)
  {
    Type* type = typeorder[i];

    if ( !pairs.count(type) ) continue;

    assert( !type2rep.count(type) );

    Function* c2o = genMappingFunction(m, "__c2o", type, pairs[type]);
    c2os[type] = c2o;

    // create o2c if the function with a type has non-inst use

    if ( hasNonInstUse.count(type) )
    {
      Function* o2c = genMappingFunction(m, "__o2c", type, pairs[type]);
      o2cs[type] = o2c;
    }
  }

  for (std::map<Type*, Type*>::iterator ti = type2rep.begin(), te = type2rep.end() ; ti != te ; ti++)
  {
    Type* type = ti->first;
    Type* rep = ti->second;

    if ( c2os.count(rep) )
      c2os[type] = c2os[rep];
    if ( o2cs.count(rep) )
      o2cs[type] = o2cs[rep];
  }

  // for all uses of function pointers in the region of interest, make them to use the value
  // returned by c2o function

  for ( BBSet::iterator si = bbs.begin() ; si != bbs.end() ; si++)
  {
    BasicBlock* bb = *si;

    std::vector<CallInst*> indirectcalls;

    for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
    {
      CallInst* ci = dyn_cast<CallInst>( &*ii );

      if (!ci) continue;
      if (ci->getCalledFunction() == NULL) indirectcalls.push_back(ci);
    }

    for (unsigned j = 0 ; j < indirectcalls.size() ; j++)
    {
      replaceIndirectCall(c2os, indirectcalls[j], selector);
    }
  }

  // for all uses of function pointers out of the RoI, make them to use the value from o2c function

  std::vector<CallInst*> indirectcalls;

  for (Module::iterator fi = m->begin() ; fi != m->end() ; fi++)
  {
    Function* func = &*fi;

    for (Function::iterator bi = func->begin(), be = func->end(); bi != be; ++bi)
    {
      BasicBlock* bb = &*bi;

      if (bbs.count(bb)) continue;

      for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
      {
        CallInst* ci = dyn_cast<CallInst>( &*ii );

        if (!ci) continue;
        if (ci->getCalledFunction() == NULL) indirectcalls.push_back(ci);
      }
    }
  }

  for (unsigned j = 0 ; j < indirectcalls.size() ; j++)
  {
    replaceIndirectCall(o2cs, indirectcalls[j], selector);
  }
}

void RoI::addToLPS(Selector &selector, Instruction *newInst, Instruction *gravity)
{
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    LoopParallelizationStrategy *lps = &*(i->second);
    lps->addInstruction(newInst,gravity);
  }
}

}
}

