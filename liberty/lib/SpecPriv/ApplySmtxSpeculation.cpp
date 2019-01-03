#define DEBUG_TYPE "apply-smtx-speculation"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"

#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/Indeterminate.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/Read.h"
#include "liberty/SpecPriv/Reduction.h"
#include "liberty/SpecPriv/Selector.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/CastUtil.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/ReplaceConstantWithLoad.h"
#include "liberty/Utilities/SplitEdge.h"
#include "liberty/Utilities/Timer.h"

#include "Api.h"
#include "Classify.h"
#include "Discriminator.h"
#include "SmtxManager.h"
#include "Preprocess.h"
#include "ApplySmtxSpeculation.h"
#include "Recovery.h"
#include "PtrResidueManager.h"
#include "RoI.h"

#include <set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

STATISTIC(numSmtxWrite,  "SMTX write checks inserted");
STATISTIC(numSmtxRead,   "SMTX read checks inserted");

#if DEBUG_MEMORY_FOOTPRINT
static bool calls(const CallSite &cs, StringRef fcnname)
{
  if( !cs.getInstruction() )
    return false;
  const Function *fcn = cs.getCalledFunction();
  if( !fcn )
    return false;
  return fcn->getName() == fcnname;
}

static bool isMemallocOp(Instruction* inst)
{
  if ( isa<AllocaInst>( inst ) )
    return true;

  CallSite cs = getCallSite(inst);

  if ( Indeterminate::isMallocOrCalloc(cs) || Indeterminate::isRealloc(cs) ||
       Indeterminate::isFree(cs) )
    return true;

  return false;
}
#endif

void ApplySmtxSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< ModuleLoops >();
  au.addRequired< SmtxSpeculationManager >();
  au.addRequired< ReadPass >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();

  au.addPreserved< ModuleLoops >();
  au.addPreserved< ReadPass >();
  au.addPreserved< SmtxSpeculationManager >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  au.addPreserved< PtrResidueSpeculationManager >();
  au.addPreserved< Selector >();
  au.addPreserved< Preprocess >();
}


bool ApplySmtxSpec::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplySmtxSpec\n\n\n");
  mod = &module;
  api = new Api(mod);
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Preprocess &preprocess = getAnalysis< Preprocess >();
  init(mloops);

  if( loops.empty() )
    return false;

  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Collect list of all memory ops before we start modifying anything.
  std::vector<Instruction*> all_mem_ops;
  const RoI &roi = preprocess.getRoI();
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      if( inst->mayReadOrWriteMemory() )
        all_mem_ops.push_back( inst );
    }
  }

  // Collect list of all memory allocation ops before we start modifying anything.
  std::vector<Instruction*> all_memalloc_ops;
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      if( isMemallocOp(inst) )
        all_memalloc_ops.push_back( inst );
    }
  }

#if DEBUG_BASICBLOCK_TRACE
  Constant* debugprintf = api->getDebugPrintf();
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    std::string namestr = bb->getParent()->getName().str()+"::"+bb->getName().str();
    Constant* name = getStringLiteralExpression( *mod, ">>> BasicBlock "+namestr+"\n" );
    Value* args[] = { name };
    CallInst* ci = CallInst::Create(debugprintf, args, "debug-ctrl", bb->getTerminator());
    preprocess.addToLPS(ci, bb->getTerminator());
  }
#endif

  // Perform per-loop preprocessing.

  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "SpecPriv ApplySmtxSpec: Processing loop "
      << fcn->getName() << " :: " << header->getName() << "\n");

    modified |= runOnLoop(loop, all_mem_ops);
  }

  // Instrument memory alloccation related calls

  for(unsigned i=0; i<loops.size(); ++i)
    modified |= addSmtxMemallocs(loops[i], all_memalloc_ops);

  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    for(RoI::FSet::iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    preprocess.assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  return modified;
}

void ApplySmtxSpec::init(ModuleLoops &mloops)
{
  LLVMContext &ctx = mod->getContext();

  voidty = Type::getVoidTy(ctx);
  IntegerType *u8 = Type::getInt8Ty(ctx);
  u16 = Type::getInt16Ty(ctx);
  voidptr = PointerType::getUnqual(u8);

  DEBUG(errs() << "SpecPriv ApplySmtxSpec: Processing parallel region, consisting of:\n");

  // Identify loops we will parallelize
  const Selector &selector = getAnalysis< Selector >();
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

bool ApplySmtxSpec::runOnLoop(Loop *loop, std::vector<Instruction*> &all_mem_ops)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Run loop simplify first!");

  bool modified = false;

  Selector &selector = getAnalysis< Selector >();
  LoopParallelizationStrategy *lps = &selector.getStrategy(loop);
  if( PipelineStrategy *pipeline = dyn_cast<PipelineStrategy>(lps) )
  {
    SmtxSpeculationManager &manager = getAnalysis< SmtxSpeculationManager >();
    manager.unspeculate(loop, *pipeline);
  }

  modified |= addSmtxChecks(loop, all_mem_ops);

  return modified;
}

bool ApplySmtxSpec::addSmtxChecks(Loop *loop, std::vector<Instruction*> &all_mem_ops)
{
  bool modified = false;

  //DataLayout &td = getAnalysis< DataLayout >();
  const DataLayout &td = mod->getDataLayout();
  Preprocess &preprocess = getAnalysis< Preprocess >();
  SmtxSpeculationManager &manager = getAnalysis< SmtxSpeculationManager >();

  // Basically, we want to apply ver_write
  // to every write, and apply ver_read to every speculative read.
  Constant *ver_write = api->getVerWrite(),
           *ver_read  = api->getVerRead(),
           *ver_memmove = api->getVerMemMove();

#if DEBUG_MEMORY_FOOTPRINT
  Constant *debug = api->getDebugPrintf();
#endif

  Type *u32 = api->getU32();
  Type *u64 = api->getU64();

  NewInstructions newInstructions;

  for(unsigned i=0, N=all_mem_ops.size(); i<N; ++i)
  {
    Instruction *inst = all_mem_ops[i];
    if( !inst )
      continue;

    // memcpy, memmove are different than load, store or memset in that
    // the value loaded/stored doesn't fit a uint64_t; we have
    // a separate call ver_memmove.
    if( MemTransferInst *memmove = dyn_cast<MemTransferInst>(inst) )
    {
      Value *write_ptr = memmove->getRawDest();
      Value *read_ptr = memmove->getRawSource();
      Value *write_size = memmove->getLength();

      InstInsertPt where = InstInsertPt::Before(inst);

      write_ptr = castPtrToVoidPtr(write_ptr, where, &newInstructions);
      write_size = castIntToInt32Ty(write_size, where, &newInstructions);
      read_ptr = castPtrToVoidPtr(read_ptr, where, &newInstructions);

      // Insert the call.
      llvm::SmallVector<Value*,3> actuals;
      actuals.push_back( write_ptr );
      actuals.push_back( write_size );
      actuals.push_back( read_ptr );
      CallInst *call = CallInst::Create(ver_memmove, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);

#if DEBUG_MEMORY_FOOTPRINT
      std::string fmt = "[memmove] from %p, to %p, size %u\n";
      Constant*   fmtarg = getStringLiteralExpression( *mod, fmt );

      actuals.clear();
      actuals.push_back( fmtarg );
      actuals.push_back( read_ptr );
      actuals.push_back( write_ptr );
      actuals.push_back( write_size );

      call = CallInst::Create(debug, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);
#endif

      for(unsigned j=0, N=newInstructions.size(); j<N; ++j)
        preprocess.addToLPS(newInstructions[j], inst);
      newInstructions.clear();

      all_mem_ops[i] = 0;
      modified = true;
      continue;
    }

    // Decode this instruction into (read, size) and/or (write, size)
    CallSite cs = getCallSite(inst);
    Instruction *write = 0, *read = 0;
    Value *write_ptr = 0, *read_ptr = 0;
    Value *write_size = 0, *read_size = 0;
    Value *write_value = 0, *read_value = 0;
    if( StoreInst *store = dyn_cast<StoreInst>(inst) )
    {
      write = store;
      write_ptr = store->getPointerOperand();
      write_value = store->getValueOperand();
      const unsigned sz = td.getTypeStoreSize( store->getValueOperand()->getType() );
      write_size = ConstantInt::get( u32, sz );
    }
    else if( LoadInst *load = dyn_cast<LoadInst>(inst) )
    {
      read = load;
      read_ptr = load->getPointerOperand();
      read_value = load;
      const unsigned sz = td.getTypeStoreSize( load->getType() );
      read_size = ConstantInt::get( u32, sz );
    }
    else if( MemSetInst *memset = dyn_cast<MemSetInst>(inst) )
    {
      write = memset;
      write_ptr = memset->getRawDest();
      write_value = memset->getValue();
      write_size = memset->getLength();
    }
    else if( cs.getInstruction() && cs.getCalledFunction() && !cs.getCalledFunction()->isDeclaration() )
    {
      // An internally-defined function will be instrumented
      // separately; this instruction does not need checks.
      continue;
    }

    // All other cases
    else
    {
      errs() << "Cannot instrument: " << *inst << '\n';
      errs() << "- In basic block : " << inst->getParent()->getName() << '\n';
      errs() << "- In function    : " << inst->getParent()->getParent()->getName() << '\n';
//      assert(false && "Incomplete validation coverage");
    }

    // Instrument *all* writes
    if( write )
    {
      InstInsertPt where = InstInsertPt::After(write);

      write_ptr = castPtrToVoidPtr(write_ptr, where, &newInstructions);
      write_size = castIntToInt32Ty(write_size, where, &newInstructions);
      assert( write_value );
      if( !write_value )
        write_value = UndefValue::get( u64 );
      write_value = castToInt64Ty(write_value, where, &newInstructions);

      // Insert the call.
      llvm::SmallVector<Value*,3> actuals;
      actuals.push_back( write_ptr );
      actuals.push_back( write_value );
      actuals.push_back( write_size );
      CallInst *call = CallInst::Create(ver_write, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);

#if DEBUG_MEMORY_FOOTPRINT
      std::string fmt = "[write] ptr %p, value %lld, size %u\n";
      Constant*   fmtarg = getStringLiteralExpression( *mod, fmt );

      actuals.clear();
      actuals.push_back( fmtarg );
      actuals.push_back( write_ptr );
      actuals.push_back( write_value );
      actuals.push_back( write_size );

      call = CallInst::Create(debug, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);
#endif

      all_mem_ops[i] = 0;
      modified = true;
      ++numSmtxWrite;
    }

    // Instrument *speculative* reads
    if( read && manager.sinksSpeculativelyRemovedEdge(loop, read) )
    {
      InstInsertPt where = InstInsertPt::After(read);

      read_ptr = castPtrToVoidPtr(read_ptr, where, &newInstructions);
      read_size = castIntToInt32Ty(read_size, where, &newInstructions);
      if( !read_value )
        read_value = UndefValue::get( u64 );
      read_value = castToInt64Ty(read_value, where, &newInstructions);

      // Insert the call.
      llvm::SmallVector<Value*,3> actuals;
      actuals.push_back( read_ptr );
      actuals.push_back( read_value );
      actuals.push_back( read_size );
      CallInst *call = CallInst::Create(ver_read, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);

#if DEBUG_MEMORY_FOOTPRINT
      std::string fmt = "[read] ptr %p, value %lld, size %u\n";
      Constant*   fmtarg = getStringLiteralExpression( *mod, fmt );

      actuals.clear();
      actuals.push_back( fmtarg );
      actuals.push_back( read_ptr );
      actuals.push_back( read_value );
      actuals.push_back( read_size );

      call = CallInst::Create(debug, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);
#endif

      all_mem_ops[i] = 0;
      modified = true;
      ++numSmtxRead;
    }
#if DEBUG_MEMORY_FOOTPRINT
    else if ( read )
    {
      InstInsertPt where = InstInsertPt::After(read);

      read_ptr = castPtrToVoidPtr(read_ptr, where, &newInstructions);
      read_size = castIntToInt32Ty(read_size, where, &newInstructions);
      if( !read_value )
        read_value = UndefValue::get( u64 );
      read_value = castToInt64Ty(read_value, where, &newInstructions);

      std::string fmt = "[read] ptr %p, value %lld, size %u\n";
      Constant*   fmtarg = getStringLiteralExpression( *mod, fmt );

      llvm::SmallVector<Value*,4> actuals;
      actuals.push_back( fmtarg );
      actuals.push_back( read_ptr );
      actuals.push_back( read_value );
      actuals.push_back( read_size );

      CallInst *call = CallInst::Create(debug, ArrayRef<Value*>(actuals));
      where << call;
      newInstructions.push_back(call);
    }
#endif

    for(unsigned j=0, N=newInstructions.size(); j<N; ++j)
      preprocess.addToLPS(newInstructions[j], inst);
    newInstructions.clear();
  }

  return modified;
}

bool ApplySmtxSpec::addSmtxMemallocs(Loop *loop, std::vector<Instruction*> &all_memalloc_ops)
{
  bool modified = false;

  Preprocess &preprocess = getAnalysis< Preprocess >();

  Constant *debug = api->getDebugPrintf();

  NewInstructions newInstructions;

  for(unsigned i=0, N=all_memalloc_ops.size(); i<N; ++i)
  {
    Instruction *inst = all_memalloc_ops[i];
    if( !inst )
      continue;

    modified = true;

    InstInsertPt where;

    if ( InvokeInst* ivi = dyn_cast<InvokeInst>( inst ) )
      where = InstInsertPt::Before( ivi->getNormalDest()->getFirstNonPHI() );
    else
      where = InstInsertPt::After(inst);
    CallSite     cs = getCallSite(inst);

    Value    *sz = 0, *ptr = 0;
    Constant *fmtarg;

    if ( AllocaInst *alloca = dyn_cast<AllocaInst>( inst ) )
    {
      //DataLayout &td = getAnalysis< DataLayout >();
      const DataLayout &td = mod->getDataLayout();
      uint64_t size = td.getTypeStoreSize( alloca->getAllocatedType() );
      sz = ConstantInt::get( api->getU64(),size );

      llvm::SmallVector<Value*,1> args;
      args.push_back( sz );

      CallInst *alloc = CallInst::Create( api->getVerMalloc(), ArrayRef<Value*>(args) );
      where << alloc;
      newInstructions.push_back(alloc);

      BitCastInst *newalloca = new BitCastInst( alloc, inst->getType() );
      where << newalloca;
      newInstructions.push_back(newalloca);

      // replace alloca to malloc call

      newalloca->takeName(inst);
      inst->replaceAllUsesWith(newalloca);
      preprocess.getRecovery().replaceAllUsesOfWith(inst, newalloca);
      inst->eraseFromParent();

      // Manually free stack variables

      Function *fcn = newalloca->getParent()->getParent();
      for(Function::iterator j=fcn->begin(), z=fcn->end(); j!=z; ++j)
      {
        BasicBlock *bb = &*j;
        TerminatorInst *term = bb->getTerminator();
        InstInsertPt freept;
        if( isa<ReturnInst>(term) )
          freept = InstInsertPt::Before(term);

        else if( isa<UnreachableInst>(term) )
        {
          errs() << "Not yet implemented: handle unreachable correctly.\n"
            << " - If 'unreachable' is reached, some memory will not be freed.\n";
          continue;
        }

        else
          continue;

        // Free the allocation
        Constant *free = api->getVerFree();
        freept << CallInst::Create(free, alloc);
      }

#if DEBUG_MEMORY_FOOTPRINT
      ptr = castPtrToVoidPtr( alloc, where, &newInstructions );

      std::string fmt = "[alloca] ptr %p, size %u\n";
      fmtarg = getStringLiteralExpression( *mod, fmt );
#endif
    }
    else if ( Indeterminate::isMalloc(cs) )
    {
#if DEBUG_MEMORY_FOOTPRINT
      sz = castIntToInt32Ty( cs.getArgument(0), where, &newInstructions );
      ptr = castPtrToVoidPtr( inst, where, &newInstructions );

      std::string fmt = "[malloc] ptr %p, size %u\n";
      fmtarg = getStringLiteralExpression( *mod, fmt );
#endif

      cs.setCalledFunction( api->getVerMalloc() );
    }
    else if ( Indeterminate::isCalloc(cs) )
    {
#if DEBUG_MEMORY_FOOTPRINT
      Value *count = cs.getArgument(0);
      Value *size  = cs.getArgument(1);

      Instruction *mul = BinaryOperator::CreateNSWMul(count,size);
      where << mul;
      newInstructions.push_back(mul);

      sz = castIntToInt32Ty( mul, where, &newInstructions );
      ptr = castPtrToVoidPtr( inst, where, &newInstructions );

      std::string fmt = "[calloc] ptr %p, size %u\n";
      fmtarg = getStringLiteralExpression( *mod, fmt );
#endif

      cs.setCalledFunction( api->getVerCalloc() );
    }
    else if ( Indeterminate::isRealloc(cs) )
    {
#if DEBUG_MEMORY_FOOTPRINT
      sz = castIntToInt32Ty( cs.getArgument(1), where, &newInstructions );
      ptr = castPtrToVoidPtr( inst, where, &newInstructions );

      std::string fmt = "[realloc] ptr %p, size %u\n";
      fmtarg = getStringLiteralExpression( *mod, fmt );
#endif

      cs.setCalledFunction( api->getVerRealloc() );
    }
    else if ( Indeterminate::isFree(cs) )
    {
#if DEBUG_MEMORY_FOOTPRINT
      ptr = castPtrToVoidPtr( cs.getArgument(0), where, &newInstructions );

      std::string fmt = "[free] ptr %p\n";
      fmtarg = getStringLiteralExpression( *mod, fmt );
#endif

      cs.setCalledFunction( api->getVerFree() );
    }
    else
      assert( false && "Unsupported type of memalloc instruction" );

#if DEBUG_MEMORY_FOOTPRINT
    llvm::SmallVector<Value*,3> actuals;
    actuals.push_back( fmtarg );
    actuals.push_back( ptr );

    if ( sz )
      actuals.push_back( sz );

    CallInst *call = CallInst::Create(debug, ArrayRef<Value*>(actuals));
    where << call;
    newInstructions.push_back(call);
#endif

    for(unsigned j=0, N=newInstructions.size(); j<N; ++j)
      preprocess.addToLPS(newInstructions[j], inst);
    newInstructions.clear();
  }

  // now replace all uses of malloc/calloc/realloc/free outside of RoI

  Function* specpriv_malloc = cast<Function>( api->getMalloc() );
  Constant* std_malloc = mod->getOrInsertFunction( "malloc", specpriv_malloc->getFunctionType() );
  std_malloc->replaceAllUsesWith(specpriv_malloc);
  Constant* _znwm = mod->getOrInsertFunction( "_Znwm", specpriv_malloc->getFunctionType() );
  _znwm->replaceAllUsesWith(specpriv_malloc);
  Constant* _znam = mod->getOrInsertFunction( "_Znam", specpriv_malloc->getFunctionType() );
  _znam->replaceAllUsesWith(specpriv_malloc);

  Function* specpriv_calloc = cast<Function>( api->getCalloc() );
  Constant* std_calloc = mod->getOrInsertFunction( "calloc", specpriv_calloc->getFunctionType() );
  std_calloc->replaceAllUsesWith(specpriv_calloc);

  Function* specpriv_realloc = cast<Function>( api->getRealloc() );
  Constant* std_realloc = mod->getOrInsertFunction( "realloc", specpriv_realloc->getFunctionType() );
  std_realloc->replaceAllUsesWith(specpriv_realloc);

  Function* specpriv_free = cast<Function>( api->getFree() );
  Constant* std_free = mod->getOrInsertFunction( "free", specpriv_free->getFunctionType() );
  std_free->replaceAllUsesWith(specpriv_free);
  Constant* _zdlpv = mod->getOrInsertFunction( "_ZdlPv", specpriv_free->getFunctionType() );
  _zdlpv->replaceAllUsesWith(specpriv_free);
  Constant* _zdapv = mod->getOrInsertFunction( "_ZdaPv", specpriv_free->getFunctionType() );
  _zdapv->replaceAllUsesWith(specpriv_free);

  return modified;
}

char ApplySmtxSpec::ID = 0;
static RegisterPass<ApplySmtxSpec> x("spec-priv-apply-smtx-spec",
  "Apply SMTX speculation to RoI");
}
}

