#define DEBUG_TYPE "apply-smtx-slamp-speculation"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/InstIterator.h"

#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/Indeterminate.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/Read.h"
#include "liberty/SpecPriv/Reduction.h"
#include "liberty/SpecPriv/Selector.h"
#include "liberty/SpecPriv/SmtxSlampManager.h"
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
#include "Preprocess.h"
#include "ApplySmtxSlampSpeculation.h"
#include "Recovery.h"
#include "HeapProfileLoad.h"
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
#endif

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

void ApplySmtxSlampSpec::getAnalysisUsage(AnalysisUsage &au) const
{
  //au.addRequired< DataLayout >();
  au.addRequired< ModuleLoops >();
  au.addRequired< SmtxSlampSpeculationManager >();
  //au.addRequired< ReadPass >();
  au.addRequired< Selector >();
  au.addRequired< Preprocess >();
  au.addRequired< HeapProfileLoad >();

  au.addPreserved< ModuleLoops >();
  au.addPreserved< ReadPass >();
  au.addPreserved< SmtxSlampSpeculationManager >();
  au.addPreserved< HeapProfileLoad >();
  au.addPreserved< ProfileGuidedControlSpeculator >();
  au.addPreserved< ProfileGuidedPredictionSpeculator >();
  au.addPreserved< PtrResidueSpeculationManager >();
  au.addPreserved< Selector >();
  au.addPreserved< Preprocess >();
}


bool ApplySmtxSlampSpec::runOnModule(Module &module)
{
  DEBUG(errs() << "#################################################\n"
               << " ApplySmtxSlampSpec\n\n\n");
  mod = &module;
  api = new Api(mod);
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  Preprocess &preprocess = getAnalysis< Preprocess >();

  init(mloops);

  if( loops.empty() )
    return false;

  // create a constructor

  LLVMContext& ctxt = mod->getContext();
  //Function*    ctor = cast<Function>( mod->getOrInsertFunction("__apply_smtx_slamp_spec_constructor", voidty, (Type*)0) );
  Function*    ctor = cast<Function>( mod->getOrInsertFunction("__apply_smtx_slamp_spec_constructor", voidty) );
  BasicBlock*  ctorentry = BasicBlock::Create(ctxt, "entry", ctor, NULL);

  // call init function from the constructor
  CallInst::Create( api->getInit(), "", ctorentry );

  ReturnInst::Create(ctxt, ctorentry);

  callBeforeMain(ctor, 0); // super high priority constructor

  preprocess.assert_strategies_consistent_with_ir();
  bool modified = false;

  // Collect list of all memory ops before we start modifying anything.

  InstVec all_mem_ops;
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

  InstVec all_memalloc_ops;
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

  // Collect list of all memory ops that checking can be skipped

  std::set<const Instruction*> skippables;

  IntraProceduralOptimization(all_mem_ops, skippables);
  errs() << "after Intra, skippables: " << skippables.size() << "\n";
  InterProceduralOptimization(skippables);
  errs() << "after Inter, skippables: " << skippables.size() << "\n";

#if DEBUG_BASICBLOCK_TRACE
  Constant* debugprintf = api->getDebugPrintf();
  for(RoI::BBSet::iterator i=roi.bbs.begin(), e=roi.bbs.end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    std::string namestr = bb->getParent()->getName().str()+"::"+bb->getName().str();
    Constant* name = getStringLiteralExpression( *mod, ">>> BasicBlock "+namestr+"\n" );
    Value* args[] = { name };
    CallInst* ci;
    if ( !isa<LandingPadInst>(bb->getFirstNonPHI()) )
      ci = CallInst::Create(debugprintf, args, "debug-ctrl", bb->getFirstNonPHI());
    else
      ci = CallInst::Create(debugprintf, args, "debug-ctrl", bb->getTerminator());
    preprocess.addToLPS(ci, bb->getTerminator());
  }
#endif

  // Instrument memory alloccation related calls

  modified |= addSmtxMemallocs(all_memalloc_ops);

  // Apply heap separation speculation

  std::map<unsigned, unsigned> global2versioned;
  std::map<unsigned, unsigned> global2nonversioned;

  modified |= addSeparationMemallocs(module, global2versioned, global2nonversioned);

  if ( global2versioned.size() || global2nonversioned.size() )
  {
    Value* args[] = {
      ConstantInt::get(api->getU32(), global2nonversioned.size()),
      ConstantInt::get(api->getU32(), global2versioned.size())
    };

    CallInst::Create( api->getSeparationInit(), args, "", ctorentry->getTerminator());
  }

  // Perform per-loop preprocessing.

  for(unsigned i=0; i<loops.size(); ++i)
  {
    Loop *loop = loops[i];
    BasicBlock *header = loop->getHeader();
    Function *fcn = header->getParent();
    DEBUG(errs() << "SpecPriv ApplySmtxSlampSpec: Processing loop "
      << fcn->getName() << " :: " << header->getName() << "\n");

    modified |= runOnLoop(loop, all_mem_ops, skippables, global2versioned, global2nonversioned);
  }

  // add smtxVerRead call to all reads in the RoI

  modified |= addSmtxChecks(NULL, all_mem_ops, skippables);

  InstInsertPt pt = InstInsertPt::Before(ctorentry->getTerminator());
  modified |= addPredictionChecks(pt);

  if( modified )
  {
    // Invalidate LoopInfo-analysis, since we have changed
    // the code.
    for(RoI::FSet::iterator i=roi.fcns.begin(), e=roi.fcns.end(); i!=e; ++i)
      mloops.forget(*i);

    preprocess.assert_strategies_consistent_with_ir();
    DEBUG(errs() << "Successfully applied speculation to sequential IR\n");
  }

  errs() << "numSmtxWrite: " << numSmtxWrite << "\n";
  errs() << "numSmtxRead: " << numSmtxRead << "\n";

  return modified;
}

void ApplySmtxSlampSpec::init(ModuleLoops &mloops)
{
  //LLVMContext &ctx = getGlobalContext();
  LLVMContext& ctx = mod->getContext();

  voidty = Type::getVoidTy(ctx);
  IntegerType *u8 = Type::getInt8Ty(ctx);
  u16 = Type::getInt16Ty(ctx);
  voidptr = PointerType::getUnqual(u8);

  DEBUG(errs() << "SpecPriv ApplySmtxSlampSpec: Processing parallel region, consisting of:\n");

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

bool ApplySmtxSlampSpec::runOnLoop(
    Loop *loop,
    InstVec &all_mem_ops,
    std::set<const Instruction*>& skippables,
    std::map<unsigned, unsigned>& g2v,
    std::map<unsigned, unsigned>& g2nv
)
{
  BasicBlock *preheader = loop->getLoopPreheader();
  assert( preheader && "Run loop simplify first!");

  bool modified = false;

  // call loop invocation function from preheader

  InstInsertPt pt = InstInsertPt::Before(preheader->getTerminator());
  pt << CallInst::Create(api->getLoopInvocation());

  modified = true;

  // call loop exit function from all exits

  SmallVector<BasicBlock*, 8> exits;
  std::set<BasicBlock*>       instrumented;
  loop->getExitBlocks(exits);

  std::set<BasicBlock*> unique_exits;
  unique_exits.insert(exits.begin(), exits.end());

  for (set<BasicBlock*>::iterator i = unique_exits.begin() ; i != unique_exits.end() ; i++)
  {
    BasicBlock* exit = *i;

    if (instrumented.count(exit)) continue;
    instrumented.insert(exit);

    pt = InstInsertPt::Before(exit->getFirstNonPHI());
    pt << CallInst::Create(api->getLoopExit());
  }

  Selector &selector = getAnalysis< Selector >();
  LoopParallelizationStrategy *lps = &selector.getStrategy(loop);
  if( PipelineStrategy *pipeline = dyn_cast<PipelineStrategy>(lps) )
  {
    SmtxSlampSpeculationManager &manager = getAnalysis< SmtxSlampSpeculationManager >();
    manager.unspeculate(loop, *pipeline);
  }

  modified |= addSmtxChecks(loop, all_mem_ops, skippables);

  pt = InstInsertPt::Before(preheader->getTerminator());
  modified |= addSeparationSetup(loop, pt, g2v, g2nv);

  return modified;
}

bool ApplySmtxSlampSpec::addPredictionChecks(InstInsertPt& pt)
{
  Preprocess &preprocess = getAnalysis< Preprocess >();
  SmtxSlampSpeculationManager &manager = getAnalysis< SmtxSlampSpeculationManager >();

  SmtxSlampSpeculationManager::Contexts& contexts = manager.getContexts();
  SmtxSlampSpeculationManager::Contexts::iterator i = contexts.begin();

  unsigned max_id = 0;
  for ( ; i != contexts.end() ; i++)
  {
    CallInst* ci = const_cast<CallInst*>(i->first);
    unsigned  id = i->second;

    if (id > max_id) max_id = id;

    InstInsertPt pt = InstInsertPt::Before(ci);
    llvm::SmallVector<Value*,1> args;
    args.push_back( ConstantInt::get(api->getU32(), id) );

    CallInst* push = CallInst::Create(api->getPushContext(), ArrayRef<Value*>(args));
    pt << push;
    preprocess.addToLPS(push, ci);

    pt = InstInsertPt::After(ci);
    CallInst* pop = CallInst::Create(api->getPopContext());
    pt << pop;
    preprocess.addToLPS(pop, ci);
  }

  // update loop_invocation to initialize loop invariant holders
  llvm::SmallVector<Value*, 64> actuals;
  actuals.push_back( ConstantInt::get( api->getU32(), liloads.size() ) );
  actuals.push_back( ConstantInt::get( api->getU32(), lploads.size() ) );
  actuals.push_back( ConstantInt::get( api->getU32(), max_id+1 ) );

  Constant* zero = ConstantInt::get(api->getU64(), 0);

  std::map<unsigned, std::vector<Constant*> > aparams;
  std::map<unsigned, std::vector<Constant*> > bparams;
  std::map<unsigned, std::vector<Constant*> > dparams;

  for (std::map<LoadInst*, unsigned>::iterator i = lploads.begin() ; i != lploads.end(); ++i)
  {
    const LoadInst* li = i->first;
    unsigned        id = i->second;

    std::vector<Constant*> as(max_id+1, zero);
    std::vector<Constant*> bs(max_id+1, zero);
    std::vector<Constant*> ds(max_id+1, zero);

    SmtxSlampSpeculationManager::LinearPredictors& lps = manager.getLinearPredictors(NULL, li);
    SmtxSlampSpeculationManager::LinearPredictors::iterator j = lps.begin();
    for ( ; j != lps.end() ; ++j)
    {
      unsigned context = j->context;
      int64_t  a = j->a;
      int64_t  b = j->b;
      bool     is_double = j->is_double;

      as[context] = ConstantInt::get(api->getU64(), a);
      bs[context] = ConstantInt::get(api->getU64(), b);
      ds[context] = ConstantInt::get(api->getU32(), is_double);
    }

    aparams[id] = as;
    bparams[id] = bs;
    dparams[id] = ds;
  }

  for (map<unsigned, std::vector<Constant*> >::iterator i = aparams.begin() ; i != aparams.end() ; i++)
  {
    std::vector<Constant*>& vec = i->second;
    for (unsigned j = 0 ; j < vec.size() ; j++) actuals.push_back(vec[j]);
  }
  for (map<unsigned, std::vector<Constant*> >::iterator i = bparams.begin() ; i != bparams.end() ; i++)
  {
    std::vector<Constant*>& vec = i->second;
    for (unsigned j = 0 ; j < vec.size() ; j++) actuals.push_back(vec[j]);
  }
  for (map<unsigned, std::vector<Constant*> >::iterator i = dparams.begin() ; i != dparams.end() ; i++)
  {
    std::vector<Constant*>& vec = i->second;
    for (unsigned j = 0 ; j < vec.size() ; j++) actuals.push_back(vec[j]);
  }

  pt << CallInst::Create( api->getInitPredictors(), ArrayRef<Value*>(actuals), "");

  return true;
}

static void separateVersionedAndNonVersioned(
    HeapProfileLoad::HeapIDs& s,
    std::map<unsigned, unsigned>& global2versioned,
    std::map<unsigned, unsigned>& global2nonversioned,
    HeapProfileLoad::HeapIDs& vs,
    HeapProfileLoad::HeapIDs& nvs
)
{
  for (HeapProfileLoad::HeapIDs::iterator i = s.begin() ; i != s.end() ; i++)
  {
    unsigned gid = *i;
    if ( global2versioned.count(gid) )
    {
      vs.insert( global2versioned[gid] );
    }
    else
    {
      assert( global2nonversioned.count(gid) );
      nvs.insert( global2nonversioned[gid] );
    }
  }
}

static void insertRegisterCalls(
    InstInsertPt& pt,
    Type* u32,
    HeapProfileLoad::HeapIDs& vs,
    HeapProfileLoad::HeapIDs& nvs,
    Constant* vsfcn,
    Constant* nvsfcn
)
{
  std::vector<Value*> params;

  params.push_back( ConstantInt::get( u32, vs.size() ) );
  for ( HeapProfileLoad::HeapIDs::iterator j = vs.begin() ; j != vs.end() ; j++)
  {
    params.push_back( ConstantInt::get( u32, *j ) );
  }
  pt << CallInst::Create( vsfcn, ArrayRef<Value*>(params) );

  params.clear();

  params.push_back( ConstantInt::get( u32, nvs.size() ) );
  for ( HeapProfileLoad::HeapIDs::iterator j = nvs.begin() ; j != nvs.end() ; j++)
  {
    params.push_back( ConstantInt::get( u32, *j ) );
  }

  pt << CallInst::Create( nvsfcn, ArrayRef<Value*>(params) );
}

bool ApplySmtxSlampSpec::addSeparationSetup(
    Loop* loop,
    InstInsertPt& pt,
    std::map<unsigned, unsigned>& g2v,
    std::map<unsigned, unsigned>& g2nv
)
{
  const Selector &selector = getAnalysis< Selector >();
  const LoopParallelizationStrategy *lps = &selector.getStrategy(loop);

  HeapProfileLoad& hpload = getAnalysis<HeapProfileLoad>();

  HeapProfileLoad::HeapIDs classified;

  HeapProfileLoad::HeapIDs ros = hpload.getROHeap(loop);
  classified.insert(ros.begin(), ros.end());

  HeapProfileLoad::HeapIDs nrbws = hpload.getNRBWHeap(loop);
  classified.insert(nrbws.begin(), nrbws.end());

  std::vector<HeapProfileLoad::HeapIDs> vec;

  for (unsigned stage = 0 ; stage < lps->getStageNum() ; stage++)
  {
    HeapProfileLoad::HeapIDs s = hpload.getStagePrivateHeap(loop, stage);

    vec.push_back(s);
    classified.insert(s.begin(), s.end());
  }

  if (classified.empty())
    return false;

  pt << CallInst::Create( api->getClearSeparationHeaps() );

  HeapProfileLoad::HeapIDs vs;
  HeapProfileLoad::HeapIDs nvs;

  separateVersionedAndNonVersioned(ros, g2v, g2nv, vs, nvs);
  insertRegisterCalls( pt, api->getU32(), vs, nvs, api->getRegisterVRO(), api->getRegisterNVRO() );

  vs.clear();
  nvs.clear();

  separateVersionedAndNonVersioned(nrbws, g2v, g2nv, vs, nvs);
  insertRegisterCalls( pt, api->getU32(), vs, nvs, api->getRegisterVNRBW(), api->getRegisterNVNRBW() );

  for (unsigned i = 0 ; i < vec.size() ; i++)
  {
    vs.clear();
    nvs.clear();

    if (vec[i].empty()) continue;

    separateVersionedAndNonVersioned(vec[i], g2v, g2nv, vs, nvs);

    std::vector<Value*> params;

    params.push_back( ConstantInt::get( api->getU32(), i ) );
    params.push_back( ConstantInt::get( api->getU32(), vs.size() ) );
    for ( HeapProfileLoad::HeapIDs::iterator j = vs.begin() ; j != vs.end() ; j++)
    {
      params.push_back( ConstantInt::get( api->getU32(), *j ) );
    }
    pt << CallInst::Create( api->getRegisterVSP(), ArrayRef<Value*>(params) );

    params.clear();

    params.push_back( ConstantInt::get( api->getU32(), i ) );
    params.push_back( ConstantInt::get( api->getU32(), nvs.size() ) );
    for ( HeapProfileLoad::HeapIDs::iterator j = nvs.begin() ; j != nvs.end() ; j++)
    {
      params.push_back( ConstantInt::get( api->getU32(), *j ) );
    }

    pt << CallInst::Create( api->getRegisterNVSP(), ArrayRef<Value*>(params) );
  }

  HeapProfileLoad::HeapIDs unclassified;

  for (HeapProfileLoad::iterator ki = hpload.begin() ; ki != hpload.end() ; ki++)
  {
    unsigned id = ki->second;
    if ( !classified.count(id) ) unclassified.insert(id);
  }

  vs.clear();
  nvs.clear();

  separateVersionedAndNonVersioned(unclassified, g2v, g2nv, vs, nvs);
  insertRegisterCalls( pt, api->getU32(), vs, nvs, api->getRegisterVUC(), api->getRegisterNVUC() );

  return true;
}

bool ApplySmtxSlampSpec::addSmtxChecks(Loop *loop, InstVec &all_mem_ops, std::set<const Instruction*>& skippables)
{
  bool modified = false;

  //DataLayout &td = getAnalysis< DataLayout >();
  Preprocess &preprocess = getAnalysis< Preprocess >();
  SmtxSlampSpeculationManager &manager = getAnalysis< SmtxSlampSpeculationManager >();


  // Basically, we want to apply ver_write
  // to every write, and apply ver_read to every speculative read.
  Constant *ver_write = api->getVerWrite(),
           *ver_write1 = api->getVerWrite1(),
           *ver_write2 = api->getVerWrite2(),
           *ver_write4 = api->getVerWrite4(),
           *ver_write8 = api->getVerWrite8(),
           *ver_read  = api->getVerRead(),
           *ver_read1 = api->getVerRead1(),
           *ver_read2 = api->getVerRead2(),
           *ver_read4 = api->getVerRead4(),
           *ver_read8 = api->getVerRead8(),
           *ver_memmove = api->getVerMemMove();

#if DEBUG_MEMORY_FOOTPRINT
  Constant *debug = api->getDebugPrintf();
#endif

  NewInstructions newInstructions;

  for(unsigned i=0, N=all_mem_ops.size(); i<N; ++i)
  {
    Instruction *inst = all_mem_ops[i];
    if( !inst )
      continue;

    Module*   mod = inst->getParent()->getParent()->getParent();
    const DataLayout &td = mod->getDataLayout();

    if ( skippables.count(inst) )
    {
      all_mem_ops[i] = 0;
      continue;
    }

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

    unsigned write_size_primitive = 0, read_size_primitive = 0;
    if( StoreInst *store = dyn_cast<StoreInst>(inst) )
    {
      write = store;
      write_ptr = store->getPointerOperand();
      const unsigned sz = td.getTypeStoreSize( store->getValueOperand()->getType() );
      write_size_primitive = sz;
      write_size = ConstantInt::get( api->getU32(), sz );
    }
    else if( LoadInst *load = dyn_cast<LoadInst>(inst) )
    {
      read = load;
      read_ptr = load->getPointerOperand();
      const unsigned sz = td.getTypeStoreSize( load->getType() );
      read_size_primitive = sz;
      read_size = ConstantInt::get( api->getU32(), sz );
    }
    else if( MemSetInst *memset = dyn_cast<MemSetInst>(inst) )
    {
      write = memset;
      write_ptr = memset->getRawDest();
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
      //errs() << "Cannot instrument: " << *inst << '\n';
      //errs() << "- In basic block : " << inst->getParent()->getName() << '\n';
      //errs() << "- In function    : " << inst->getParent()->getParent()->getName() << '\n';
      //assert(false && "Incomplete validation coverage");
    }

    // Instrument *all* writes
    if( write )
    {
      InstInsertPt where = InstInsertPt::After(write);

      write_ptr = castPtrToVoidPtr(write_ptr, where, &newInstructions);
      write_size = castIntToInt32Ty(write_size, where, &newInstructions);

      // Insert the call.
      llvm::SmallVector<Value*,3> actuals;
      actuals.push_back( write_ptr );

      CallInst* call;
      switch ( write_size_primitive )
      {
        case 1:
          call = CallInst::Create(ver_write1, ArrayRef<Value*>(actuals));
          break;
        case 2:
          call = CallInst::Create(ver_write2, ArrayRef<Value*>(actuals));
          break;
        case 4:
          call = CallInst::Create(ver_write4, ArrayRef<Value*>(actuals));
          break;
        case 8:
          call = CallInst::Create(ver_write8, ArrayRef<Value*>(actuals));
          break;
        default:
          actuals.push_back( write_size );
          call = CallInst::Create(ver_write, ArrayRef<Value*>(actuals));
      }

      where << call;
      newInstructions.push_back(call);

      all_mem_ops[i] = 0;
      modified = true;
      ++numSmtxWrite;
    }

    // Instrument *speculative* reads
    if( read )
    {
      InstInsertPt where = InstInsertPt::After(read);

      read_ptr = castPtrToVoidPtr(read_ptr, where, &newInstructions);
      read_size = castIntToInt32Ty(read_size, where, &newInstructions);

      if ( manager.useLoopInvariantPrediction(loop, read) )
      {
        LoadInst* loadRead = dyn_cast<LoadInst>(read);
        assert(loadRead);

        if ( !li_instrumented.count(loadRead) )
        {
          unsigned id = liloads.size();
          liloads[loadRead] = id;

          // generate a function to call the register_loop_invariant_buffer_function

          Function* wrapper;
          {
            //LLVMContext& ctxt = getGlobalContext();
            LLVMContext& ctxt = mod->getContext();

            std::vector<Type*> formals(2);
            formals[0] = voidptr;      // read_ptr
            formals[1] = api->getU32(); // read_size

            FunctionType* fty = FunctionType::get(voidty, formals, false);
            char namebuf[80];
            sprintf(namebuf, "register_loop_invariant_buffer_wrapper%u", id);
            std::string   name(namebuf);
            Function*     fcn = cast<Function>(mod->getOrInsertFunction(name, fty));

            BasicBlock* entry = BasicBlock::Create(ctxt, "entry", fcn);
            BasicBlock* check = BasicBlock::Create(ctxt, "check-iteration", fcn);
            BasicBlock* call_register = BasicBlock::Create(ctxt, "call-register", fcn);
            BasicBlock* call_validate = BasicBlock::Create(ctxt, "call-validate", fcn);
            BasicBlock* ret = BasicBlock::Create(ctxt, "ret", fcn);

            // CFG:
            //              entry
            //            /
            //          check
            //       /          \
            //  call-register call-validate

            // entry: if the context is worth checking

            uint64_t ctxts = 0;
            const std::set<unsigned>& applicable_ctxts = manager.getLIPredictionApplicableCtxts(loop, loadRead);
            for (std::set<unsigned>::const_iterator i = applicable_ctxts.begin() ; i != applicable_ctxts.end() ; i++)
            {
              assert( (*i) < 64 );
              ctxts |= ((uint64_t)1 << (uint64_t)(*i));
            }
            ConstantInt* bv = ConstantInt::get( api->getU64(), ctxts );

            InstInsertPt pt = InstInsertPt::Beginning(entry);

            CallInst* context = CallInst::Create( api->getGetContext() );
            pt << context;

            ConstantInt* one = ConstantInt::get( api->getU64(), 1 );
            Instruction* sft = BinaryOperator::Create(Instruction::Shl, one, context, "shift");
            pt << sft;

            Instruction* aand = BinaryOperator::Create(Instruction::And, sft, bv, "and");
            pt << aand;

            ICmpInst* cmp2 = new ICmpInst(ICmpInst::ICMP_EQ, aand, ConstantInt::get( api->getU64(), 0) );
            pt << cmp2;

            BranchInst* br2 = BranchInst::Create(ret, check, cmp2);
            pt << br2;

            // check: check iteration count

            pt = InstInsertPt::Beginning(check);

            CallInst* iter = CallInst::Create( api->getCurrentIter() );
            pt << iter;
            ICmpInst* cmp = new ICmpInst(ICmpInst::ICMP_NE, iter, ConstantInt::get( api->getU32(), 0) );
            pt << cmp;
            BranchInst* br = BranchInst::Create(call_validate, call_register, cmp);
            pt << br;

            // call-register: call register_loop_invariant_buffer function

            pt = InstInsertPt::Beginning(call_register);

            llvm::SmallVector<Value*,4> actuals;
            actuals.push_back( ConstantInt::get( api->getU32(), id) );
            actuals.push_back( context );
            for (Function::arg_iterator ai = fcn->arg_begin() ; ai != fcn->arg_end() ; ai++)
            {
              actuals.push_back(&*ai);
            }

            pt << CallInst::Create( api->getRegisterLoopInvariantBuffer(), ArrayRef<Value*>(actuals) );
            pt << ReturnInst::Create(ctxt);

            // call-validate: call loop_invariant_* function

            pt = InstInsertPt::Beginning(call_validate);

            pt << CallInst::Create( api->getCheckLoopInvariant(), ArrayRef<Value*>(actuals) );
            pt << ReturnInst::Create(ctxt);

            // return:

            pt = InstInsertPt::Beginning(ret);
            pt << ReturnInst::Create(ctxt);

            wrapper = fcn;
          }

          llvm::SmallVector<Value*,2> actuals;
          actuals.push_back( read_ptr );
          actuals.push_back( read_size );

          CallInst* r = CallInst::Create( wrapper, ArrayRef<Value*>(actuals) );
          where << r;
          newInstructions.push_back(r);

          li_instrumented.insert(loadRead);
        }
      }
      else if ( manager.useLinearPredictor(loop, read) )
      {
        LoadInst* loadRead = dyn_cast<LoadInst>(read);
        assert(loadRead);

        if ( !lp_instrumented.count(loadRead) )
        {
          unsigned id = lploads.size();
          lploads[loadRead] = id;

          // check if context is linear prediction applicable context

          CallInst* context = CallInst::Create( api->getGetContext() );
          where << context;
          newInstructions.push_back(context);

          Value* valid;
          {
            uint64_t ctxts = 0;
            SmtxSlampSpeculationManager::LinearPredictors& lps = manager.getLinearPredictors(loop, loadRead);
            SmtxSlampSpeculationManager::LinearPredictors::iterator i = lps.begin();
            for ( ; i != lps.end() ; i++)
            {
              assert( (i->context) < 64 );
              ctxts |= ((uint64_t)1 << (uint64_t)(i->context));
            }
            ConstantInt* bv = ConstantInt::get( api->getU64(), ctxts );

            ConstantInt* one = ConstantInt::get( api->getU64(), 1 );
            Instruction* sft = BinaryOperator::Create(Instruction::Shl, one, context, "shift");
            where << sft;
            newInstructions.push_back(sft);

            Instruction* aand = BinaryOperator::Create(Instruction::And, sft, bv, "and");
            where << aand;
            newInstructions.push_back(aand);

            ICmpInst* cmp = new ICmpInst(ICmpInst::ICMP_NE, aand, ConstantInt::get( api->getU64(), 0) );
            where << cmp;
            newInstructions.push_back(cmp);

            valid = cmp;
          }

          llvm::SmallVector<Value*,6> actuals;
          actuals.push_back( valid );
          actuals.push_back( ConstantInt::get( api->getU32(), id ) );
          actuals.push_back( context );
          actuals.push_back( read_ptr );
          actuals.push_back( read_size );

          CallInst* pred = CallInst::Create( api->getCheckRegisterLinearPredictor(), ArrayRef<Value*>(actuals) );
          where << pred;
          newInstructions.push_back(pred);

          lp_instrumented.insert(loadRead);
        }
      }
      else
      {
#if 1
        // Insert the call.
        llvm::SmallVector<Value*,3> actuals;
        actuals.push_back( read_ptr );

        CallInst* call;
        switch ( read_size_primitive )
        {
          case 1:
            call = CallInst::Create(ver_read1, ArrayRef<Value*>(actuals));
            break;
          case 2:
            call = CallInst::Create(ver_read2, ArrayRef<Value*>(actuals));
            break;
          case 4:
            call = CallInst::Create(ver_read4, ArrayRef<Value*>(actuals));
            break;
          case 8:
            call = CallInst::Create(ver_read8, ArrayRef<Value*>(actuals));
            break;
          default:
            actuals.push_back( read_size );
            call = CallInst::Create(ver_read, ArrayRef<Value*>(actuals));
        }
        where << call;
        newInstructions.push_back(call);

        all_mem_ops[i] = 0;
        ++numSmtxRead;
#endif
      }

      modified = true;
    }

    for(unsigned j=0, N=newInstructions.size(); j<N; ++j)
      preprocess.addToLPS(newInstructions[j], inst);
    newInstructions.clear();
  }

  return modified;
}

bool ApplySmtxSlampSpec::addSmtxMemallocs(InstVec &all_memalloc_ops)
{
  bool modified = false;

  Preprocess &preprocess = getAnalysis< Preprocess >();
  const RoI  &roi = preprocess.getRoI();

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

    Value    *sz = 0;

    if ( AllocaInst *alloca = dyn_cast<AllocaInst>( inst ) )
    {
      // ignore short-lived alloca
      if ( !roi.roots.count( alloca->getParent() ) )
        continue;

      //DataLayout &td = getAnalysis< DataLayout >();
      const DataLayout &td = inst->getModule()->getDataLayout();
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

bool ApplySmtxSlampSpec::isValidSeparationMemallocs(CallInst* ci)
{
  Preprocess& preprocess = getAnalysis< Preprocess >();
  const RoI & roi = preprocess.getRoI();

  Function* cf = ci->getCalledFunction();
  if (!cf)
  {
    errs() << "Error: separation memalloc instruction is an indirect call\n";
    return false;
  }

  if ( roi.bbs.count(ci->getParent()) )
  {
    if (
        cf != api->getVerMalloc() &&
        cf != api->getVerCalloc() &&
        cf != api->getVerRealloc() &&
        cf != api->getVerFree()
       )
    {
      errs() << "Error: called function of separation memalloc is not a versioned-memalloc\n";
      return false;
    }
  }
  else
  {
    if (
        cf != api->getMalloc() &&
        cf != api->getCalloc() &&
        cf != api->getRealloc() &&
        cf != api->getFree()
       )
    {
      errs() << "Error: called function of separation memalloc is not a memalloc\n";
      return false;
    }
  }

  return true;
}

Constant* ApplySmtxSlampSpec::getMatchingSeparationMemalloc(Function* fcn)
{
  assert(fcn);

  if ( fcn == api->getMalloc() )  return api->getSepMalloc();
  if ( fcn == api->getCalloc() )  return api->getSepCalloc();
  if ( fcn == api->getRealloc() ) return api->getSepRealloc();
  if ( fcn == api->getFree() )    return api->getSepFree();

  assert("No matching separation memalloc!" && false);
}

Constant* ApplySmtxSlampSpec::getMatchingVersionedSeparationMemalloc(Function* fcn)
{
  assert(fcn);

  if ( fcn == api->getVerMalloc() )  return api->getVerSepMalloc();
  if ( fcn == api->getVerCalloc() )  return api->getVerSepCalloc();
  if ( fcn == api->getVerRealloc() ) return api->getVerSepRealloc();
  if ( fcn == api->getVerFree() )    return api->getVerSepFree();

  assert("No matching versioned-separation memalloc!" && false);
}

bool ApplySmtxSlampSpec::addSeparationMemallocs(Module& m,
  std::map<unsigned, unsigned>& global2versioned,
  std::map<unsigned, unsigned>& global2nonversioned
)
{
  bool modified = false;

  //LLVMContext&     ctxt = getGlobalContext();
  LLVMContext& ctxt = mod->getContext();

  HeapProfileLoad& hpload = getAnalysis<HeapProfileLoad>();

  Preprocess& preprocess = getAnalysis< Preprocess >();
  const RoI & roi = preprocess.getRoI();

  // classfy heap ids to versioned and non-versioned

  for (HeapProfileLoad::iterator ki = hpload.begin() ; ki != hpload.end() ; ki++)
  {
    HeapProfileLoad::Key key = ki->first;
    unsigned             id =  ki->second;

    Instruction* inst = key.second;

    if ( roi.bbs.count(inst->getParent()) )
    {
      unsigned sz = global2versioned.size();
      global2versioned[id] = sz;
    }
    else
    {
      unsigned sz = global2nonversioned.size();
      global2nonversioned[id] = sz;
    }
  }

  // replace calls

  HeapProfileLoad::InstSet contexts;
  unsigned wrappercount = 0;

  std::vector<Instruction*> to_erase;

  for (Module::iterator mi = m.begin() ; mi != m.end() ; mi++)
  {
    Function* fcn = &*mi;

    if ( fcn->isDeclaration() ) continue;

    for (inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
    {
      CallInst* inst = dyn_cast<CallInst>(&*ii);

      if (!inst) continue;

      if ( hpload.isClassifiedAlloc(inst) )
      {
        assert( isValidSeparationMemallocs(inst) );
        Function* called_function = inst->getCalledFunction();
        assert( called_function );

        if ( hpload.allocRequiresContext(inst) )
        {
          // memalloc call from RoI, but not from the loop itself
          // generate a wrapper function for context check

          HeapProfileLoad::InstSet contexts_to_check = hpload.getAllocContextsFor(inst);
          contexts.insert(contexts_to_check.begin(), contexts_to_check.end());

          Function* wrapper = NULL;
          {
            char namebuf[80];
            sprintf(namebuf, "__specpriv_versioned_separation_memalloc_wrapper%u\n", wrappercount++);
            std::string name(namebuf);
            wrapper = cast<Function>(m.getOrInsertFunction(name, called_function->getFunctionType()));

            // default: just call the original called function and return the result

            BasicBlock* dflt = BasicBlock::Create(ctxt, "default", wrapper);
            {
              std::vector<Value*> args;
              for (Function::arg_iterator ai = wrapper->arg_begin() ; ai != wrapper->arg_end() ; ai++)
                args.push_back(&*ai);

              InstInsertPt where = InstInsertPt::Beginning(dflt);
              CallInst* dfltcall = CallInst::Create( called_function, ArrayRef<Value*>(args) );
              where << dfltcall;

              if ( called_function->getReturnType() != voidty )
                where << ReturnInst::Create(ctxt, dfltcall);
              else
                where << ReturnInst::Create(ctxt);
            }

            // Entry block

            BasicBlock* entry = BasicBlock::Create(ctxt, "entry", wrapper, dflt);
            SwitchInst* si;
            {
              InstInsertPt where = InstInsertPt::Beginning(entry);

              CallInst* alloc_ctxt = CallInst::Create( api->getSeparationAllocContext() );
              where << alloc_ctxt;

              si = SwitchInst::Create(alloc_ctxt, dflt, contexts_to_check.size());
              where << si;
            }

            // for each context to check

            for (HeapProfileLoad::InstSet::iterator i = contexts_to_check.begin() ; i != contexts_to_check.end() ; i++)
            {
              BasicBlock*  bb = BasicBlock::Create(ctxt, "bb", wrapper, dflt);
              InstInsertPt where = InstInsertPt::Beginning(bb);

              unsigned heapid = hpload.getHeapID( make_pair(*i, inst) );
              assert( heapid );

              assert( global2versioned.count(heapid) );
              unsigned vid = global2versioned[heapid];

              std::vector<Value*> args;

              for (Function::arg_iterator ai = wrapper->arg_begin() ; ai != wrapper->arg_end() ; ai++)
              {
                args.push_back( &*ai );
              }
              args.push_back( ConstantInt::get(api->getU32(), vid) );

              Function* caller = cast<Function>( getMatchingVersionedSeparationMemalloc(called_function ) );
              CallInst* newcall = CallInst::Create(caller, ArrayRef<Value*>(args));
              where << newcall;

              if ( called_function->getReturnType() != voidty )
                where << ReturnInst::Create(ctxt, newcall);
              else
                where << ReturnInst::Create(ctxt);

              si->addCase( ConstantInt::get(api->getU32(), hpload.getContextID(*i)), bb );
            }
          }

          // replace call

          inst->setCalledFunction(wrapper);
        }
        else if ( roi.bbs.count(inst->getParent()) )
        {
          // memalloc call from the loop itself
          // calls versioned-separation-malloc directly here

          HeapProfileLoad::Key k(NULL, inst);

          unsigned heapid = hpload.getHeapID(k);
          assert( heapid );

          assert( global2versioned.count(heapid) );
          unsigned vid = global2versioned[heapid];

          std::vector<Value*> args;

          for (unsigned i = 0 ; i < inst->getNumArgOperands() ; i++)
          {
            args.push_back( inst->getOperand(i) );
          }
          args.push_back( ConstantInt::get(api->getU32(), vid) );

          // replace the call

          Function* caller = cast<Function>( getMatchingVersionedSeparationMemalloc( called_function ) );
          CallInst* newcall = CallInst::Create(caller, ArrayRef<Value*>(args), "", inst);

          inst->replaceAllUsesWith(newcall);
          preprocess.replaceInLPS(newcall, inst);
          to_erase.push_back(inst);
        }
        else
        {
          // memalloc calls outside of RoI
          // replace with (non-versioned) separation malloc

          HeapProfileLoad::Key k(NULL, inst);

          unsigned heapid = hpload.getHeapID(k);
          assert( heapid );

          assert( global2nonversioned.count(heapid) );
          unsigned nvid = global2nonversioned[heapid];

          std::vector<Value*> args;

          for (unsigned i = 0 ; i < inst->getNumArgOperands() ; i++)
          {
            args.push_back( inst->getOperand(i) );
          }
          args.push_back( ConstantInt::get(api->getU32(), nvid) );

          // replace the call

          Function* caller = cast<Function>( getMatchingSeparationMemalloc( called_function ) );
          CallInst* newcall = CallInst::Create(caller, ArrayRef<Value*>(args), "", inst);

          inst->replaceAllUsesWith(newcall);
          to_erase.push_back(inst);
        }

        modified |= true;
      }
    }
  }

  for (unsigned i = 0 ; i < to_erase.size() ; i++)
  {
    to_erase[i]->eraseFromParent();
  }

  // inser push/pop for context

  for (HeapProfileLoad::InstSet::iterator i = contexts.begin() ; i != contexts.end() ; i++)
  {
    CallInst* inst = dyn_cast<CallInst>(*i);
    assert(inst);

    InstInsertPt pt = InstInsertPt::Before(inst);
    llvm::SmallVector<Value*,1> args;
    args.push_back( ConstantInt::get(api->getU32(), hpload.getContextID(inst)) );

    CallInst* push = CallInst::Create(api->getPushSeparationAllocContext(), ArrayRef<Value*>(args));
    pt << push;
    preprocess.addToLPS(push, inst);

    pt = InstInsertPt::After(inst);
    CallInst* pop = CallInst::Create(api->getPopSeparationAllocContext());
    pt << pop;
    preprocess.addToLPS(pop, inst);
  }

  return modified;
}

static bool isIdentical(const Value* v1, const Value* v2)
{
  if (v1 == v2)
    return true;

  const Instruction* inst1 = dyn_cast<Instruction>(v1);
  const Instruction* inst2 = dyn_cast<Instruction>(v2);

  if ( !inst1 || !inst2) return false;
  if ( inst1->mayReadFromMemory() || inst2->mayReadFromMemory() ) return false;
  if ( inst1->mayWriteToMemory() || inst2->mayWriteToMemory() ) return false;
  if ( isa<PHINode>(inst1) || isa<PHINode>(inst2) ) return false;
  if ( inst1->getNumOperands() != inst2->getNumOperands() ) return false;

  for (unsigned i = 0 ; i < inst1->getNumOperands() ; i++)
  {
    if ( !isIdentical(inst1->getOperand(i), inst2->getOperand(i)) )
      return false;
  }

  return true;
}

void ApplySmtxSlampSpec::IntraProceduralOptimization(InstVec& all_mem_ops, std::set<const Instruction*>& skippables)
{
  Preprocess&  preprocess = getAnalysis< Preprocess >();
  ModuleLoops& mloops = getAnalysis< ModuleLoops >();

  std::map<Function*, LoadVec>  loads;
  std::map<Function*, StoreVec> stores;

  for (unsigned i = 0 ; i < all_mem_ops.size() ; i++)
  {
    Instruction* inst = all_mem_ops[i];
    Function*    fcn = inst->getParent()->getParent();

    if (LoadInst* load = dyn_cast<LoadInst>(inst))
      loads[fcn].push_back(load);
    if (StoreInst* store = dyn_cast<StoreInst>(inst))
      stores[fcn].push_back(store);
  }

  // loads

  for (std::map<Function*, LoadVec>::iterator fi = loads.begin() ; fi != loads.end() ; fi++)
  {
    const Function* fcn = fi->first;
    DominatorTree&  dt = mloops.getAnalysis_DominatorTree(fcn);

    LoadVec& vec = fi->second;
    for (unsigned i = 0 ; i < vec.size() ; i++)
    {
      LoadInst* i1 =   vec[i];
      Value*    ptr1 = i1->getPointerOperand();

      for (unsigned j = (i+1) ; j < vec.size() ; j++)
      {
        LoadInst* i2 = vec[j];
        Value*    ptr2 = i2->getPointerOperand();

        if ( dt.dominates(i1, i2) && isIdentical(ptr1, ptr2) && preprocess.ifI2IsInI1IsIn(i1, i2) )
        {
          //errs() << "Intra - " << sid.getID(i2) << " is skippable by " << sid.getID(i1) << "\n";
          skippables.insert(i2);
        }
        if ( dt.dominates(i2, i1) && isIdentical(ptr2, ptr1) && preprocess.ifI2IsInI1IsIn(i2, i1) )
        {
          //errs() << "Intra - " << sid.getID(i2) << " is skippable by " << sid.getID(i1) << "\n";
          skippables.insert(i1);
        }
      }
    }
  }

  // stores

  for (std::map<Function*, StoreVec>::iterator fi = stores.begin() ; fi != stores.end() ; fi++)
  {
    const Function* fcn = fi->first;
    DominatorTree&  dt = mloops.getAnalysis_DominatorTree(fcn);

    StoreVec& vec = fi->second;
    for (unsigned i = 0 ; i < vec.size() ; i++)
    {
      StoreInst* i1 =   vec[i];
      Value*    ptr1 = i1->getPointerOperand();

      for (unsigned j = (i+1) ; j < vec.size() ; j++)
      {
        StoreInst* i2 = vec[j];
        Value*    ptr2 = i2->getPointerOperand();

        if ( dt.dominates(i1, i2) && isIdentical(ptr1, ptr2) && preprocess.ifI2IsInI1IsIn(i1, i2) )
        {
          //errs() << "Intra - " << sid.getID(i2) << " is skippable by " << sid.getID(i1) << "\n";
          skippables.insert(i2);
        }
        if ( dt.dominates(i2, i1) && isIdentical(ptr2, ptr1) && preprocess.ifI2IsInI1IsIn(i2, i1) )
        {
          //errs() << "Intra - " << sid.getID(i2) << " is skippable by " << sid.getID(i1) << "\n";
          skippables.insert(i1);
        }
      }
    }
  }
}

static void findUniqueCallsites(const RoI::BBSet& bbs, std::map<const Function*, const Instruction*>& unique_callsites)
{
  std::set<const Function*> exclude;
  for (RoI::BBSet::const_iterator i = bbs.begin() ; i != bbs.end() ; i++)
  {
    const BasicBlock* bb = *i;

    for (BasicBlock::const_iterator ii = bb->begin() ; ii != bb->end() ; ii++)
    {
      if ( const CallInst* call = dyn_cast<CallInst>(&*ii) )
      {
        const Function* cfcn = call->getCalledFunction();

        if (exclude.count(cfcn)) continue;

        if ( unique_callsites.count(cfcn) )
        {
          unique_callsites.erase(cfcn);
          exclude.insert(cfcn);
        }
        else
        {
          unique_callsites[cfcn] = call;
        }
      }
      else if ( const InvokeInst* invoke = dyn_cast<InvokeInst>(&*ii) )
      {
        const Function* cfcn = invoke->getCalledFunction();

        if (exclude.count(cfcn)) continue;

        if ( unique_callsites.count(cfcn) && unique_callsites[cfcn] != invoke)
        {
          unique_callsites.erase(cfcn);
          exclude.insert(cfcn);
        }
        else
        {
          unique_callsites[cfcn] = invoke;
        }
      }
    }
  }
}

void ApplySmtxSlampSpec::findMustExecuted(
  RoI::FSet& fcns,
  std::map<const Function*, LoadVec>& must_executed_loads,
  std::map<const Function*, StoreVec>& must_executed_stores,
  std::map<const Function*, InstVec>& must_executed_calls
)
{
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();

  for (RoI::FSet::const_iterator i = fcns.begin() ; i != fcns.end() ; i++)
  {
    Function*   fcn = *i;
    BasicBlock* entry = &(fcn->getEntryBlock());
    PostDominatorTree& pdt = mloops.getAnalysis_PostDominatorTree(fcn);

    for (inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
    {
      if (LoadInst* load = dyn_cast<LoadInst>(&*ii))
      {
        if (load->getParent() == entry || pdt.dominates(load->getParent(), entry))
        {
          must_executed_loads[fcn].push_back(load);
        }
      }
      else if (StoreInst* store = dyn_cast<StoreInst>(&*ii))
      {
        if (store->getParent() == entry || pdt.dominates(store->getParent(), entry))
        {
          must_executed_stores[fcn].push_back(store);
        }
      }
      else if (CallInst* call = dyn_cast<CallInst>(&*ii))
      {
        if (call->getParent() == entry || pdt.dominates(call->getParent(), entry))
        {
          must_executed_calls[fcn].push_back(call);
        }
      }
      else if (InvokeInst* ivi = dyn_cast<InvokeInst>(&*ii))
      {
        if (ivi->getParent() == entry || pdt.dominates(ivi->getParent(), entry))
        {
          must_executed_calls[fcn].push_back(ivi);
        }
      }
    }
  }
}

static void sweep(Instruction* callsite, Function* fcn,
  std::set<Function*>& visited,
  std::set<Type*>& inapplicables,
  std::map<Function*, std::set<Instruction*> >& mycallsites
)
{
  if ( visited.count(fcn) )
    return;
  visited.insert(fcn);

  mycallsites[fcn].insert(callsite);

  for (inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
  {
    if ( CallInst* ci = dyn_cast<CallInst>(&*ii) )
    {
      if ( ci->getCalledFunction() )
      {
        sweep(callsite, ci->getCalledFunction(), visited, inapplicables, mycallsites);
      }
      else
      {
        inapplicables.insert( ci->getCalledValue()->getType() );
      }
    }
    else if ( InvokeInst* ivi = dyn_cast<InvokeInst>(&*ii) )
    {
      if ( ivi->getCalledFunction() )
      {
        sweep(callsite, ivi->getCalledFunction(), visited, inapplicables, mycallsites);
      }
      else
      {
        inapplicables.insert( ivi->getCalledValue()->getType() );
      }
    }
  }
}

static void sweepMustExecutes(
  Function* fcn,
  std::map<const Function*, std::vector<LoadInst*> >&  must_executed_loads,
  std::map<const Function*, std::vector<StoreInst*> >& must_executed_stores,
  std::map<const Function*, std::vector<Instruction*> >&  must_executed_calls,
  /* output */
  std::set<LoadInst*>&  loads,
  std::set<StoreInst*>& stores
)
{
  // WARNING: may induce infinite recursion

  loads.insert( must_executed_loads[fcn].begin(), must_executed_loads[fcn].end() );
  stores.insert( must_executed_stores[fcn].begin(), must_executed_stores[fcn].end() );

  std::vector<Instruction*>& vec = must_executed_calls[fcn];
  for (unsigned i = 0 ; i < vec.size() ; i++)
  {
    if ( CallInst* ci = dyn_cast<CallInst>(vec[i]) )
    {
      if (ci->getCalledFunction())
        sweepMustExecutes(ci->getCalledFunction(),
            must_executed_loads, must_executed_stores, must_executed_calls,
            loads, stores);
    }
    else if ( InvokeInst* ivi = dyn_cast<InvokeInst>(vec[i]) )
    {
      if (ivi->getCalledFunction())
        sweepMustExecutes(ivi->getCalledFunction(),
            must_executed_loads, must_executed_stores, must_executed_calls,
            loads, stores);
    }
  }
}

void ApplySmtxSlampSpec::myDominators(Instruction* me, RoI::BBSet& roots,
  std::map<const Function*, std::vector<LoadInst*> >&  must_executed_loads,
  std::map<const Function*, std::vector<StoreInst*> >& must_executed_stores,
  std::map<const Function*, std::vector<Instruction*> >&  must_executed_calls,
  /* output */
  std::set<LoadInst*>&  loads,
  std::set<StoreInst*>& stores
)
{
  Preprocess& preprocess = getAnalysis< Preprocess >();
  ModuleLoops &mloops = getAnalysis< ModuleLoops >();

  Function* fcn = me->getParent()->getParent();
  DominatorTree& dt = mloops.getAnalysis_DominatorTree(fcn);

  for (RoI::BBSet::iterator i = roots.begin() ; i != roots.end() ; i++)
  {
    BasicBlock* bb = *i;
    assert( bb->getParent() == fcn );

    for (BasicBlock::iterator j = bb->begin() ; j != bb->end() ; j++)
    {
      Instruction* inst = &*j;

      if ( !dt.dominates(inst, me) )
        continue;

      if ( !preprocess.ifI2IsInI1IsIn(inst, me) )
        continue;

      if ( LoadInst* li = dyn_cast<LoadInst>(inst) )
      {
        loads.insert(li);
      }
      else if ( StoreInst* si = dyn_cast<StoreInst>(inst) )
      {
        stores.insert(si);
      }
      else if ( CallInst* ci = dyn_cast<CallInst>(inst) )
      {
        if ( ci->getCalledFunction() )
          sweepMustExecutes(ci->getCalledFunction(),
              must_executed_loads, must_executed_stores, must_executed_calls,
              loads, stores);
      }
      else if ( InvokeInst* ivi = dyn_cast<InvokeInst>(inst) )
      {
        if ( ivi->getCalledFunction() )
          sweepMustExecutes(ivi->getCalledFunction(),
              must_executed_loads, must_executed_stores, must_executed_calls,
              loads, stores);
      }
    }
  }
}

void ApplySmtxSlampSpec::InterProceduralOptimization(std::set<const Instruction*>& skippables)
{
  Preprocess& preprocess = getAnalysis< Preprocess >();
  RoI&        roi = preprocess.getRoI();
  RoI::BBSet& roots = roi.roots;

  const Function* fcn = NULL;

  // all of this calls are technically within the target loop
  // 'calls' is the list of instructions within the target loop that calls other fcns

  std::vector<Instruction*> calls;

  for (RoI::BBSet::const_iterator i = roots.begin() ; i != roots.end() ; i++)
  {
    BasicBlock* bb = *i;

    if ( fcn )
    {
      assert(bb->getParent() == fcn);
    }
    else
    {
      fcn = bb->getParent();
    }

    for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
    {
      if ( isa<CallInst>(&*ii) || isa<InvokeInst>(&*ii) )
      {
        calls.push_back(&*ii);
      }
    }
  }

  // we need a "sweep" hear, to build a Function to (set of instructions) map,
  // for all function in RoI.
  // 'mycallsites' is a map from function to the set of callsites which is in the target loop,
  // that may call the function

  std::set<Type*> inapplicables;
  std::map<Function*, std::set<Instruction*> > mycallsites;

  for (unsigned i = 0 ; i < calls.size() ; i++)
  {
    std::set<Function*> visited;

    if ( CallInst* ci = dyn_cast<CallInst>(calls[i]) )
    {
      if ( ci->getCalledFunction() )
      {
        sweep(ci, ci->getCalledFunction(), visited, inapplicables, mycallsites);
      }
      else
      {
        inapplicables.insert( ci->getCalledValue()->getType() );
      }
    }
    else if ( InvokeInst* ivi = dyn_cast<InvokeInst>(calls[i]) )
    {
      if ( ivi->getCalledFunction() )
      {
        sweep(ci, ci->getCalledFunction(), visited, inapplicables, mycallsites);
      }
      else
      {
        inapplicables.insert( ci->getCalledValue()->getType() );
      }
    }
  }

  // find must executable loads, stores, and calls for each function in RoI

  std::map<const Function*, LoadVec>  must_executed_loads;
  std::map<const Function*, StoreVec> must_executed_stores;
  std::map<const Function*, InstVec>  must_executed_calls;
  findMustExecuted(roi.fcns, must_executed_loads, must_executed_stores, must_executed_calls);

  // for each function in RoI

  for (std::set<Function*>::iterator i = roi.fcns.begin() ; i != roi.fcns.end() ; i++)
  {
    Function* fcn = *i;

    if ( inapplicables.count(fcn->getType()) ) continue;

    bool is_first = true;
    std::set<LoadInst*>  g_loads;
    std::set<StoreInst*> g_stores;

    // for each callsite of that function

    std::set<Instruction*>& insts = mycallsites[fcn];

    for (std::set<Instruction*>::iterator j = insts.begin() ; j != insts.end() ; j++)
    {
      std::set<LoadInst*>  loads;
      std::set<StoreInst*> stores;

      myDominators(*j, roi.roots,
          must_executed_loads, must_executed_stores, must_executed_calls,
          loads, stores);

      if (is_first)
      {
        g_loads.insert(loads.begin(), loads.end());
        g_stores.insert(stores.begin(), stores.end());
        is_first = false;
      }
      else
      {
        for (std::set<LoadInst*>::iterator li = g_loads.begin() ; li != g_loads.end() ; )
        {
          if ( loads.count(*li) ) li++;
          else g_loads.erase(li++);
        }
        for (std::set<StoreInst*>::iterator si = g_stores.begin() ; si != g_stores.end() ; )
        {
          if ( stores.count(*si) ) si++;
          else g_stores.erase(si++);
        }
      }
    }

    // for each instruction in the function
    for (inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
    {
      if ( LoadInst* li = dyn_cast<LoadInst>(&*ii) )
      {
        for ( std::set<LoadInst*>::iterator gli = g_loads.begin() ; gli != g_loads.end() ; gli++)
        {
          if ( isIdentical( (*gli)->getPointerOperand(), li->getPointerOperand() ) )
          {
            skippables.insert(li);
          }
        }
      }
      if ( StoreInst* si = dyn_cast<StoreInst>(&*ii) )
      {
        for ( std::set<StoreInst*>::iterator gsi = g_stores.begin() ; gsi != g_stores.end() ; gsi++)
        {
          if ( isIdentical( (*gsi)->getPointerOperand(), si->getPointerOperand() ) )
          {
            skippables.insert(si);
          }
        }
      }
    }
  }
}

char ApplySmtxSlampSpec::ID = 0;
static RegisterPass<ApplySmtxSlampSpec> x("spec-priv-apply-smtx-slamp-spec",
  "Apply SMTX speculation (based on SLAMP) to RoI");
}
}

