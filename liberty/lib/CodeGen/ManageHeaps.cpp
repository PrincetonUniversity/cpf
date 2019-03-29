#define DEBUG_TYPE "doall-transform"

#include "liberty/CodeGen/DOALLTransform.h"
#include "llvm/ADT/Statistic.h"

namespace liberty {
using namespace llvm;

STATISTIC(numStaticReloc, "Static allocations relocated");
STATISTIC(numDynReloc,    "Dynamic allocations relocated");

bool DOALLTransform::startInitializationFunction()
{
  // OUTSIDE of parallel region
  Function *init = Function::Create(fv2v, GlobalValue::InternalLinkage, "__specpriv_startup", mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *entry = BasicBlock::Create(ctx, "entry", init);

  initFcn = InstInsertPt::End(entry);

  Constant *beginfcn = Api(mod).getBegin();
  initFcn << CallInst::Create(beginfcn);

  return true;
}

bool DOALLTransform::startFinalizationFunction()
{
  // OUTSIDE of parallel region
  Function *fini = Function::Create(fv2v, GlobalValue::InternalLinkage, "__specpriv_shutdown", mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *entry = BasicBlock::Create(ctx, "entry", fini);

  finiFcn = InstInsertPt::End(entry);
  return true;
}

bool DOALLTransform::finishInitializationFunction()
{
  // OUTSIDE of parallel region
  LLVMContext &ctx = mod->getContext();
  initFcn << ReturnInst::Create(ctx);

  callBeforeMain( initFcn.getFunction() );
  return true;
}

bool DOALLTransform::finishFinalizationFunction()
{
  // OUTSIDE of parallel region
  Constant *endfcn = Api(mod).getEnd();
  finiFcn << CallInst::Create(endfcn);

  LLVMContext &ctx = mod->getContext();
  finiFcn << ReturnInst::Create(ctx);

  callAfterMain( finiFcn.getFunction() );
  return true;
}

void DOALLTransform::insertMemcpy(InstInsertPt &where, Value *dst, Value *src, Value *sz)
{
  // OUTSIDE of parallel region (called within constructor)
  Value *s = src;
  if( s->getType() != voidptr )
  {
    Instruction *cast = new BitCastInst(s,voidptr);
    where  << cast;
    s = cast;
  }

  Value *d = dst;
  if( d->getType() != voidptr )
  {
    Instruction *cast = new BitCastInst(d,voidptr);
    where << cast;
    s = cast;
  }

  LLVMContext &ctx = mod->getContext();
  Type *u1 = Type::getInt1Ty(ctx);
  Type *u32 = Type::getInt32Ty(ctx);

  std::vector<Type*> formals(5);
  formals[0] = voidptr;
  formals[1] = voidptr;
  formals[2] = u32;
  formals[3] = u32;
  formals[4] = u1;
  FunctionType *fty = FunctionType::get(voidty, formals, false);
  Constant *memcpy =  mod->getOrInsertFunction("llvm.memcpy.p0i8.p0i8.i32", fty);

  Value *one = ConstantInt::get(u32, 1);
  Value *fls = ConstantInt::getFalse(ctx);

  Value *actuals[] = { d, s, sz, one, fls };
  where << CallInst::Create(memcpy, ArrayRef<Value*>(&actuals[0], &actuals[5]) );
}

// dummy version not to break API
struct ReplaceConstant2PreprocessAdaptor : ReplaceConstantObserver
{
  //ReplaceConstant2PreprocessAdaptor(Preprocess &prep) : preprocess(prep) {}
  ReplaceConstant2PreprocessAdaptor() {}

  virtual void addInstruction(Instruction *newInst, Instruction *gravity)
  {
    //preprocess.addToLPS(newInst,gravity);
  }

//private:
//  Preprocess &preprocess;
};

bool DOALLTransform::reallocateGlobals(const HeapAssignment::AUSet &aus, const HeapAssignment::Type heap)
{
  // OUTSIDE of parallel region
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = mod->getDataLayout();

  //Preprocess &preprocess = getAnalysis< Preprocess >();
  //ReplaceConstant2PreprocessAdaptor adaptor(preprocess);
  ReplaceConstant2PreprocessAdaptor adaptor();

  bool modified = false;
  for(HeapAssignment::AUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *au = *i;

    if( au->type != AU_Constant
    &&  au->type != AU_Global )
      continue;

    const GlobalVariable *cgv = dyn_cast< GlobalVariable >( au->value );
    assert( cgv );
    GlobalVariable *gv = const_cast< GlobalVariable* >( cgv );

    DEBUG(errs() << "Static AU: " << gv->getName() << " ==> heap " << Api::getNameForHeap( heap ) << '\n');

    // create a new global pointer.
    PointerType *pty = cast< PointerType >( gv->getType() );
    Type *eltty = pty->getElementType();
    Twine name = "__reallocated_" + gv->getName();
    GlobalVariable *gvptr = new GlobalVariable(
      *mod,
      pty,
      false,
      GlobalValue::InternalLinkage,
      ConstantPointerNull::get(pty),
      name);

    // replace all uses of the global.
    assert(replaceConstantWithLoad(gv, gvptr, adaptor)
      && "Couldn't replace constant with load.");

    // allocate the object in the initialization function.
    uint64_t size = td.getTypeStoreSize( eltty );
    Constant *sz = ConstantInt::get(int32,size);

    // sub-heap
    Constant *subheap = ConstantInt::get(int8, asgn->getSubHeap(au) );

    Constant *alloc = Api(mod).getAlloc(heap);
    Value *actuals[] = {sz, subheap};
    Instruction *allocate = CallInst::Create(alloc, ArrayRef<Value*>(&actuals[0], &actuals[2]));
    initFcn << allocate;
    Value *newAU = allocate;

    Instruction *cast = new BitCastInst(newAU, pty);
    initFcn << cast << new StoreInst(cast, gvptr);

    // if has initializer, copy initial value.
    if( gv->hasInitializer() && !isa< ConstantAggregateZero >( gv->getInitializer() ) )
      insertMemcpy(initFcn,allocate,gv,sz);

    // free the object in the finalization function
    Constant *free = Api(mod).getFree(heap);
    Instruction *load = new LoadInst(gvptr);
    finiFcn << load;
    Value *actual = load;
    if( actual->getType() != voidptr )
    {
      Instruction *cast = new BitCastInst(load,voidptr);
      finiFcn << cast;
      actual = cast;
    }
    finiFcn << CallInst::Create(free, actual)
            << new StoreInst( ConstantPointerNull::get(pty), gvptr);

    ++numStaticReloc;
    modified = true;
  }

  return modified;
}

bool DOALLTransform::reallocateGlobals(const HeapAssignment::ReduxAUSet &aus)
{
  // OUTSIDE of parallel region
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = mod->getDataLayout();

  //Preprocess &preprocess = getAnalysis< Preprocess >();
  //ReplaceConstant2PreprocessAdaptor adaptor(preprocess);
  ReplaceConstant2PreprocessAdaptor adaptor();

  bool modified = false;
  for(HeapAssignment::ReduxAUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Reduction::Type redty = i->second;

    if( au->type != AU_Constant
    &&  au->type != AU_Global )
      continue;

    GlobalVariable *gv = const_cast< GlobalVariable*>( cast< GlobalVariable >( au->value ) );

    if( gv->hasExternalLinkage() )
      continue;

    DEBUG(errs() << "Static AU: " << gv->getName() << " ==> heap redux\n");

    // create a new global pointer.
    PointerType *pty = cast< PointerType >( gv->getType() );
    Type *eltty = pty->getElementType();
    Twine name = "__reallocated_" + gv->getName();
    GlobalVariable *gvptr = new GlobalVariable(
      *mod,
      pty,
      false,
      GlobalValue::InternalLinkage,
      ConstantPointerNull::get(pty),
      name);

    // replace all uses of the global.
    assert(replaceConstantWithLoad(gv, gvptr, adaptor)
      && "Couldn't replace constant with load.");

    // allocate the object in the initialization function.
    uint64_t size = td.getTypeStoreSize( eltty );
    Constant *sz = ConstantInt::get(int32,size);

    Constant *subheap = ConstantInt::get(int8, asgn->getSubHeap(au) );

    Constant *alloc = Api(mod).getAlloc( HeapAssignment::Redux );
    Value *actuals[] = { sz, subheap, ConstantInt::get(int8, redty) };
    Instruction *allocate = CallInst::Create(alloc, ArrayRef<Value*>(&actuals[0], &actuals[3]) );
    initFcn << allocate;
    Value *newAU = allocate;

    Instruction *cast = new BitCastInst(newAU, pty);
    initFcn << cast << new StoreInst(cast, gvptr);

    // if has initializer, copy initial value.
    if( gv->hasInitializer() && !isa< ConstantAggregateZero >( gv->getInitializer() ) )
      insertMemcpy(initFcn,allocate,gv,sz);

    // free the object in the finalization function
    Constant *free = Api(mod).getFree( HeapAssignment::Redux );
    Instruction *load = new LoadInst(gvptr);
    finiFcn << load;
    Value *actual = load;
    if( actual->getType() != voidptr )
    {
      Instruction *cast = new BitCastInst(load,voidptr);
      finiFcn << cast;
      actual = cast;
    }
    finiFcn << CallInst::Create(free, actual)
            << new StoreInst( ConstantPointerNull::get(pty), gvptr);

    ++numStaticReloc;
    modified = true;
  }

  return modified;
}

bool DOALLTransform::reallocateStaticAUs()
{
  // OUTSIDE of parallel region
  bool modified = false;

  modified |= reallocateGlobals(asgn->getSharedAUs(),   HeapAssignment::Shared );
  modified |= reallocateGlobals(asgn->getLocalAUs(),    HeapAssignment::Local );
  modified |= reallocateGlobals(asgn->getPrivateAUs(),  HeapAssignment::Private );
  modified |= reallocateGlobals(asgn->getReadOnlyAUs(), HeapAssignment::ReadOnly );

  modified |= reallocateGlobals(asgn->getReductionAUs());

  return modified;
}

Value *DOALLTransform::determineSize(Instruction *gravity, InstInsertPt &where, Instruction *inst)
{
  //Preprocess &preprocess = getAnalysis< Preprocess >();
  CallSite cs = getCallSite(inst);

  if( AllocaInst *alloca = dyn_cast< AllocaInst >(inst) )
  {
    //DataLayout &td = getAnalysis< DataLayout >();
    const DataLayout &td = inst->getParent()->getParent()->getParent()->getDataLayout();
    uint64_t size = td.getTypeStoreSize( alloca->getAllocatedType() );
    return ConstantInt::get(int32,size);
  }

  else if( Indeterminate::isMalloc(cs) )
  {
    Value *sz = cs.getArgument(0);
    if( sz->getType() != int32 )
    {
      Instruction *cast = new TruncInst(sz,int32);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      sz = cast;
    }
    return sz;
  }

  else if( Indeterminate::isCalloc(cs) )
  {
    Value *count = cs.getArgument(0);
    Value *size  = cs.getArgument(1);

    Instruction *mul = BinaryOperator::CreateNSWMul(count,size);
    where << mul;
    //preprocess.addToLPS(mul, gravity);

    Value *sz = mul;
    if( sz->getType() != int32 )
    {
      Instruction *cast = new TruncInst(sz,int32);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      sz = cast;
    }

    return sz;
  }

  else if( Indeterminate::isRealloc(cs) )
  {
    Value *sz = cs.getArgument(1);
    if( sz->getType() != int32 )
    {
      Instruction *cast = new TruncInst(sz,int32);
      where << cast;
      //preprocess.addToLPS(cast, gravity);
      sz = cast;
    }
    return sz;
  }


  errs() << *inst;
  assert(false && "Wtf");
}

bool DOALLTransform::reallocateInst(const HeapAssignment::AUSet &aus, const HeapAssignment::Type heap)
{
  bool modified = false;

  //Preprocess &preprocess = getAnalysis< Preprocess >();
/*
  DEBUG(errs() << "\n\nReallocateInst " << heap_names[heap] << " will do this:\n");
  for(HeapAssignment::AUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
    DEBUG(errs() << " - " << **i << " => " << heap_names[heap] << '\n');
  DEBUG(errs() << "\nAnd now I'm gonna doit:\n\n");
*/
  std::set<const Value*> already;
  for(HeapAssignment::AUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *au = *i;
    if( au->type != AU_Stack && au->type != AU_Heap )
      continue;

    if( already.count( au->value ) )
      continue;
    already.insert( au->value );
    Instruction *origI = const_cast< Instruction* >( dyn_cast< Instruction >( au->value ) );
    assert( origI );

    // get cloned inst in the parallelized code if any
    Instruction *inst = origI;
    if (task->instructionClones.find(origI) != task->instructionClones.end())
      inst = task->instructionClones[origI];

    DEBUG(errs() << "Dynamic AU: " << *inst << " ==> heap " << Api::getNameForHeap( heap ) << '\n');

    Function *fcn = inst->getParent()->getParent();
    InstInsertPt where = InstInsertPt::After(inst);

    // Determine size of allocation
    Value *sz = determineSize(inst, where, inst);

    Constant *subheap = ConstantInt::get(int8, asgn->getSubHeap(au));
    Value *actuals[] = {sz, subheap};

    // Add code to perform allocation
    Constant *alloc = Api(mod).getAlloc( heap );
    Instruction *allocate = CallInst::Create(alloc, ArrayRef<Value*>(&actuals[0], &actuals[2]));
    where << allocate;
    //preprocess.replaceInLPS(allocate, inst);
    Value *newAU = allocate;

    if( newAU->getType() != inst->getType() )
    {
      Instruction *cast = new BitCastInst(newAU, inst->getType());
      where << cast;
      //preprocess.addToLPS(cast, allocate);
      newAU = cast;
    }

    // Replace old allocation
    newAU->takeName(inst);
    inst->replaceAllUsesWith(newAU);
    //preprocess.getRecovery().replaceAllUsesOfWith(inst,newAU);
    inst->eraseFromParent();

    // Manually free stack variables
    if( au->type == AU_Stack )
    {
      // At each function exit (return, unwind, or unreachable...)
      for(Function::iterator j=fcn->begin(), z=fcn->end(); j!=z; ++j)
      {
        BasicBlock *bb = &*j;
 //       errs() << "bb: " << *bb << "\n\n\n\n";
        TerminatorInst *term = bb->getTerminator();
        InstInsertPt where;
        if( isa<ReturnInst>(term) )
          where = InstInsertPt::Before(term);

        else if( isa<UnreachableInst>(term) )
        {
          where = InstInsertPt::Before(term);

          // This unreachable terminator is probably prededed by
          // a call to a noreturn function...
          for(BasicBlock::iterator k=bb->begin(); k!=bb->end(); ++k)
          {
            CallSite cs = getCallSite( &*k );
            if( !cs.getInstruction() )
              continue;

            if( cs.doesNotReturn() )
            {
              where = InstInsertPt::Before( cs.getInstruction() );
              break;
            }
          }
        }

        else
          continue;

        // Free the allocation
        Constant *free = Api(mod).getFree( heap );
        where << CallInst::Create(free, allocate);
      }
    }

    ++numDynReloc;
    modified = true;
  }

  return modified;
}

// TODO: generalize and merge with the other version of reallocateInst.
bool DOALLTransform::reallocateInst(const HeapAssignment::ReduxAUSet &aus)
{
  bool modified = false;

  //Preprocess &preprocess = getAnalysis< Preprocess >();

  std::set<const Value *> already;
  for(HeapAssignment::ReduxAUSet::const_iterator i=aus.begin(), e=aus.end(); i!=e; ++i)
  {
    AU *au = i->first;
    Reduction::Type redty = i->second;
    if( au->type != AU_Stack && au->type != AU_Heap )
      continue;

    if( already.count( au->value ) )
      continue;
    already.insert( au->value );
    Instruction *origI = const_cast< Instruction* >( dyn_cast< Instruction >( au->value ) );
    assert( origI );

    // get cloned inst in the parallelized code if any
    Instruction *inst = origI;
    if (task->instructionClones.find(origI) != task->instructionClones.end())
      inst = task->instructionClones[origI];

    DEBUG(errs() << "Dynamic AU: " << *inst << " ==> heap redux\n");

    Function *fcn = inst->getParent()->getParent();
    InstInsertPt where = InstInsertPt::After(inst);

    // Determine size of allocation
    Value *sz = determineSize(inst, where, inst);
    Constant *subheap = ConstantInt::get(int8, asgn->getSubHeap(au));

    // Add code to perform allocation
    Constant *alloc = Api(mod).getAlloc( HeapAssignment::Redux );
    Value *actuals[] = { sz, subheap, ConstantInt::get(int8, redty) };
    Instruction *allocate = CallInst::Create(alloc, ArrayRef<Value*>(&actuals[0], &actuals[3]) );
    where << allocate;
    Value *newAU = allocate;

    if( newAU->getType() != inst->getType() )
    {
      Instruction *cast = new BitCastInst(newAU, inst->getType());
      where << cast;
      newAU = cast;
    }

    // Replace old allocation
    newAU->takeName(inst);
    inst->replaceAllUsesWith(newAU);
    //preprocess.getRecovery().replaceAllUsesOfWith(inst,newAU);
    inst->eraseFromParent();

    // Manually free stack variables
    if( au->type == AU_Stack )
    {
      // At each function exit (return, unwind, or unreachable...)
      for(Function::iterator j=fcn->begin(), z=fcn->end(); j!=z; ++j)
      {
        BasicBlock *bb = &*j;
        TerminatorInst *term = bb->getTerminator();
        InstInsertPt where;
        if( isa<ReturnInst>(term) )
          where = InstInsertPt::Before(term);

        else if( isa<UnreachableInst>(term) )
        {
          errs() << "Not yet implemented: handle unreachable correctly.\n"
                 << " - If 'unreachable' is reached, some memory will not be freed.\n";
          continue;
        }

        else
          continue;

        // Free the allocation
        Constant *free = Api(mod).getFree( HeapAssignment::Redux );
        where << CallInst::Create(free, allocate);
      }
    }

    ++numDynReloc;
    modified = true;
  }

  return modified;
}

bool DOALLTransform::reallocateDynamicAUs()
{
  bool modified = false;

  modified |= reallocateInst(asgn->getSharedAUs(),   HeapAssignment::Shared );
  modified |= reallocateInst(asgn->getLocalAUs(),    HeapAssignment::Local );
  modified |= reallocateInst(asgn->getPrivateAUs(),  HeapAssignment::Private );
  modified |= reallocateInst(asgn->getReadOnlyAUs(), HeapAssignment::ReadOnly );

  modified |= reallocateInst(asgn->getReductionAUs() );

  return modified;
}

HeapAssignment::Type DOALLTransform::selectHeap(const Value *ptr, const Ctx *ctx) const
{
  Ptrs aus;
  if( !read->getUnderlyingAUs(ptr,ctx,aus) )
    return HeapAssignment::Unclassified;

  return asgn->classify(aus);
}

bool DOALLTransform::replaceFrees()
{
  Function *free = mod->getFunction("free");
  if( !free )
    return false;

  bool modified = false;

  Api api(mod);

  // collect set of clone insts
  // get map of cloneInsts to original ones
  //std::unordered_map<Instruction *, Instruction *> cloneToOrigMap;
  std::unordered_set<Instruction *> cloneInstsSet;
  for (auto instPair: task->instructionClones) {
    //auto *origInst = instPair->first;
    Instruction *cloneInst = instPair.second;
    //cloneToOrigMap[cloneInst] = origInst;
    cloneInstsSet.insert(cloneInst);
  }

  // For each call to the function free
  std::vector<User*> users( free->user_begin(), free->user_end() );
  for(unsigned i=0; i<users.size(); ++i)
  {
    User *user = users[i];
    CallSite cs = getCallSite(user);
    Instruction *callinst = cs.getInstruction();
    if( !callinst )
      continue;
    if( cs.getCalledValue() != free )
      continue;
//    if( ! roi.bbs.count( callinst->getParent() ) )
//      continue;

    // get cloned inst in the parallelized code if any
    Instruction *cloneCallInst = callinst;
    if (task->instructionClones.find(callinst) != task->instructionClones.end())
      cloneCallInst = task->instructionClones[callinst];

    // clone insts handled in another iteration
    //if (cloneToOrigMap.find(callinst) != cloneToOrigMap.end())
    if (cloneInstsSet.count(callinst))
      continue;

    BasicBlock *callbb = callinst->getParent();
    Function *callfcn = callbb->getParent();
    const Ctx *ctx = read->getCtx(callfcn);

    Value *ptr = cs.getArgument(0);

    HeapAssignment::Type heap = selectHeap(ptr, ctx);
    Constant *replacement = api.getFree( heap );

    cloneCallInst->replaceUsesOfWith( free, replacement );
    modified = true;

    // TODO: what about local objects in the recovery function!?
  }

  return modified;
}

bool DOALLTransform::manageHeaps()
{
  bool modified = false;

  // Make a global constructor function
  modified |= startInitializationFunction();
  modified |= startFinalizationFunction();

  // Reallocate global variables in the appropriate heaps
  modified |= reallocateStaticAUs();

  // Replace calls to free.
  modified |= replaceFrees();

  // Reallocate stack variables, heap allocations
  modified |= reallocateDynamicAUs();

  modified |= finishInitializationFunction();
  modified |= finishFinalizationFunction();

  return modified;
}

bool DOALLTransform::manageNonSpecHeaps()
{
  bool modified = false;

  HeapAssignment::AUSet &shared = asgn->getSharedAUs();
  HeapAssignment::AUSet &readonly = asgn->getReadOnlyAUs();
  HeapAssignment::AUSet &priv = asgn->getPrivateAUs();
  HeapAssignment::AUSet &local = asgn->getLocalAUs();

  for (AU* au: priv) {
    if (!nonSpecPrivAUs.count(au))
      shared.insert(au);
  }

  for (AU* au: readonly) {
    if (!nonSpecPrivAUs.count(au))
      shared.insert(au);
  }

  priv.clear();
  readonly.clear();

  for (AU* au: nonSpecPrivAUs)
    priv.insert(au);

  modified |= manageHeaps();

  return modified;
}


}
