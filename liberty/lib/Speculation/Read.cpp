#define DEBUG_TYPE "classify"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include "liberty/Analysis/PureFunAA.h"
#include "liberty/Analysis/SemiLocalFunAA.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Utilities/GetMemOper.h"

#include <stdio.h>
#include <sstream>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

static cl::opt<bool> AssertOnUnexpectedGetUOFailure(
  "assert-on-unexpected-get-uo-failure",
  cl::init(false),
  cl::NotHidden,
  cl::desc("(debugging) assert if an unexpected GetUO failure occurs"));


void Read::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - Read::contextRenamedViaClone: " << *changedContext << '\n';

  // Update escapes
  updateAu2Ctx2Count( escapes, cmap, amap );

  // Update locals
  updateAu2Ctx2Count( escapes, cmap, amap );

  // Update integerPredictions
  updateValue2Ctx2Inst( integerPredictions, vmap, cmap );

  // Update pointerPredictions
  // Update underlyingObjects
  updateValue2Ctx2Ptrs( pointerPredictions, vmap, cmap, amap );
  updateValue2Ctx2Ptrs( underlyingObjects, vmap, cmap, amap );

  // Update pointer residuals
  updateValue2Ctx2Residual( pointerResiduals, vmap, cmap );
}

void Read::updateAu2Ctx2Count( AU2Ctx2Count &oldMap, const CtxToCtxMap &cmap, const AuToAuMap &amap)
{
  AU2Ctx2Count newMap;

  const AuToAuMap::const_iterator amap_end = amap.end();
  const CtxToCtxMap::const_iterator cmap_end = cmap.end();

  for(AU2Ctx2Count::const_iterator i=oldMap.begin(), e=oldMap.end(); i!=e; ++i)
  {
    const AU *au = i->first;
    const AuToAuMap::const_iterator j = amap.find(au);
    if( j != amap_end )
      au = j->second;

    const Ctx2Count &oldC2C = i->second;
    for(Ctx2Count::const_iterator j=oldC2C.begin(), z=oldC2C.end(); j!=z; ++j)
    {
      const Ctx *ctx = j->first;
      unsigned count = j->second;

      const CtxToCtxMap::const_iterator k = cmap.find(ctx);
      if( k != cmap_end )
        ctx = k->second;

      newMap[ au ].insert( Ctx2Count::value_type( const_cast<Ctx*>(ctx),count) );
    }
  }

  oldMap.swap(newMap);
}

void Read::updateValue2Ctx2Inst( Value2Ctx2Ints &oldMap, const ValueToValueMapTy &vmap, const CtxToCtxMap &cmap )
{
  Value2Ctx2Ints newMap;

  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();
  const CtxToCtxMap::const_iterator cmap_end = cmap.end();

  for(Value2Ctx2Ints::const_iterator i=oldMap.begin(), e=oldMap.end(); i!=e; ++i)
  {
    const Value *value = i->first;
    const ValueToValueMapTy::const_iterator j = vmap.find(value);
    if( j != vmap_end )
      value = &*(j->second);

    const Ctx2Ints &oldC2I = i->second;
    for(Ctx2Ints::const_iterator j=oldC2I.begin(), f=oldC2I.end(); j!=f; ++j)
    {
      const Ctx *ctx = j->first;
      const CtxToCtxMap::const_iterator k = cmap.find(ctx);
      if( k != cmap_end )
        ctx = k->second;

      newMap[ value ][ const_cast<Ctx*>(ctx) ] = j->second;
    }
  }

  oldMap.swap(newMap);
}

void Read::updateValue2Ctx2Residual( Value2Ctx2Residual &oldMap, const ValueToValueMapTy &vmap, const CtxToCtxMap &cmap )
{
  Value2Ctx2Residual newMap;

  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();
  const CtxToCtxMap::const_iterator cmap_end = cmap.end();

  for(Value2Ctx2Residual::const_iterator i=oldMap.begin(), e=oldMap.end(); i!=e; ++i)
  {
    const Value *value = i->first;
    const ValueToValueMapTy::const_iterator j = vmap.find(value);
    if( j != vmap_end )
      value = &*(j->second);

    const Ctx2Residual &oldC2I = i->second;
    for(Ctx2Residual::const_iterator j=oldC2I.begin(), f=oldC2I.end(); j!=f; ++j)
    {
      const Ctx *ctx = j->first;
      const CtxToCtxMap::const_iterator k = cmap.find(ctx);
      if( k != cmap_end )
        ctx = k->second;

      newMap[ value ][ const_cast<Ctx*>(ctx) ] = j->second;
    }
  }

  oldMap.swap(newMap);
}



void Read::updateValue2Ctx2Ptrs( Value2Ctx2Ptrs &oldMap, const ValueToValueMapTy &vmap, const CtxToCtxMap &cmap, const AuToAuMap &amap )
{
  Value2Ctx2Ptrs newMap;

  const ValueToValueMapTy::const_iterator vmap_end = vmap.end();
  const AuToAuMap::const_iterator amap_end = amap.end();
  const CtxToCtxMap::const_iterator cmap_end = cmap.end();

  for(Value2Ctx2Ptrs::const_iterator i=oldMap.begin(), e=oldMap.end(); i!=e; ++i)
  {
    const Value *value = i->first;
    const ValueToValueMapTy::const_iterator j = vmap.find(value);
    if( j != vmap_end )
      value = &*(j->second);

    const Ctx2Ptrs &oldC2P = i->second;
    for(Ctx2Ptrs::const_iterator j=oldC2P.begin(), f=oldC2P.end(); j!=f; ++j)
    {
      const Ctx *ctx = j->first;
      const CtxToCtxMap::const_iterator k = cmap.find(ctx);
      if( k != cmap_end )
        ctx = k->second;

      const Ptrs &ptrs = j->second;
      for(Ptrs::const_iterator k=ptrs.begin(), z=ptrs.end(); k!=z; ++k)
      {
        Ptr newPtr = *k;

        const AuToAuMap::const_iterator l = amap.find( newPtr.au );
        if( l != amap_end )
          newPtr.au = l->second;

        newMap[ value ][ const_cast<Ctx*>( ctx ) ].push_back( newPtr );
      }
    }
  }

  oldMap.swap(newMap);
}


AU *Read::fold(AU *a) const { return fm->fold(a); }
Ctx *Read::fold(Ctx *c) const { return fm->fold(c); }

Ctx *Read::getCtx(const Loop *loop, const Ctx *within) const
{
  Ctx *parent = 0;

  const BasicBlock *header = loop->getHeader();

  if( loop->getParentLoop() )
    parent = getCtx( loop->getParentLoop(), within );
  else
    parent = getCtx( header->getParent(), within );

  Ctx *cc = new Ctx(Ctx_Loop, parent);
  cc->header = header;
  cc->depth = loop->getLoopDepth();

  return fold(cc);
}

Ctx *Read::getCtx(const Function *fcn, const Ctx *within) const
{
  Ctx *cc = new Ctx(Ctx_Fcn,within);
  cc->fcn = fcn;

  return fold(cc);
}

static Read::Ctx2Count empty_c2c;

const Read::Ctx2Count &Read::find_escapes(const AU *au) const
{
  assert( resultsValid() );
  AU2Ctx2Count::const_iterator i = escapes.find(au);
  if( i == escapes.end() )
    return empty_c2c;

  return i->second;
}

const Read::Ctx2Count &Read::find_locals(const AU *au) const
{
  assert( resultsValid() );
  AU2Ctx2Count::const_iterator i = locals.find(au);
  if( i == locals.end() )
    return empty_c2c;

  return i->second;
}

static Read::Ctx2Ints empty_c2i;

const Read::Ctx2Ints &Read::predict_int(const Value *v) const
{
  assert( resultsValid() );
  Value2Ctx2Ints::const_iterator i = integerPredictions.find(v);
  if( i == integerPredictions.end() )
    return empty_c2i;

  return i->second;
}

static Read::Ctx2Ptrs empty_c2p;

const Read::Ctx2Ptrs &Read::predict_pointer(const Value *v) const
{
  assert( resultsValid() );
  Value2Ctx2Ptrs::const_iterator i = pointerPredictions.find(v);
  if( i == pointerPredictions.end() )
    return empty_c2p;

  return i->second;
}

const Read::Ctx2Ptrs &Read::find_underylying_objects(const Value *v) const
{
  assert( resultsValid() );
  Value2Ctx2Ptrs::const_iterator i = underlyingObjects.find(v);
  if( i == underlyingObjects.end() )
    return empty_c2p;

  return i->second;
}

static Read::Ctx2Residual empty_c2r;

const Read::Ctx2Residual &Read::pointer_residuals(const Value *v) const
{
  assert( resultsValid() );
  Value2Ctx2Residual::const_iterator i = pointerResiduals.find(v);
  if( i == pointerResiduals.end() )
    return empty_c2r;

  return i->second;
}

static const BasicBlock *BlockPtr(const BasicBlock *i) { return i; }
static const BasicBlock *BlockPtr(const BasicBlock &i) { return &i; }

template <class BlockIterator>
bool Read::getFootprint(const BlockIterator &begin, const BlockIterator &end, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions, CallSiteSet &already) const
{
  for(BlockIterator i=begin; i!=end; ++i)
  {
    const BasicBlock *bb = BlockPtr(*i);

    if( ctrlspec->isSpeculativelyDead(bb) )
      continue;

    for(BasicBlock::const_iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      const Instruction *inst = &*j;
      if( !inst->mayReadFromMemory() && !inst->mayWriteToMemory() )
        continue;

      if( !getFootprint(inst,exec_ctx,reads,writes,reductions, already) )
      {
        // can't reason about op within callee
        DEBUG(
          errs() << "getFootprint: failed on " << *inst << " AT " << *exec_ctx << '\n'
                 << "          in: Function " << bb->getParent()->getName() << " :: block " << bb->getName() << '\n'
        );
        return false;
      }
    }
  }

  // successfully processed callee
  return true;
}

static bool isDeferrableIO(const Instruction *inst)
{
  return false; // no more deferrable IO
  /*
  CallSite cs = getCallSite(inst);
  if( !cs.getInstruction() )
    return false;

  const Function *callee = cs.getCalledFunction();
  if( !callee )
    return false;

  const std::string &name = callee->getName();

  return name == "printf"
  ||     name == "fprintf"
  ||     name == "fwrite"
  ||     name == "puts"
  ||     name == "putchar"
  ||     name == "fflush"
  ||     name == "vfprintf"
  ;
  */
}

bool Read::getFootprint(const Instruction *op, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions) const
{
  CallSiteSet already;
  return getFootprint(op,exec_ctx,reads,writes,reductions, already);
}

// Get a set of AUs which were written by this instruction
bool Read::getFootprint(const Instruction *op, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions, CallSiteSet &already) const
{
  const BasicBlock *parent = op->getParent();
  if( ctrlspec->isSpeculativelyDead(parent) )
    return true;

  if( const LoadInst *load = dyn_cast< LoadInst >(op) )
  {
    Ptrs aus;
    if( !getUnderlyingAUs(load->getPointerOperand(), exec_ctx, aus) )
      return false;

    if( Reduction::Type rt = Reduction::isReductionLoad(load) )
      for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
        reductions.push_back( ReduxAU(i->au,rt) );

    else
      for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
        reads.push_back( i->au );

    return true;
  }

  if( const StoreInst *store = dyn_cast< StoreInst >(op) )
  {
    Ptrs aus;
    if( !getUnderlyingAUs(store->getPointerOperand(), exec_ctx, aus) )
      return false;

    if( Reduction::Type rt = Reduction::isReductionStore(store) )
      for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
        reductions.push_back( ReduxAU(i->au,rt) );

    else
      for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
        writes.push_back( i->au );

    return true;
  }

  else if( const MemTransferInst *mti = dyn_cast< MemTransferInst >(op) )
  {
    Ptrs aus;
    if( !getUnderlyingAUs(mti->getDest(), exec_ctx, aus) )
      return false;
    for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
      writes.push_back( i->au );

    aus.clear();
    if( !getUnderlyingAUs(mti->getSource(), exec_ctx, aus) )
      return false;
    for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
      reads.push_back( i->au );

    return true;
  }

  else if( const MemSetInst *msi = dyn_cast< MemSetInst >(op) )
  {
    Ptrs aus;
    if( !getUnderlyingAUs(msi->getDest(), exec_ctx, aus ) )
      return false;

    for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
      writes.push_back( i->au );
    return true;
  }

  CallSite cs = getCallSite(op);
  if( !cs.getInstruction() )
  {
    // Not Store, MemIntrinsic or call.
    // This operation does not write to memory
    assert( !op->mayWriteToMemory() && !op->mayReadFromMemory() && "Unknown memory op");
    return true;
  }

  const Function *callee = cs.getCalledFunction();
  if( !callee )
  {
    DEBUG( errs() << "getFootprint: cannot determine indirect call " << *op << '\n');
    return false;
  }

  else if( ! callee->isDeclaration() )
  {
    std::pair<CallSiteSet::iterator,bool> res = already.insert(op);
    if( !res.second )
      return true; // already in there.

    const Ctx *cc = getCtx( callee, exec_ctx );
    const bool success = getFootprint(callee->begin(), callee->end(), cc, reads, writes, reductions, already);

    already.erase( res.first ); // erase it.
    return success;
  }

  // externally defined function.
  // Look it up in the pure,semi-local database.

  else if( pure->isReadOnly(callee) )
    return true; // write-none

  else if( pure->isLocal(callee) )
  {
    // footprint == the actual parameters
    for(CallSite::arg_iterator i=cs.arg_begin(),  e=cs.arg_end(); i!=e; ++i)
    {
      const Value *actual = *i;
      if( actual->getType()->isPointerTy() )
      {
        Ptrs aus;
        if( ! getUnderlyingAUs(actual, exec_ctx, aus) )
          return false;

        if( !pure->isReadOnly(callee) )
          for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
            writes.push_back( i->au );

        for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
          reads.push_back( i->au );
      }
    }

    return true;
  }

  else if( semi->isSemiLocal(callee,*pure) )
  {
    // footprint == the actual parameters + hidden state
    unsigned argno = 0;
    for(CallSite::arg_iterator i=cs.arg_begin(),  e=cs.arg_end(); i!=e; ++i, ++argno)
    {
      const Value *actual = *i;
      if( actual->getType()->isPointerTy() )
      {
        Ptrs aus;
        bool success = getUnderlyingAUs(actual, exec_ctx, aus);

        if( ! SemiLocalFunAA::readOnlyFormalArg(callee, argno) )
        {
          if( !success )
            return false;
          for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
            writes.push_back( i->au );
        }

        if( ! SemiLocalFunAA::writeOnlyFormalArg(callee, argno) )
        {
          if( !success )
            return false;
          for(Ptrs::iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
            reads.push_back( i->au );
        }
      }
    }

    // Here, this is unknown as-in Unmanaged.
    AU *hidden_state = fold(new AU(AU_IO));
    reads.push_back(hidden_state);

    if( !isDeferrableIO(op) )
    {
      reads.push_back(hidden_state);
      writes.push_back(hidden_state);
    }

//    errs() << "Adding hidden state to semi-local callsite " << *op << '\n';

    return true;
  }

  // Worst case, we know nothing about this external function.
  DEBUG(errs() << "getFootprint: cannot analyze external function " << *op << '\n');
  return false;
}


bool Read::getFootprint(const Loop *loop, const Ctx *ctx, AUs &reads, AUs &writes, ReduxAUs &reductions) const
{
  Loop::block_iterator begin = loop->block_begin(), end = loop->block_end();
  CallSiteSet already;
  return getFootprint(begin, end, ctx, reads, writes, reductions, already);
}

static bool isFieldInStructure(const GetElementPtrInst *gep, Type ** type_out, unsigned *fieldno_out)
{
  // Find the last type, field.
  Type *last_type = 0;
  unsigned last_index = 0;
  gep_type_iterator i = gep_type_begin(gep), e = gep_type_end(gep);
  User::const_op_iterator j = gep->idx_begin();
  for(; i!=e; ++i, ++j)
  {
    if( ConstantInt *last_index_ci = dyn_cast< ConstantInt >(*j) )
    {
      //sot : operator* is no longer supported in LLVM 5.0 for gep_type_iterator
      //last_type = *i;
      if (StructType *STy = i.getStructTypeOrNull())
        last_type = STy;
      else
        last_type = i.getIndexedType();

      last_index = last_index_ci->getLimitedValue();
    }
    else
      last_type = 0;
  }

  if( last_type == 0 )
    return false;

  if( !last_type->isStructTy() )
    return false;

  *type_out = last_type;
  *fieldno_out = last_index;
  return true;
}

static std::set<const GlobalVariable *> GuessGlobalVarAlreadyReported;
static std::set< std::pair<Type *, unsigned> > GuessFieldAlreadyReported;

bool Read::guess(const Value *uo, const Ctx *ctx, Ptrs &aus) const
{
  // Also try searching the pointer-prediction tables...
  // First, try to match context.  If that fails, try
  // any context.
  const Ctx2Ptrs &c2p = predict_pointer(uo);
  const Ctx *queryCtx;
  for(queryCtx=ctx; queryCtx && queryCtx->type == Ctx_Loop; queryCtx=queryCtx->parent)
    {}
  // Filter out observations which were not collected
  // within the appropriate context.
  typedef std::map<AU*,unsigned> AU2Freq;
  AU2Freq same_context, any_context;
  for(Ctx2Ptrs::const_iterator j=c2p.begin(), f=c2p.end(); j!=f; ++j)
  {
    const Ctx *obsCtx = j->first;
    const Ptrs &ptrs = j->second;

    // Filter-out samples which were not in the requested
    // context.
    const bool ctx_match = obsCtx->matches( queryCtx );

    // Note that these are pointer-predictions (au+offset),
    // but the return value should be AUs only (au+0)
    for(Ptrs::const_iterator i=ptrs.begin(), e=ptrs.end(); i!=e; ++i)
    {
      const Ptr &ptr = *i;
      if( ctx_match )
        same_context[ ptr.au ] += ptr.frequency;
      else
        any_context[ ptr.au ] += ptr.frequency;
    }
  }
  // Prefer guesses from the same context
  if( ! same_context.empty() )
  {
    for(AU2Freq::const_iterator i=same_context.begin(), e=same_context.end(); i!=e; ++i)
      aus.push_back( Ptr( i->first, 0, i->second ) );

//    errs() << "Guessing from pointer-prediction tables\n";
    return true;
  }
  // Fall back to guesses from any context
  if( ! any_context.empty() )
  {
    for(AU2Freq::const_iterator i=any_context.begin(), e=any_context.end(); i!=e; ++i)
      aus.push_back( Ptr( i->first, 0, i->second ) );

//    errs() << "Guessing from pointer-prediction tables\n";
    return true;
  }

  if( const LoadInst *load = dyn_cast< LoadInst >(uo) )
  {
    if( const GlobalVariable *gv = dyn_cast< GlobalVariable >( load->getPointerOperand() ) )
    {
      unsigned numVotes=0;

      // When else have we observed a load from this global?
      for(Value2Ctx2Ptrs::const_iterator i=underlyingObjects.begin(), e=underlyingObjects.end(); i!=e; ++i)
        if( const LoadInst *load2 = dyn_cast< LoadInst >( i->first ) )
          if( const GlobalVariable *gv2 = dyn_cast< GlobalVariable >( load2->getPointerOperand() ) )
            if( gv == gv2 )
            {
              // A new vote.
              for( Ctx2Ptrs::const_iterator j=i->second.begin(), z=i->second.end(); j!=z; ++j)
                aus.insert( aus.end(),
                  j->second.begin(), j->second.end() );
              ++numVotes;
            }

      if( numVotes > 0 )
      {
        if( ! GuessGlobalVarAlreadyReported.count(gv) )
        {
          const BasicBlock *bb = load->getParent();
          const Function *fcn = bb->getParent();
          errs() << "Guessing at load from global " << gv->getName() << " at " << fcn->getName() << "::" << bb->getName() << '\n';
          GuessGlobalVarAlreadyReported.insert(gv);
        }
        return true;
      }
    }

    else if( const GetElementPtrInst *gep = dyn_cast< GetElementPtrInst >( load->getPointerOperand() ) )
    {
      Type *structty = 0;
      unsigned fieldno = 0;
      if( isFieldInStructure(gep, &structty, &fieldno) )
      {
        unsigned numVotes = 0;

        // When else have we observed a load from this field of this structure?

        for(Value2Ctx2Ptrs::const_iterator i=underlyingObjects.begin(), e=underlyingObjects.end(); i!=e; ++i)
          if( const LoadInst *load2 = dyn_cast< LoadInst >( i->first ) )
            if( const GetElementPtrInst *gep2 = dyn_cast< GetElementPtrInst >( load2->getPointerOperand() ) )
            {
              Type *structty2 = 0;
              unsigned fieldno2 = 0;
              if( isFieldInStructure(gep2, &structty2, &fieldno2) )
                if( structty == structty2 && fieldno == fieldno2 )
                {
                  // A new vote.
                  std::pair<Type*,unsigned> key(structty,fieldno);
                  if( ! GuessFieldAlreadyReported.count(key) )
                  {
                    errs() << "Guessing at load from struct " << *structty
                           << " field " << fieldno << '\n';
                    GuessFieldAlreadyReported.insert(key);
                  }

                  for( Ctx2Ptrs::const_iterator j=i->second.begin(), z=i->second.end(); j!=z; ++j)
                  {
                    Ptrs::const_iterator k = j->second.begin(), q = j->second.end();
/*
                      errs() << "  " << *load << '\n';
                      if( k==q )
                        errs() << "  yields <empty>\n";
                      else
                      {
                        errs() << "  yields:\n";
                        for(Ptrs::const_iterator pp=k; pp!=q; ++pp)
                          errs() << "    " << *(pp->au) << '\n';
                      }
*/

                    aus.insert( aus.end(), k,q );
                  }
                  ++numVotes;
                }
            }

        if( numVotes > 0 )
        {
//          for(Ptrs::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
//            errs() << " guess by field: " << *(i->au) << '\n';
          return true;
        }
      }
    }
  }

  // All other cases: fail.
  return false;
}

bool Read::missingAUs(const Value *uo, const Ctx *ctx, Ptrs &aus) const
{
  // In most cases, an AU is missing from the profile because
  // the AU was statically determinate, and so we chose not to
  // instrument it.  This routine will try to statically determine
  // the AU in such cases, or failing that, make an educated guess.

  // Special cases: uo is a global / alloca / malloc / realloc.
  // These were not instrumented by the profiler because
  // they are statically determinate.

  // Is this object a cast of an integer constant to a pointer?
  // This is surprisingly common in spec benchmarks... :(
  if( const ConstantExpr *unary = dyn_cast< ConstantExpr >(uo) )
    if( unary->isCast() && isa< ConstantInt >( unary->getOperand(0) ) )
      return true; // no objects.

  if( isa< UndefValue >(uo) )
    return true; // no objects.

  else if( isa< ConstantPointerNull >(uo) )
  {
    AU *au = new AU( AU_Null );

    aus.push_back( Ptr( fold(au), 0, 1 ) );
    return true;
  }
  else if( const GlobalVariable *gv = dyn_cast< GlobalVariable >(uo) )
  {
    AU *au = new AU( gv->isConstant() ? AU_Constant : AU_Global);
    au->value = gv;

    aus.push_back( Ptr(fold(au), 0, 1) );
    return true;
  }
  else if( const AllocaInst *alloca = dyn_cast< AllocaInst >(uo) )
  {
    const Ctx *ctx = getCtx( alloca->getParent()->getParent() );
    AU *au = new AU( AU_Stack );
    au->value = alloca;
    au->ctx = ctx;

    aus.push_back( Ptr( fold(au), 0, 1) );
    return true;
  }
  else if( const LoadInst *load = dyn_cast< LoadInst >(uo) )
  {
    // Special cases for loading stdin/stdout/stderr,
    // since the profiler cannot capture the allocation
    // of their objects.
    if( const GlobalVariable *gv = dyn_cast< GlobalVariable >( load->getPointerOperand() ) )
    {
      StringRef  name = gv->getName();
      if( name == "stdin" || name == "stdout" || name == "stderr" )
      {
        // Here, this is 'Unknown' as in Unmanaged
        aus.push_back( Ptr( fold(new AU(AU_Unknown)), 0, 1 ) );
        return true;
      }
    }
  }
  else
  {
    CallSite cs = getCallSite(uo);
    if( cs.getInstruction() )
    {
      Function *callee = cs.getCalledFunction();
      if (callee &&
          (callee->getName() == "malloc" || callee->getName() == "calloc" ||
           callee->getName() == "realloc" || callee->getName() == "xalloc")) {
        const Ctx *ctx = getCtx( cs.getInstruction()->getParent()->getParent() );
        AU *au = new AU(AU_Heap);
        au->value = uo;
        au->ctx = ctx;
        aus.push_back( Ptr( fold(au), 0, 1 ) );
        return true;
      }
    }
  }

  // Okay, it failed... why?
  if( const Instruction *inst = dyn_cast< Instruction >(uo) )
    if( ctrlspec->isSpeculativelyDead(inst) )
    {
      // We're going to speculate this,
      // so we may as well ignore it.
      return true;
    }

  if ( const Argument *arg = dyn_cast< Argument >(uo) ) {
    if( ctrlspec->isSpeculativelyDead(&arg->getParent()->getEntryBlock()) )
    {
      // We're going to speculate this,
      // so we may as well ignore it.
      return true;
    }
  }

  // Make an educated guess.
  if( guess(uo,ctx,aus) )
    return true;

  // All other cases.
  DEBUG(errs() << "Read::getUnderlyingAUs: Unexpected failure on " << *uo);
  if( const Instruction *iuo = dyn_cast<Instruction>(uo) )
  {
    DEBUG(errs() << " in fcn " << iuo->getParent()->getParent()->getName()
           << ", bb " << iuo->getParent()->getName());
  }
  DEBUG(errs() << "\n  In context " << *ctx << '\n');

  if( AssertOnUnexpectedGetUOFailure )
    assert( false );

  return false;
}

uint16_t Read::getPointerResiduals(const Value *v, const Ctx *ctx) const
{
  const Ctx2Residual &c2r = pointer_residuals(v);

  const Ctx *queryCtx;
  for(queryCtx=ctx; queryCtx && queryCtx->type == Ctx_Loop; queryCtx=queryCtx->parent)
    {}

  uint16_t acc = 0;
  for(Ctx2Residual::const_iterator i=c2r.begin(), e=c2r.end(); i!=e; ++i)
    if( i->first->matches( queryCtx ) )
      acc |= i->second;

  return acc;
}


bool Read::getUnderlyingAUs(const Value *ptr, const Ctx *ctx, Ptrs &aus) const
{
//  bool isPointerInLoop = false;
//  if( ctx->type == Ctx_Loop )
//    if( const Instruction *iptr = dyn_cast<Instruction>(ptr) )
//      isPointerInLoop = ctx->contains(iptr);

  // Find underlying objects using static info.
  UO uos;

  //errs() << "getUnderlyingAUs for ptr:  " << *ptr <<  '\n';

  GetUnderlyingObjects(ptr, uos, *DL);


  // Note that, even if the pointer is computed
  // by an instruction within the loop, the underlying
  // objects may be outside of the loop, e.g. loop
  // live-in values.

  // Map those to predictions.
  for(UO::const_iterator i=uos.begin(), e=uos.end(); i!=e; ++i)
  {
    const Value *uo = *i;

    //errs() << "UO: " << *uo << '\n';

    const Ctx2Ptrs &c2p = find_underylying_objects(uo);
    if( c2p.empty() )
    {
      //errs() << "missing uo: " << *uo <<'\n';
      if( !missingAUs(uo, ctx, aus) )
        return false;
      continue;
    }

// TODO - temporary patch; pop up to the function.
    const Ctx *queryCtx;
    for(queryCtx=ctx; queryCtx && queryCtx->type == Ctx_Loop; queryCtx=queryCtx->parent)
      {}

    // Filter out observations which were not collected
    // within the appropriate context.
    for(Ctx2Ptrs::const_iterator j=c2p.begin(), f=c2p.end(); j!=f; ++j)
    {
      // Filter-out samples which were not in the requested
      // context.
      if( j->first->matches( queryCtx )  )
      {
//        DEBUG(
//          for(Ptrs::const_iterator x=j->second.begin(), q=j->second.end(); x!=q; ++x)
//          {
//            const Ptr &ptr = *x;
//            errs() << "Adding AU " << *ptr.au
//                 << "  because observed " << *uo << '\n'
//                   << "  in context " << *(j->first) << '\n';
//            break;
//          }
//        );

        // Report them to the caller
        aus.insert( aus.end(),
          j->second.begin(), j->second.end() );
      }
    }
  }

  return true;
}

bool Read::sem_escape_object(AU *au, Ctx *ctx, unsigned cnt)
{
  escapes[au][ctx] = cnt;
  return true;
}

bool Read::sem_local_object(AU *au, Ctx *ctx, unsigned cnt)
{
  locals[au][ctx] = cnt;
  return true;
}

bool Read::sem_int_predict(Value *v, Ctx *ctx, Ints &ints)
{
  integerPredictions[v][ctx] = ints;
  return true;
}

bool Read::sem_ptr_predict(Value *v, Ctx *ctx, Ptrs &ptrs)
{
  pointerPredictions[v][ctx] = ptrs;
  return true;
}

bool Read::sem_obj_predict(Value *v, Ctx *ctx, Ptrs &ptrs)
{
  underlyingObjects[v][ctx] = ptrs;
  return true;
}

bool Read::sem_pointer_residual(Value *v, Ctx *ctx, unsigned char bitvector)
{
  pointerResiduals[v][ctx] = bitvector;
  return true;
}


bool Read::areEverSimultaneouslyActive(const Ctx *A, const Ctx *B) const
{
  if( A->matches(B) || B->matches(A) )
    return true;

  for(FoldManager::ctx_iterator i=fm->ctx_begin(), e=fm->ctx_end(); i!=e; ++i)
  {
    const Ctx *ctx = &*i;

    if( ctx->matches(A) && ctx->matches(B) )
      return true;
  }

  return false;
}

Read::Read() : SemanticAction(), pure(0), semi(0), ctrlspec(0)
{
  fm = new FoldManager;
}

Read::~Read()
{
  delete fm;
}

void Read::setPureFunAA(const PureFunAA *pfaa) { pure = pfaa; }
void Read::setSemiLocalFunAA(const SemiLocalFunAA *slfaa) { semi = slfaa; }
void Read::setControlSpeculator(ControlSpeculation *ctrl) { ctrlspec = ctrl; }

bool Read::predictIntAtLoop(const Value *v, const Ctx *ctx, Ints &predictions) const
{
  predictions.clear();

  const Ctx2Ints &c2i = predict_int(v);

  unsigned numFound = 0;
  for(Ctx2Ints::const_iterator i=c2i.begin(), e=c2i.end(); i!=e; ++i)
  {
    const Ctx *c0 = i->first;
    if( c0->matches( ctx ) && ! c0->isWithinSubloopOf(ctx) )
    {
      predictions.insert( predictions.end(),
        i->second.begin(), i->second.end() );

      ++numFound;
    }
  }

  return (numFound == 1);
}

bool Read::predictPtrAtLoop(const Value *v, const Ctx *ctx, Ptrs &predictions) const
{
  predictions.clear();

  const Ctx2Ptrs &c2p = predict_pointer(v);

  unsigned numFound = 0;
  for(Ctx2Ptrs::const_iterator i=c2p.begin(), e=c2p.end(); i!=e; ++i)
  {
    const Ctx *c0 = i->first;
    if( c0->matches( ctx ) && ! c0->isWithinSubloopOf(ctx) )
    {
      predictions.insert( predictions.end(),
        i->second.begin(), i->second.end() );

      ++numFound;
    }
  }

  return (numFound == 1);
}

void Read::removeInstructionFromPtrs(const Instruction *no_longer_exists, Ptrs &collection)
{
  for(unsigned j=0; j<collection.size(); ++j)
  {
    Ptr &ptr = collection[j];
    if( ptr.referencesValue( no_longer_exists ) )
    {
      std::swap(ptr, collection.back() );
      collection.pop_back();
      --j;
    }
  }
}

void Read::removeInstructionFromCtx2Ptrs(const Instruction *no_longer_exists, Ctx2Ptrs &collection)
{
  std::vector<Ctx*> to_delete;
  for(Ctx2Ptrs::iterator j=collection.begin(), z=collection.end(); j!=z; ++j)
  {
    Ctx *ctx = j->first;
    if( ctx->referencesValue( no_longer_exists ) )
    {
      to_delete.push_back( ctx );
      continue;
    }

    removeInstructionFromPtrs( no_longer_exists, j->second );
  }

  for(unsigned i=0, N=to_delete.size(); i<N; ++i)
    collection.erase( to_delete[i] );
}

void Read::removeInstructionFromValue2Ctx2Ptrs(const Instruction *no_longer_exists, Value2Ctx2Ptrs &collection)
{
  collection.erase(no_longer_exists);
  for(Value2Ctx2Ptrs::iterator i=collection.begin(), e=collection.end(); i!=e; ++i)
    removeInstructionFromCtx2Ptrs( no_longer_exists, i->second );
}



void Read::removeInstruction(const Instruction *no_longer_exists)
{
  removeInstructionFromValue2Ctx2Ptrs(no_longer_exists, pointerPredictions);
  removeInstructionFromValue2Ctx2Ptrs(no_longer_exists, underlyingObjects);
}

static cl::opt<std::string> ProfileFileName("specpriv-profile-filename",
  cl::init("result.specpriv.profile.txt"),
  cl::NotHidden,
  cl::desc("Read specpriv-profile results from this file"));

void ReadPass::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< PureFunAA >();
  au.addRequired< SemiLocalFunAA >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.setPreservesAll();
}


bool ReadPass::runOnModule(Module &mod)
{
  if( read )
    delete read;

  read = new Read;

  //sot
  const DataLayout *DL = &mod.getDataLayout();

  Parse parser(mod);
  parser.parse(ProfileFileName.c_str(), read);

  const PureFunAA &pure = getAnalysis< PureFunAA >();
  read->setPureFunAA(&pure);

  const SemiLocalFunAA &semi = getAnalysis< SemiLocalFunAA >();
  read->setSemiLocalFunAA( &semi );

  read->setControlSpeculator( getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr() );

  read->setDataLayout(DL);

  return false;
}


char ReadPass::ID = 0;
static RegisterPass<ReadPass> rp("read-specpriv-profile", "Read spec-priv profile",true,true);

}
}

