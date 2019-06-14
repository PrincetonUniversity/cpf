#define DEBUG_TYPE "specpriv-transform"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Speculation/Read.h"
#include "liberty/Speculation/Selector.h"
#include "liberty/Redux/Reduction.h"
#include "liberty/PointsToProfiler/Indeterminate.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"
#include "liberty/Utilities/Timer.h"

#include "liberty/Speculation/Api.h"
#include "liberty/Speculation/Classify.h"
#include "liberty/Speculation/Discriminator.h"
//#include "liberty/Speculation/PtrResidueManager.h"
#include "liberty/CodeGen/Preprocess.h"
#include "liberty/CodeGen/ApplySeparationSpeculation.h"
#include "liberty/Speculation/Recovery.h"
#include "liberty/CodeGen/RoI.h"
#include "liberty/Speculation/RemedSelector.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numStaticReloc, "Static allocations relocated");
STATISTIC(numDynReloc,    "Dynamic allocations relocated");
STATISTIC(numUOTests,     "UO tests inserted");
STATISTIC(numPrivRead,    "Private reads instrumented");
STATISTIC(numPrivWrite,   "Private writes instrumented");
STATISTIC(numReduxWrite,  "Redux writes instrumented");

static cl::opt<bool> DontCheckPrivacy(
  "evil-dont-check-privacy", cl::init(false), cl::Hidden,
  cl::desc("Do not insert checks for private reads/writes"));

const Selector &ApplySeparationSpec::getSelector() const
{
  return getAnalysis< Selector >();
}

const HeapAssignment &ApplySeparationSpec::getHeapAssignment() const
{
  Selector &selector = getAnalysis< Selector >();
  return selector.getAssignment();
}

void ApplySeparationSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< ReadPass >();
  au.addRequired< ModuleLoops >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();

  au.addPreserved< ReadPass >();
  au.addPreserved< ModuleLoops >();
  au.addPreserved< Selector >();
  au.addPreserved< RemedSelector >();
  //au.addPreserved< SpecPrivSelector >();
  //au.addPreserved< SmtxSelector >();
  //au.addPreserved< Smtx2Selector >();
  au.addPreserved< Preprocess >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  //au.addPreserved< PtrResidueSpeculationManager >();
  //au.addPreserved< SmtxSpeculationManager >();
}

bool ApplySeparationSpec::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplySeparationSpec\n\n\n");
  mod = &module;
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Preprocess &preprocess = getAnalysis< Preprocess >();

  init(mloops);

  if( loops.empty() )
    return false;

  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Separation speculation:
  // - Reallocation
  // - UO checks
  // - Privacy guards
  // - Reduction guards

  // Perform per-loop preprocessing.

  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "SpecPriv ApplySeparationSpec: Processing loop "
      << fcn->getName() << " :: " << header->getName() << "\n");

    modified |= runOnLoop(loop);
  }

  // Replace object allocations with heap-specific
  // allocation operations.
  modified |= manageHeaps();

  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    const RoI &roi = preprocess.getRoI();
    for(RoI::FSet::iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    preprocess.assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  return modified;
}


void ApplySeparationSpec::init(ModuleLoops &mloops)
{
  LLVMContext &ctx = mod->getContext();

  voidty = Type::getVoidTy(ctx);
  u8 = Type::getInt8Ty(ctx);
  u16 = Type::getInt16Ty(ctx);
  u32 = Type::getInt32Ty(ctx);
  u64 = Type::getInt64Ty(ctx);
  voidptr = PointerType::getUnqual(u8);

  std::vector<Type *> formals;
  fv2v = FunctionType::get(voidty, formals, false);

  const HeapAssignment &asgn = getHeapAssignment();

  DEBUG(errs() << "SpecPriv ApplySeparationSpec: Processing parallel region, consisting of:\n");

  DEBUG(errs() << asgn);

  // Identify loops we will parallelize
  const Selector &selector = getSelector();
  for(Selector::strat_iterator i=selector.strat_begin(), e=selector.strat_end(); i!=e; ++i)
  {
    const BasicBlock *header = i->first;
    Function *fcn = const_cast< Function *>( header->getParent() );

    LoopInfo &li = mloops.getAnalysis_LoopInfo(fcn);
    Loop *loop = li.getLoopFor(header);
    assert( loop->getHeader() == header );

    loops.push_back(loop);

    DEBUG(errs() << " - loop " << fcn->getName() << " :: " << header->getName() << "\n");
  }
}


bool ApplySeparationSpec::runOnLoop(Loop *loop)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Run loop simplify first!");

  bool modified = false;

  modified |= manageMemOps(loop);

  return modified;
}

bool ApplySeparationSpec::manageMemOps(Loop *loop)
{
  // Instrument the RoI:
  bool modified = false;

  /* This made sense when we were trying to make
   * everything DOALL.
   *
  // Turn all IO calls into __specpriv_*() IO calls.
  modified |= deferIO();
  */

  // Insert UO checks
  modified |= addUOChecks(loop);

  // Add checks to private loads/stores
  if( ! DontCheckPrivacy )
    modified |= replacePrivateLoadsStores(loop);

  // Report footprint of reduction operators
  modified |= replaceReduxStores(loop);

  return modified;
}

bool ApplySeparationSpec::replaceFrees()
{
  Function *free = mod->getFunction("free");
  if( !free )
    return false;

  bool modified = false;

  Api api(mod);
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();

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

    BasicBlock *callbb = callinst->getParent();
    Function *callfcn = callbb->getParent();
    const Ctx *ctx = spresults.getCtx(callfcn);

    Value *ptr = cs.getArgument(0);

    HeapAssignment::Type heap = selectHeap(ptr, ctx);
    Constant *replacement = api.getFree( heap );

    callinst->replaceUsesOfWith( free, replacement );
    modified = true;

    // TODO: what about local objects in the recovery function!?
  }

  return modified;
}


bool ApplySeparationSpec::manageHeaps()
{
  bool modified = false;

  // initialization and finalization already applied in preprocess
  // Make a global constructor function
  modified |= initFiniFcns();
  //modified |= startInitializationFunction();
  //modified |= startFinalizationFunction();

  // Reallocate global variables in the appropriate heaps
  modified |= reallocateStaticAUs();

  // Replace calls to free.
  modified |= replaceFrees();

  // Reallocate stack variables, heap allocations
  modified |= reallocateDynamicAUs();

  //modified |= finishInitializationFunction();
  //modified |= finishFinalizationFunction();

  return modified;
}

bool ApplySeparationSpec::isPrivate(Loop *loop, Value *ptr)
{
  return selectHeap(ptr,loop) == HeapAssignment::Private;
}
bool ApplySeparationSpec::isRedux(Loop *loop, Value *ptr)
{
  return selectHeap(ptr,loop) == HeapAssignment::Redux;
}


void ApplySeparationSpec::insertPrivateWrite(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz)
{
  ++numPrivWrite;

  Preprocess &preprocess = getAnalysis< Preprocess >();

  // Maybe cast to void*
  Value *base = ptr;
  if( base->getType() != voidptr )
  {
    Instruction *cast = new BitCastInst(ptr, voidptr);
    where << cast;
    preprocess.addToLPS(cast, gravity);
    base = cast;
  }

  // Maybe cast the length
  Value *len = sz;
  if( len->getType() != u32 )
  {
    Instruction *cast = new TruncInst(len,u32);
    where << cast;
    preprocess.addToLPS(cast, gravity);
    len = cast;
  }

  Constant *writerange = Api(mod).getPrivateWriteRange();
  Value *actuals[] = { base, len };
  Instruction *validation = CallInst::Create(writerange, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
  where << validation;
  preprocess.addToLPS(validation, gravity);
}
void ApplySeparationSpec::insertReduxWrite(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz)
{
  ++numReduxWrite;

  Preprocess &preprocess = getAnalysis< Preprocess >();

  // Maybe cast to void*
  Value *base = ptr;
  if( base->getType() != voidptr )
  {
    Instruction *cast = new BitCastInst(ptr, voidptr);
    where << cast;
    preprocess.addToLPS(cast,gravity);
    base = cast;
  }

  // Maybe cast the length
  Value *len = sz;
  if( len->getType() != u32 )
  {
    Instruction *cast = new TruncInst(len,u32);
    where << cast;
    preprocess.addToLPS(cast,gravity);
    len = cast;
  }

  Constant *writerange = Api(mod).getReduxWriteRange();
  Value *actuals[] = { base, len };
  Instruction *validation = CallInst::Create(writerange, ArrayRef<Value*>(&actuals[0], &actuals[2]) );
  where << validation;
  preprocess.addToLPS(validation, gravity);
}


void ApplySeparationSpec::insertPrivateRead(Instruction *gravity, InstInsertPt where, Value *ptr, Value *sz)
{
  ++numPrivRead;

  Preprocess &preprocess = getAnalysis< Preprocess >();

  // Name
  Twine msg = "Privacy violation on pointer " + ptr->getName()
                  + " in " + where.getFunction()->getName()
                  + " :: " + where.getBlock()->getName();
  Constant *message = getStringLiteralExpression(*mod, msg.str());

  // Maybe cast to void*
  Value *base = ptr;
  if( base->getType() != voidptr )
  {
    Instruction *cast = new BitCastInst(ptr, voidptr);
    where << cast;
    preprocess.addToLPS(cast, gravity);
    base = cast;
  }

  // Maybe cast the length
  Value *len = sz;
  if( len->getType() != u32 )
  {
    Instruction *cast = new TruncInst(len,u32);
    where << cast;
    preprocess.addToLPS(cast, gravity);
    len = cast;
  }

  Constant *readrange = Api(mod).getPrivateReadRange();
  Value *actuals[] = { base, len, message };
  Instruction *validation = CallInst::Create(readrange, ArrayRef<Value*>(&actuals[0], &actuals[3]) );

  where << validation;
  preprocess.addToLPS(validation, gravity);
}

bool ApplySeparationSpec::replacePrivateLoadsStores(Loop *loop, BasicBlock *bb)
{
  bool modified = false;
  for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
  {
    Instruction *inst = &*i;

    if( LoadInst *load = dyn_cast< LoadInst >(inst) )
    {
      Value *ptr = load->getPointerOperand();

      if( !isPrivate(loop,ptr) )
        continue;

      const GlobalVariable *gv = dyn_cast<GlobalVariable>(ptr);
      if (gv && gv->hasExternalLinkage()) {
        // do not instrument externally defined objects such as stdout or stderr
        continue;
      }

      DEBUG(errs() << "Instrumenting private load: " << *load << '\n');

      //DataLayout &td = getAnalysis< DataLayout >();
      const DataLayout &td = bb->getParent()->getParent()->getDataLayout();
      PointerType *pty = cast< PointerType >( ptr->getType() );
      Type *eltty = pty->getElementType();
      uint64_t size = td.getTypeStoreSize(eltty);
      Value *sz = ConstantInt::get(u32,size);

      insertPrivateRead(load, InstInsertPt::Before(load), ptr, sz);
      modified = true;
    }
    else if( StoreInst *store = dyn_cast< StoreInst >(inst) )
    {
      Value *ptr = store->getPointerOperand();

      if( !isPrivate(loop,ptr) )
        continue;

      const GlobalVariable *gv = dyn_cast<GlobalVariable>(ptr);
      if (gv && gv->hasExternalLinkage()) {
        // do not instrument externally defined objects such as stdout or stderr
        continue;
      }

      DEBUG(errs() << "Instrumenting private store: " << *store << '\n');

      const DataLayout &td = bb->getParent()->getParent()->getDataLayout();
      //DataLayout &td = getAnalysis< DataLayout >();
      PointerType *pty = cast< PointerType >( ptr->getType() );
      Type *eltty = pty->getElementType();
      uint64_t size = td.getTypeStoreSize(eltty);
      Value *sz = ConstantInt::get(u32,size);

      insertPrivateWrite(store, InstInsertPt::Before(store), ptr, sz);
      modified = true;
    }
    else if( MemTransferInst *mti = dyn_cast< MemTransferInst >(inst) )
    {
      Value *src = mti->getRawSource(),
            *dst = mti->getRawDest(),
            *sz  = mti->getLength();

      bool psrc = isPrivate(loop,src),
           pdst = isPrivate(loop,dst);

      if( psrc )
      {
        DEBUG(errs() << "Instrumenting private source of mti: " << *mti << '\n');

        insertPrivateRead(mti, InstInsertPt::Before(mti), src, sz );
        modified = true;
      }

      if( pdst )
      {
        DEBUG(errs() << "Instrumenting private dest of mti: " << *mti << '\n');

        insertPrivateWrite(mti, InstInsertPt::Before(mti), dst, sz );
        modified = true;
      }
    }
    else if( MemSetInst *msi = dyn_cast< MemSetInst >(inst) )
    {
      Value *ptr = msi->getRawDest(),
            *sz  = msi->getLength();

      if( !isPrivate(loop,ptr) )
        continue;

      DEBUG(errs() << "Instrumenting private dest of memset: " << *msi << '\n');

      insertPrivateWrite(msi, InstInsertPt::Before(msi), ptr, sz );
      modified = true;
    }
  }

  return modified;
}
bool ApplySeparationSpec::replaceReduxStores(Loop *loop, BasicBlock *bb)
{
  bool modified = false;
  for(BasicBlock::iterator i=bb->begin(), e=bb->end(); i!=e; ++i)
  {
    Instruction *inst = &*i;

    if( StoreInst *store = dyn_cast< StoreInst >(inst) )
    {
      Value *ptr = store->getPointerOperand();

      if( !isRedux(loop,ptr) )
        continue;

      DEBUG(errs() << "Instrumenting redux store: " << *store << '\n');

      //DataLayout &td = getAnalysis< DataLayout >();
      const DataLayout &td = bb->getParent()->getParent()->getDataLayout();
      PointerType *pty = cast< PointerType >( ptr->getType() );
      Type *eltty = pty->getElementType();
      uint64_t size = td.getTypeStoreSize(eltty);
      Value *sz = ConstantInt::get(u32,size);

      insertReduxWrite(store, InstInsertPt::Before(store), ptr, sz);
      modified = true;
    }
  }

  return modified;
}

bool ApplySeparationSpec::replacePrivateLoadsStores(Loop *loop)
{
  bool modified = false;
  Preprocess &preprocess = getAnalysis< Preprocess >();
  const RoI &roi = preprocess.getRoI();
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
    modified |= replacePrivateLoadsStores(loop,*i);

  return modified;
}
bool ApplySeparationSpec::replaceReduxStores(Loop *loop)
{
  bool modified = false;
  Preprocess &preprocess = getAnalysis< Preprocess >();
  const RoI &roi = preprocess.getRoI();
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
    modified |= replaceReduxStores(loop,*i);

  return modified;
}

bool ApplySeparationSpec::initFiniFcns()
{
  Preprocess &preprocess = getAnalysis< Preprocess >();
  initFcn = preprocess.getInitFcn();
  finiFcn = preprocess.getFiniFcn();
}

bool ApplySeparationSpec::startInitializationFunction()
{
  // OUTSIDE of parallel region
  Function *init = Function::Create(fv2v, GlobalValue::InternalLinkage, "__specpriv_startup", mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *entry = BasicBlock::Create(ctx, "entry", init);

  initFcn = InstInsertPt::End(entry);

  Constant *beginfcn = Api(mod).getBegin();
  initFcn << CallInst::Create(beginfcn);

  if( DontCheckPrivacy )
  {
    Constant *enablePrivate = Api(mod).getEnablePrivate();
    initFcn << CallInst::Create(enablePrivate, ConstantInt::get(u32, 0));
  }

  return true;
}

bool ApplySeparationSpec::startFinalizationFunction()
{
  // OUTSIDE of parallel region
  Function *fini = Function::Create(fv2v, GlobalValue::InternalLinkage, "__specpriv_shutdown", mod);
  LLVMContext &ctx = mod->getContext();
  BasicBlock *entry = BasicBlock::Create(ctx, "entry", fini);

  finiFcn = InstInsertPt::End(entry);
  return true;
}

bool ApplySeparationSpec::finishInitializationFunction()
{
  // OUTSIDE of parallel region
  LLVMContext &ctx = mod->getContext();
  initFcn << ReturnInst::Create(ctx);

  callBeforeMain( initFcn.getFunction() );
  return true;
}

bool ApplySeparationSpec::finishFinalizationFunction()
{
  // OUTSIDE of parallel region
  Constant *endfcn = Api(mod).getEnd();
  finiFcn << CallInst::Create(endfcn);

  LLVMContext &ctx = mod->getContext();
  finiFcn << ReturnInst::Create(ctx);

  callAfterMain( finiFcn.getFunction() );
  return true;
}

void ApplySeparationSpec::insertMemcpy(InstInsertPt &where, Value *dst, Value *src, Value *sz)
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

struct ReplaceConstant2PreprocessAdaptor : ReplaceConstantObserver
{
  ReplaceConstant2PreprocessAdaptor(Preprocess &prep) : preprocess(prep) {}

  virtual void addInstruction(Instruction *newInst, Instruction *gravity)
  {
    preprocess.addToLPS(newInst,gravity);
  }

private:
  Preprocess &preprocess;
};


bool ApplySeparationSpec::reallocateGlobals(const HeapAssignment &asgn, const HeapAssignment::AUSet &aus, const HeapAssignment::Type heap)
{
  // OUTSIDE of parallel region
  const DataLayout &td = mod->getDataLayout();

  Preprocess &preprocess = getAnalysis< Preprocess >();
  ReplaceConstant2PreprocessAdaptor adaptor(preprocess);

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

    //if (gv->isExternallyInitialized()) {
    if (gv->hasExternalLinkage()) {
      // do not realllocate externally defined objects such as stdout or stderr
      continue;
    }

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
    Constant *sz = ConstantInt::get(u32,size);

    // sub-heap
    Constant *subheap = ConstantInt::get(u8, asgn.getSubHeap(au) );

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

bool ApplySeparationSpec::reallocateGlobals(const HeapAssignment &asgn, const HeapAssignment::ReduxAUSet &aus)
{
  // OUTSIDE of parallel region
  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = mod->getDataLayout();

  Preprocess &preprocess = getAnalysis< Preprocess >();
  ReplaceConstant2PreprocessAdaptor adaptor(preprocess);

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
    Constant *sz = ConstantInt::get(u32,size);

    Constant *subheap = ConstantInt::get(u8, asgn.getSubHeap(au) );

    Constant *alloc = Api(mod).getAlloc( HeapAssignment::Redux );
    Value *actuals[] = { sz, subheap, ConstantInt::get(u8, redty) };
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

bool ApplySeparationSpec::reallocateStaticAUs()
{
  // OUTSIDE of parallel region
  bool modified = false;

  const HeapAssignment &asgn = getHeapAssignment();

  modified |= reallocateGlobals(asgn, asgn.getSharedAUs(),   HeapAssignment::Shared );
  modified |= reallocateGlobals(asgn, asgn.getLocalAUs(),    HeapAssignment::Local );
  modified |= reallocateGlobals(asgn, asgn.getPrivateAUs(),  HeapAssignment::Private );
  modified |= reallocateGlobals(asgn, asgn.getReadOnlyAUs(), HeapAssignment::ReadOnly );

  modified |= reallocateGlobals(asgn, asgn.getReductionAUs());

  return modified;
}

Value *ApplySeparationSpec::determineSize(Instruction *gravity, InstInsertPt &where, Instruction *inst)
{
  Preprocess &preprocess = getAnalysis< Preprocess >();
  CallSite cs = getCallSite(inst);

  if( AllocaInst *alloca = dyn_cast< AllocaInst >(inst) )
  {
    //DataLayout &td = getAnalysis< DataLayout >();
    const DataLayout &td = inst->getParent()->getParent()->getParent()->getDataLayout();
    uint64_t size = td.getTypeStoreSize( alloca->getAllocatedType() );
    return ConstantInt::get(u32,size);
  }

  else if( Indeterminate::isMalloc(cs) )
  {
    Value *sz = cs.getArgument(0);
    if( sz->getType() != u32 )
    {
      Instruction *cast = new TruncInst(sz,u32);
      where << cast;
      preprocess.addToLPS(cast, gravity);
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
    preprocess.addToLPS(mul, gravity);

    Value *sz = mul;
    if( sz->getType() != u32 )
    {
      Instruction *cast = new TruncInst(sz,u32);
      where << cast;
      preprocess.addToLPS(cast, gravity);
      sz = cast;
    }

    return sz;
  }

  else if( Indeterminate::isRealloc(cs) )
  {
    Value *sz = cs.getArgument(1);
    if( sz->getType() != u32 )
    {
      Instruction *cast = new TruncInst(sz,u32);
      where << cast;
      preprocess.addToLPS(cast, gravity);
      sz = cast;
    }
    return sz;
  }


  errs() << *inst;
  assert(false && "Wtf");
}

bool ApplySeparationSpec::reallocateInst(const HeapAssignment &asgn, const HeapAssignment::AUSet &aus, const HeapAssignment::Type heap)
{
  bool modified = false;

  Preprocess &preprocess = getAnalysis< Preprocess >();
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
    Instruction *inst = const_cast< Instruction* >( dyn_cast< Instruction >( au->value ) );
    assert( inst );


    DEBUG(errs() << "Dynamic AU: " << *inst << " ==> heap " << Api::getNameForHeap( heap ) << '\n');

    Function *fcn = inst->getParent()->getParent();
    InstInsertPt where = InstInsertPt::After(inst);

    // Determine size of allocation
    Value *sz = determineSize(inst, where, inst);

    Constant *subheap = ConstantInt::get(u8, asgn.getSubHeap(au));
    Value *actuals[] = {sz, subheap};

    // Add code to perform allocation
    Constant *alloc = Api(mod).getAlloc( heap );
    Instruction *allocate = CallInst::Create(alloc, ArrayRef<Value*>(&actuals[0], &actuals[2]));
    where << allocate;
    preprocess.replaceInLPS(allocate, inst);
    Value *newAU = allocate;

    if( newAU->getType() != inst->getType() )
    {
      Instruction *cast = new BitCastInst(newAU, inst->getType());
      where << cast;
      preprocess.addToLPS(cast, allocate);
      newAU = cast;
    }

    // Replace old allocation
    newAU->takeName(inst);
    inst->replaceAllUsesWith(newAU);
    preprocess.getRecovery().replaceAllUsesOfWith(inst,newAU);
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
bool ApplySeparationSpec::reallocateInst(const HeapAssignment &asgn, const HeapAssignment::ReduxAUSet &aus, const HeapAssignment::ReduxDepAUSet depAUs)
{
  bool modified = false;

  Preprocess &preprocess = getAnalysis< Preprocess >();

  std::set<const Value *> already;
  std::map<AU *, Value *> newAUs;
  std::map<AU *, Value *> ausSize;
  std::map<AU *, Value *> ausType;
  bool notDone = true;
  // iterate over all reduxAUs until all dependent redux objects, if any, are
  // processed
  while (notDone) {
    notDone = false; // if no dependent redux then only one invocation needed
    for (HeapAssignment::ReduxAUSet::const_iterator i = aus.begin(),
                                                    e = aus.end();
         i != e; ++i) {
      AU *au = i->first;
      Reduction::Type redty = i->second;

      if (au->type != AU_Stack && au->type != AU_Heap)
        continue;

      if (already.count(au->value))
        continue;

      // if there is a redux that 'i' redux depends on, make sure it is already
      // available, if not, process it first
      Value *depSz;      // defaults to 0
      Value *depAllocAU; // defaults to null
      Value *depType;
      depSz = ConstantInt::get(u32, 0);
      Instruction *I =
          const_cast<Instruction *>(dyn_cast<Instruction>(au->value));
      assert(I);
      LLVMContext &ctx = I->getModule()->getContext();
      PointerType *voidptr = PointerType::getUnqual(Type::getInt8Ty(ctx));
      depAllocAU = ConstantPointerNull::get(voidptr);
      depType = ConstantInt::get(u8, 0);

      auto f = depAUs.find(au);
      if (f != depAUs.end()) {
        AU *depAU = f->second.depAU;
        if (!newAUs.count(depAU)) {
          au = depAU;
          redty = f->second.depType;
          notDone = true; // process this in the next loop invocation
        } else {
          depSz = ausSize[depAU];
          depAllocAU = newAUs[depAU];
          depType = ausType[depAU];
        }
      }

      if (au->type != AU_Stack && au->type != AU_Heap)
        continue;

      if (already.count(au->value))
        continue;

      already.insert(au->value);
      Instruction *inst =
          const_cast<Instruction *>(dyn_cast<Instruction>(au->value));
      assert(inst);

      DEBUG(errs() << "Dynamic AU: " << *inst << " ==> heap redux\n");

      Function *fcn = inst->getParent()->getParent();
      InstInsertPt where = InstInsertPt::After(inst);
      if (depAUs.find(au) != depAUs.end()) {
        Instruction *depAllocAUI = dyn_cast<Instruction>(depAllocAU);
        assert(depAllocAUI);
        where = InstInsertPt::After(depAllocAUI);
      }

      // Determine size of allocation
      Value *sz = determineSize(inst, where, inst);
      Constant *subheap = ConstantInt::get(u8, asgn.getSubHeap(au));

      // Add code to perform allocation
      Constant *alloc = Api(mod).getAlloc(HeapAssignment::Redux);
      Value *actuals[] = {sz,         subheap, ConstantInt::get(u8, redty),
                          depAllocAU, depSz,   depType};
      Instruction *allocate =
          CallInst::Create(alloc, ArrayRef<Value *>(&actuals[0], &actuals[6]));
      where << allocate;
      Value *newAU = allocate;

      newAUs[au] = newAU;
      ausSize[au] = sz;
      PointerType *pty = dyn_cast<PointerType>(inst->getType());
      assert(pty && "Alloc inst not a pointer?!");
      if (pty->getElementType()->isPointerTy())
        ausType[au] = ConstantInt::get(u8, 0);
      else if (pty->getElementType()->isIntegerTy())
        ausType[au] = ConstantInt::get(u8, 1);
      else if (pty->getElementType()->isFloatingPointTy())
        ausType[au] = ConstantInt::get(u8, 2);
      else
        assert(0 && "Not yet implemented");

      if (newAU->getType() != inst->getType()) {
        Instruction *cast = new BitCastInst(newAU, inst->getType());
        where << cast;
        newAU = cast;
      }

      // Replace old allocation
      newAU->takeName(inst);
      inst->replaceAllUsesWith(newAU);
      preprocess.getRecovery().replaceAllUsesOfWith(inst, newAU);
      inst->eraseFromParent();

      // Manually free stack variables
      if (au->type == AU_Stack) {
        // At each function exit (return, unwind, or unreachable...)
        for (Function::iterator j = fcn->begin(), z = fcn->end(); j != z; ++j) {
          BasicBlock *bb = &*j;
          TerminatorInst *term = bb->getTerminator();
          InstInsertPt where;
          if (isa<ReturnInst>(term))
            where = InstInsertPt::Before(term);

          else if (isa<UnreachableInst>(term)) {
            where = InstInsertPt::Before(term);

            // This unreachable terminator is probably prededed by
            // a call to a noreturn function...
            for (BasicBlock::iterator k = bb->begin(); k != bb->end(); ++k) {
              CallSite cs = getCallSite(&*k);
              if (!cs.getInstruction())
                continue;

              if (cs.doesNotReturn()) {
                where = InstInsertPt::Before(cs.getInstruction());
                break;
              }
            }
          }

          else
            continue;

          // Free the allocation
          Constant *free = Api(mod).getFree(HeapAssignment::Redux);
          where << CallInst::Create(free, allocate);
        }
      }
    }

    ++numDynReloc;
    modified = true;
  }

  return modified;
}

bool ApplySeparationSpec::reallocateDynamicAUs()
{
  bool modified = false;

  const HeapAssignment &asgn = getHeapAssignment();

  modified |= reallocateInst(asgn, asgn.getSharedAUs(),   HeapAssignment::Shared );
  modified |= reallocateInst(asgn, asgn.getLocalAUs(),    HeapAssignment::Local );
  modified |= reallocateInst(asgn, asgn.getPrivateAUs(),  HeapAssignment::Private );
  modified |= reallocateInst(asgn, asgn.getReadOnlyAUs(), HeapAssignment::ReadOnly );

  modified |= reallocateInst(asgn, asgn.getReductionAUs(), asgn.getReduxDepAUs());

  return modified;
}

bool ApplySeparationSpec::addUOChecks(Loop *loop)
{
  bool modified = false;

  VSet alreadyInstrumented;

  Preprocess &preprocess = getAnalysis< Preprocess >();

  if (!preprocess.isSeparationSpecUsed(loop->getHeader()))
    return modified;

  const RoI &roi = preprocess.getRoI();

  const HeapAssignment &asgn = getHeapAssignment();

  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
    modified |= addUOChecks(asgn, loop,*i,alreadyInstrumented);

  return modified;
}

bool ApplySeparationSpec::addUOChecks(const HeapAssignment &asgn, Loop *loop, BasicBlock *bb, VSet &alreadyInstrumented)
{
  UO objectsToInstrument;
  Indeterminate::findIndeterminateObjects(*bb, objectsToInstrument);

  if( objectsToInstrument.empty() )
    return false;

//    DEBUG(errs() << "RoI contains " << objectsToInstrument.size() << " indeterminate objects.\n");

  // Okay, we have a set of UOs.  Let's instrument them!
  bool modified = false;

  for(UO::iterator i=objectsToInstrument.begin(), e=objectsToInstrument.end(); i!=e; ++i)
  {
    const Value *obj = *i;

    const GlobalVariable *cgv = dyn_cast< GlobalVariable >( obj );
    if (cgv && cgv->hasExternalLinkage()) {
      // do not check externally defined objects such as stdout or stderr
      continue;
    }

    if( alreadyInstrumented.count(obj) )
      continue;
    alreadyInstrumented.insert(obj);

    // What heap do we expect to find this object in?
    HeapAssignment::Type ty = selectHeap(obj,loop);
    if( ty == HeapAssignment::Unclassified )
    {
      DEBUG(errs() << "Cannot check " << *obj << "!!!\n");
      continue;
    }

    Value *cobj = const_cast< Value* >(obj);
    insertUOCheck(asgn, loop,cobj,ty);

    modified = true;
  }

  return modified;
}


void ApplySeparationSpec::insertUOCheck(const HeapAssignment &asgn, Loop *loop, Value *obj, HeapAssignment::Type heap)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Loop simplify!?");

  Preprocess &preprocess = getAnalysis< Preprocess >();

  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
  int sh = asgn.getSubHeap(obj, loop, spresults);

  // Where should we insert this check?
  InstInsertPt where;
  Instruction *inst_obj = dyn_cast< Instruction >(obj);
  if( inst_obj )
  {
//    if( roi.bbs.count(inst_obj->getParent()) )
      where = InstInsertPt::After(inst_obj);
//    else
//      where = InstInsertPt::End( preheader ); // TODO: assumes single parallel region
  }
  else if( Argument *arg = dyn_cast< Argument >(obj) )
  {
    Function *f = arg->getParent();
//    if( f == preheader->getParent() )
//      where = InstInsertPt::End( preheader ); // TODO: assumes single parallel region
//    else
      where = InstInsertPt::Beginning( f );
  }

  Twine msg = "UO violation on pointer " + obj->getName()
                  + " in " + where.getFunction()->getName()
                  + " :: " + where.getBlock()->getName()
                  + "; should be in " + Api::getNameForHeap(heap)
                  + ", sub-heap " + Twine(sh);
  Constant *message = getStringLiteralExpression(*mod, msg.str());

  Instruction *cast = new BitCastInst(obj,voidptr);

  Constant *check = Api(mod).getUO();

  Value *code = ConstantInt::get(u8, (int) heap );
  Constant *subheap = ConstantInt::get(u8, sh);

  Value *args[] = { cast, code, subheap, message };
  Instruction *call = CallInst::Create(check, ArrayRef<Value*>(&args[0], &args[4]) );

  where << cast << call;
  preprocess.addToLPS(cast, inst_obj);
  preprocess.addToLPS(call, inst_obj);

  ++numUOTests;
  DEBUG(errs() << "Instrumented indet obj: " << *obj << '\n');
}

// TODO This, as well as LocalityAA::fold(), should be merged into a single
// method of HeapAssignment.
HeapAssignment::Type ApplySeparationSpec::selectHeap(const Value *ptr, const Loop *loop) const
{
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
  const Ctx *ctx = spresults.getCtx(loop);

  return selectHeap(ptr,ctx);
}

HeapAssignment::Type ApplySeparationSpec::selectHeap(const Value *ptr, const Ctx *ctx) const
{
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();

  Ptrs aus;
  if( !spresults.getUnderlyingAUs(ptr,ctx,aus) )
    return HeapAssignment::Unclassified;

  const HeapAssignment &asgn = getHeapAssignment();

  return asgn.classify(aus);
}

char ApplySeparationSpec::ID = 0;
static RegisterPass<ApplySeparationSpec> x("spec-priv-apply-separation-spec",
  "Apply Separation Speculation to RoI for SpecPriv: fix allocation, UO tests, and privacy");
}
}

