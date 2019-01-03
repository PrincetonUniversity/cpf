//===- HeapProfile.cpp - Insert Heap Profile Instrumentation -----------===//

// !!DEPRECATED
// This profile is not used at all for now. Might be useful later. 
//
// If you remove the "if 0" macro below and compile this with other files,
// you are likely to experience a segfault at the end of the pass manager. 
// All the output files will be generated correctly, but just annoying.

#if 0 

#define DEBUG_TYPE "HP"

#include "HeapProfile.h"
#include "Preprocess.h"
// share SLAMP extern file
#include "liberty/SLAMP/externs.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"

#include "liberty/SpecPriv/Indeterminate.h"
#include "liberty/Utilities/CallSiteFactory.h"
#include "liberty/Utilities/CastUtil.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/StaticID.h"

#include <sstream>

namespace liberty
{
namespace SpecPriv
{

#define INST_ID_BOUND ( ((uint32_t)1<<20) - 1 )

using namespace llvm;

char HeapProfile::ID = 0;
static RegisterPass<HeapProfile> RP("heap-classify-profile", "Insert instrumentation for heap profiling", false, false);

static cl::opt<std::string> TargetFcn(
  "hcp-target-fcn", cl::init(""), cl::NotHidden,
  cl::desc("Target Function"));

static cl::opt<std::string> TargetLoop(
  "hcp-target-loop", cl::init(""), cl::NotHidden,
  cl::desc("Target Loop"));

cl::opt<std::string> hcpoutfile(
  "hcp-outfile", cl::init("result.heap.profile"), cl::NotHidden,
  cl::desc("Output file name"));

static bool isMemallocOp(Instruction* inst)
{
  CallSite cs = getCallSite(inst);

  if ( SpecPriv::Indeterminate::isMallocOrCalloc(cs) || SpecPriv::Indeterminate::isRealloc(cs) )
    return true;

  return false;
}

void HeapProfile::getAnalysisUsage(AnalysisUsage& au) const
{
  au.addRequired< DataLayout >();
  au.addRequired< StaticID >();
  au.addRequired< ModuleLoops >();
  au.addRequired< Preprocess >();

  au.setPreservesAll();
}

void HeapProfile::collectStageInfo(Module& m, Loop* loop)
{
  Preprocess& preprocess = getAnalysis< Preprocess >();
  StaticID&   sid = getAnalysis<StaticID>();

  for (Loop::block_iterator bi = loop->block_begin() ; bi != loop->block_end() ; bi++)
  {
    BasicBlock* bb = *bi;

    for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
    {
      Instruction* inst = &*ii;

      uint32_t id = sid.getID(inst);

      if ( !id )
      {
        errs() << "Error: inst has no statid id: "; inst->dump();
        assert(false);
      }

      if ( !isa<LoadInst>(inst) && !isa<StoreInst>(inst) && !isa<CallInst>(inst) && !isa<InvokeInst>(inst) )
        continue;

      std::vector<unsigned> stages;
      preprocess.getExecutingStages(inst, stages);

      if ( stages.empty() )
      {
        errs() << "Error: instruction is not executed in any stage - "; inst->dump();
        assert(false);
      }

      uint8_t sign = 0;
      for (unsigned i = 0 ; i < stages.size() ; i++)
      {
        unsigned stage = stages[i];
      
        // TODO: For now, there shouldn't be more than 5 stages
        assert( stage < 5 );

        // last 3 bits are preserved for other purpose
        sign |= ( 1 << (3 + stage) );
      }

      inst2stagesign[id] = sign;
    }
  }
}

bool HeapProfile::runOnModule(Module& m)
{
  LLVMContext& ctxt = m.getContext();
  StaticID&    sid = getAnalysis<StaticID>();

  // frequently used types

  Void = Type::getVoidTy(ctxt);
  U8 = Type::getInt8Ty(ctxt);
  I32 = Type::getInt32Ty(ctxt);
  I64 = Type::getInt64Ty(ctxt);
  I8Ptr = Type::getInt8PtrTy(ctxt);
  VoidPtr = PointerType::getUnqual( U8 );

  if ( !findTarget(m) )
  {
    errs() << "HeapProfile: Failed to find the target fcn/loop\n";
    return false;
  }

  // collect stage information for each loop instructions

  collectStageInfo(m, this->target_loop);

  // collect memalloc calls before instrumentation

  std::vector<Instruction*> all_memalloc_ops;
  for(Module::iterator mi = m.begin() ; mi != m.end() ; mi++)
  {
    Function* fcn = &*mi;

    for(inst_iterator ii = inst_begin(fcn) ; ii != inst_end(fcn) ; ii++)
    {
      Instruction *inst = &*ii;
      if( isMemallocOp(inst) )
        all_memalloc_ops.push_back( inst );
    }
  }

  replaceExternalFunctionCalls(m);

  instrumentConstructor(m);
  instrumentDestructor(m);

  instrumentLoopStartStop(m, this->target_loop);

  instrumentInstructions(m, this->target_loop);

  // instrument memalloc calls
  {
    Function* push = cast<Function>( m.getOrInsertFunction("HP_push_alloc_id", Void, I32, (Type*)0) ); 
    Function* pop = cast<Function>( m.getOrInsertFunction("HP_pop_alloc_id", Void, I32, (Type*)0) ); 

    for (unsigned i = 0 ; i < all_memalloc_ops.size() ; i++)
    {
      Instruction* inst = all_memalloc_ops[i];
      uint32_t     id = sid.getID( inst );
      assert( id );

      vector<Value*> args;
      args.push_back( ConstantInt::get(I32, id) );

      InstInsertPt pt = InstInsertPt::Before(inst);
      pt << CallInst::Create(push, args);

      if ( InvokeInst* ivi = dyn_cast<InvokeInst>(inst) )
      {
        BasicBlock* normal = ivi->getNormalDest();
        pt = InstInsertPt::Before(normal->getFirstNonPHI());
        pt << CallInst::Create(pop, args);

        pt = InstInsertPt::After(ivi->getLandingPadInst());
        pt << CallInst::Create(pop, args);
      }
      else
      {
        pt = InstInsertPt::After(inst);
        pt << CallInst::Create(pop, args);
      }
    }
  }

  // insert implementations for runtime wrapper functions, which calls the binary standard function
  addWrapperImplementations(m); 

  // debugpass
  /* 
  {
    for(Module::iterator mi = m.begin() ; mi != m.end() ; mi++)
    {
      Function* fcn = &*mi;

      for(Function::iterator bi = fcn->begin() ; bi != fcn->end() ; bi++)
      {
        Instruction* inst = (*bi).getFirstNonPHI();
        InstInsertPt pt;
        if ( isa<LandingPadInst>(inst) )
          pt = InstInsertPt::After(inst);
        else
          pt = InstInsertPt::Before(inst);

        insertPrintf(pt, fcn->getName().str() + "::" + (*bi).getName().str()+"\n", true);
      }
    }
  }
  */

  return true;
}

bool HeapProfile::findTarget(Module& m)
{
  ModuleLoops& mloops = getAnalysis<ModuleLoops>();
  bool         found = false;

  for (Module::iterator fi = m.begin() ; fi != m.end() ; fi++)
  {
    Function* f = &*fi;

    if ( f->getName().str() == TargetFcn )
    {
      BasicBlock* header = NULL;

      for (Function::iterator bi = f->begin() ; bi != f->end() ; bi++)
      {
        if ( (*bi).getName().str() == TargetLoop )
        {
          header = &*bi;
          break;
        }
      }

      if (header == NULL)
        break;

      LoopInfo& loopinfo = mloops.getAnalysis_LoopInfo(f);
      
      this->target_loop = loopinfo.getLoopFor(header);

      if ( !this->target_loop )
        break;

      this->target_fn = f;
      found = true;
    }
  }

  return found;
}

void HeapProfile::replaceExternalFunctionCalls(Module& m)
{
  // initialize a set of external function names
  set<string> externs;
  for (unsigned i = 0, e = sizeof(externs_str) / sizeof(externs_str[0]) ; i < e ; i++)
    externs.insert( externs_str[i] );

  // initialize a set of external functions not to be implemented
  set<string> ignores;
  for (unsigned i = 0, e = sizeof(ignore_externs_str) / sizeof(ignore_externs_str[0]) ; i < e ; i++)
    ignores.insert( ignore_externs_str[i] );

  vector<Function*> funcs;

  for (Module::iterator fi = m.begin(), fe = m.end() ; fi != fe ; fi++)
  {
    Function* func = &*fi;

    // only external functions are of interest
    if ( !func->isDeclaration() )
      continue;

    // filter functions to ignore
    if ( ignores.find(func->getName()) != ignores.end() )
      continue;

    if ( func->isIntrinsic() )
    {
      // just confirm that all uses is an intrinsic instruction
      for (Value::user_iterator ui = func->user_begin() ; ui != func->user_end() ; ui++)
        assert( isa<IntrinsicInst>(*ui) );
      continue;
    }

    funcs.push_back(func);
  }
  
  for (unsigned i = 0 ; i < funcs.size() ; i++)
  {
    Function* func = funcs[i];
    string    name = func->getName();

    if ( externs.find(name) == externs.end() )
    {
      DEBUG( errs() << "WARNING: Wrapper for external function " << name << " not implemented.\n" );
    }
    else
    {
      // register wrapper function
      if ( name == "_Znwm" || name == "_Znam" )
        name = "malloc";
      else if ( name == "_ZdlPv" || name == "_ZdaPv" )
        name = "free";

      string    wrapper_name = "HP_" + name;
      Function* wrapper = cast<Function>( m.getOrInsertFunction(wrapper_name, func->getFunctionType() ) );

      // replace 'func' to 'wrapper' in uses
      func->replaceAllUsesWith(wrapper); 
    }
  }
}

void HeapProfile::instrumentConstructor(Module& m)
{
  StaticID& sid = getAnalysis<StaticID>();

  LLVMContext& c = m.getContext();
  Function* ctor = cast<Function>( m.getOrInsertFunction( "___HP_ctor", Void, (Type*)0 ) );
  BasicBlock* entry = BasicBlock::Create(c, "entry", ctor, NULL);
  ReturnInst::Create(c, entry);
  callBeforeMain(ctor, 65534);

  // call HP_init function 

  Function* init = cast<Function>( m.getOrInsertFunction( "HP_init", Void, I32, I32, (Type*)0) );
  Value*    args[] = { 
      ConstantInt::get(I32, sid.getID(this->target_fn)), 
      ConstantInt::get(I32, sid.getID(this->target_loop->getHeader())) 
  };
  CallInst::Create(init, args, "", entry->getTerminator());
}

void HeapProfile::instrumentDestructor(Module& m)
{
  LLVMContext& c = m.getContext();
  Function* dtor = cast<Function>( m.getOrInsertFunction( "___HP_dtor", Void, (Type*)0 ) );
  BasicBlock* entry = BasicBlock::Create(c, "entry", dtor, NULL);
  ReturnInst::Create(c, entry);
  callAfterMain(dtor, 65534);

  // call HP_fini function 
  Function* fini = cast<Function>( m.getOrInsertFunction( "HP_fini", Void, I8Ptr, (Type*)0 ) );
  Constant* filename = getStringLiteralExpression(m, hcpoutfile);
  Value*    args[] = { filename };

  CallInst::Create(fini, args, "", entry->getTerminator());
}

void HeapProfile::instrumentLoopStartStop(Module& m, Loop* loop)
{
  BasicBlock* header = loop->getHeader();
  BasicBlock* latch = loop->getLoopLatch();

  // check if loop-simplify pass executed
  assert( loop->getNumBackEdges() == 1 && "Should be only 1 back edge, loop-simplify?");
  assert( latch && "Loop latch needs to exist, loop-simplify?");

  // add instrumentation on loop header:
  // if new invocation, call HP_loop_invocation, else, call HP_loop_iteration

  Function* f_loop_invoke = cast<Function>( m.getOrInsertFunction("HP_loop_invocation", Void, (Type*)0) );
  Function* f_loop_iter = cast<Function>( m.getOrInsertFunction("HP_loop_iteration", Void, (Type*)0) );
  Function* f_loop_exit = cast<Function>( m.getOrInsertFunction("HP_loop_exit", Void, (Type*)0) );
  PHINode*  funcphi = PHINode::Create(f_loop_invoke->getType(), 2, "funcphi", header->getFirstNonPHI());

  for (pred_iterator pi = pred_begin(header) ; pi != pred_end(header) ; pi++) 
  {
    if (*pi == latch)
      funcphi->addIncoming(f_loop_iter, *pi);
    else
      funcphi->addIncoming(f_loop_invoke, *pi);
  }

  CallInst::Create(funcphi, "", header->getFirstNonPHI());

  SmallVector<BasicBlock*, 8> exits;
  loop->getExitBlocks(exits);

  std::set<BasicBlock*> unique_exits(exits.begin(), exits.end());

  for (std::set<BasicBlock*>::iterator si = unique_exits.begin() ; si != unique_exits.end() ; si++)
  {
    CallInst::Create(f_loop_exit, "", (*si)->getFirstNonPHI());
  }
}


void HeapProfile::instrumentInstructions(Module& m, Loop* loop)
{
  StaticID& sid = getAnalysis<StaticID>();
  
  // collect loop instructions

  set<Instruction*> loopinsts;

  for (Loop::block_iterator bi = loop->block_begin() ; bi != loop->block_end() ; bi++)
    for (BasicBlock::iterator ii = (*bi)->begin() ; ii != (*bi)->end() ; ii++)
      loopinsts.insert(&*ii);

  for (Module::iterator fi = m.begin() ; fi != m.end() ; fi++)
  {
    if ( (*fi).isDeclaration() )
      continue;

    for (inst_iterator ii = inst_begin(&*fi) ; ii != inst_end(&*fi) ; ii++)
    {
      if ( MemIntrinsic* mi = dyn_cast<MemIntrinsic>(&*ii) )
      {
        instrumentMemIntrinsics(m, mi);
      }
      else if ( loopinsts.find(&*ii) != loopinsts.end() )
      {
        instrumentLoopInst(m, &*ii, sid.getID(&*ii));
      }
      else
      {
        instrumentExtInst(m, &*ii, sid.getID(&*ii));
      }
    }
  }
}

int HeapProfile::getIndex(PointerType* ty, size_t& size)
{
  DataLayout& td = getAnalysis<DataLayout>();
  int         i = td.getTypeStoreSizeInBits( ty->getElementType() );

  switch (i)
  {
    case 8:
      return 0;  
    case 16:
      return 1;  
    case 32:
      return 2;  
    case 64:
      return 3;  
    default:
      size = i / 8;
      return 4;
  }
}

void HeapProfile::instrumentMemIntrinsics(Module& m, MemIntrinsic* mi)
{
  CallSite cs(mi);
  const Function* callee = cs.getCalledFunction();
  assert(callee);
  string callee_name = callee->getName();

  // add intrinsic handlers

  Type* mi_param_types_a[] = { I8Ptr, I8Ptr, I32 }; 
  Type* mi_param_types_b[] = { I8Ptr, I8Ptr, I64 }; 
  Type* mi_param_types_c[] = { I8Ptr, I32 }; 
  Type* mi_param_types_d[] = { I8Ptr, I64 }; 

  FunctionType* mi_fty_a = FunctionType::get(Void, mi_param_types_a, false);
  FunctionType* mi_fty_b = FunctionType::get(Void, mi_param_types_b, false);
  FunctionType* mi_fty_c = FunctionType::get(Void, mi_param_types_c, false);
  FunctionType* mi_fty_d = FunctionType::get(Void, mi_param_types_d, false);

  m.getOrInsertFunction("HP_llvm_memcpy_p0i8_p0i8_i32", mi_fty_a);
  m.getOrInsertFunction("HP_llvm_memcpy_p0i8_p0i8_i64", mi_fty_b);

  m.getOrInsertFunction("HP_llvm_memmove_p0i8_p0i8_i32", mi_fty_a);
  m.getOrInsertFunction("HP_llvm_memmove_p0i8_p0i8_i64", mi_fty_b);

  m.getOrInsertFunction("HP_llvm_memset_p0i8_i32", mi_fty_c);
  m.getOrInsertFunction("HP_llvm_memset_p0i8_i64", mi_fty_d);

  if( callee_name == "llvm.memcpy.p0i8.p0i8.i32" || callee_name == "llvm.memcpy.p0i8.p0i8.i64"
      ||  callee_name == "llvm.memmove.p0i8.p0i8.i32" ||  callee_name == "llvm.memmove.p0i8.p0i8.i64"
      ||  callee_name == "llvm.memset.p0i8.i32" ||  callee_name == "llvm.memset.p0i8.i64" )
  {
    // good
  }
  else
  {
    assert(false && "Unknown memory intrinsic");
  }

  // get corresponding heap profile runtime function by manipulating callee_name
  
  ostringstream name;
  name << "HP_";
  for(unsigned i = 0 ; i < callee_name.size() ; i++)
  {
    if( callee_name[i] == '.' )
      name << '_';
    else
      name << callee_name[i];
  }
  Function* fcn = m.getFunction( name.str() );

  // set parameters

  vector<Value*> args;

  if ( callee_name.find("memset") != string::npos )
  {
    // memset
    args.push_back( cs.getArgument(0) );
    args.push_back( cs.getArgument(2) );
  }
  else
  {
    // memcpy and memmove
    args.push_back( cs.getArgument(0) );
    args.push_back( cs.getArgument(1) );
    args.push_back( cs.getArgument(2) );
  }

  CallInst::Create(fcn, args, "", mi);
}

void HeapProfile::instrumentLoopInst(Module& m, Instruction* inst, uint32_t id)
{
  assert( id < INST_ID_BOUND );

  if (id == 0) // instrumented instructions
    return;

  // --- loads

  string lf_name[] = { "HP_load1", "HP_load2", "HP_load4", "HP_load8", "HP_loadn" };
  vector<Function*> lf(5); 

  for (unsigned i = 0 ; i < 4; i++)
  {
    lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, VoidPtr, U8, (Type*)0) ); 
  }
  lf[4] = cast<Function>( m.getOrInsertFunction(lf_name[4], Void, VoidPtr, I64, U8, (Type*)0) ); 

  // --- stores

  string sf_name[] = { "HP_store1", "HP_store2", "HP_store4", "HP_store8", "HP_storen" };
  vector<Function*> sf(5); 

  for (unsigned i = 0 ; i < 4; i++)
  {
    sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, VoidPtr, U8, (Type*)0) ); 
  }
  sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, VoidPtr, I64, U8, (Type*)0) ); 

  // --- calls

  Function* push = cast<Function>( m.getOrInsertFunction("HP_push", Void, I32, (Type*)0) ); 
  Function* pop = cast<Function>( m.getOrInsertFunction("HP_pop", Void, (Type*)0) ); 

  Function* push_stage_sign = cast<Function>( m.getOrInsertFunction("HP_push_stage_sign", Void, U8, (Type*)0) ); 
  Function* pop_stage_sign = cast<Function>( m.getOrInsertFunction("HP_pop_stage_sign", Void, (Type*)0) ); 

  if ( LoadInst* li = dyn_cast<LoadInst>(inst) )
  {
    InstInsertPt pt = InstInsertPt::After(li);

    Value*         ptr = li->getPointerOperand();
    vector<Value*> args;

    args.push_back( castPtrToVoidPtr(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size );

    if (index == 4)
    {
      args.push_back( ConstantInt::get(I64, size) );
    }

    assert( inst2stagesign.count(id) );
    args.push_back( ConstantInt::get(U8, inst2stagesign[id]) );

    pt << CallInst::Create(lf[index], args);
  }
  else if ( StoreInst* si = dyn_cast<StoreInst>(inst) )
  {
    InstInsertPt pt = InstInsertPt::Before(si);

    Value*         ptr = si->getPointerOperand();
    vector<Value*> args;

    args.push_back( castPtrToVoidPtr(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size );

    if (index == 4)
    {
      args.push_back( ConstantInt::get(I64, size) );
    }

    assert( inst2stagesign.count(id) );
    args.push_back( ConstantInt::get(U8, inst2stagesign[id]) );

    pt << CallInst::Create(sf[index], args);
  }
  else if ( CallInst* ci = dyn_cast<CallInst>(inst) )
  {
    vector<Value*> args;

    args.push_back( ConstantInt::get(I32, id) );

    InstInsertPt pt = InstInsertPt::Before(ci);
    pt << CallInst::Create(push, args);
    pt = InstInsertPt::After(ci);
    pt << CallInst::Create(pop);

    args.clear();
    assert( inst2stagesign.count(id) );
    args.push_back( ConstantInt::get(U8, inst2stagesign[id]) );

    pt = InstInsertPt::Before(ci);
    pt << CallInst::Create(push_stage_sign, args);
    pt = InstInsertPt::After(ci);
    pt << CallInst::Create(pop_stage_sign);
  }
  else if ( InvokeInst* ivi = dyn_cast<InvokeInst>(inst) )
  {
    assert(false && "I hate invokeinst. Handle this case. Be sure to put pop instructions at the correct place\n");
  }
}

void HeapProfile::instrumentExtInst(Module& m, Instruction* inst, uint32_t id)
{
  // --- loads

  string lf_name[] = { "HP_load1_ext", "HP_load2_ext", "HP_load4_ext", "HP_load8_ext", "HP_loadn_ext" };
  vector<Function*> lf(5); 

  for (unsigned i = 0 ; i < 5; i++)
  {
    lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, I64, I32, I64, (Type*)0) ); 
  }

  // --- stores

  string sf_name[] = { "HP_store1_ext", "HP_store2_ext", "HP_store4_ext", "HP_store8_ext", "HP_storen_ext" };
  vector<Function*> sf(5); 

  for (unsigned i = 0 ; i < 4; i++)
  {
    sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, I64, I32, (Type*)0) ); 
  }
  sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, I64, I32, I64, (Type*)0) ); 

  if ( LoadInst* li = dyn_cast<LoadInst>(inst) )
  {
    InstInsertPt pt = InstInsertPt::After(li);

    Value*         ptr = li->getPointerOperand();
    vector<Value*> args;

    args.push_back( castToInt64Ty(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size );

    if (index == 4)
    {
      args.push_back( ConstantInt::get(I32, id) );
      args.push_back( ConstantInt::get(I64, size) );
    }
    else
    {
      args.push_back( ConstantInt::get(I32, id) );
      args.push_back( castToInt64Ty(li, pt) );
    }

    pt << CallInst::Create(lf[index], args);
  }
  else if ( StoreInst* si = dyn_cast<StoreInst>(inst) )
  {
    InstInsertPt pt = InstInsertPt::Before(si);

    Value*         ptr = si->getPointerOperand();
    vector<Value*> args;

    args.push_back( castToInt64Ty(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size );

    args.push_back( ConstantInt::get(I32, id) );

    if (index == 4)
    {
      args.push_back( ConstantInt::get(I64, size) );
    }

    pt << CallInst::Create(sf[index], args);
  }
}

void HeapProfile::addWrapperImplementations(Module& m)
{
  LLVMContext& c = m.getContext();
  vector<Value*> args;

  // --- HP___errno_location
  Function* f0 = cast<Function>( m.getOrInsertFunction("HP___errno_location", I32->getPointerTo(), (Type*)0) );
  BasicBlock* entry = BasicBlock::Create(c, "entry", f0, NULL);
  Function* c0 = cast<Function>( m.getOrInsertFunction("__errno_location", I32->getPointerTo(), (Type*)0) );
  InstInsertPt pt = InstInsertPt::Beginning(entry);
  CallInst* ci = CallInst::Create(c0, "");
  pt << ci;
  pt << ReturnInst::Create(c, ci);
}

}
}
#endif
