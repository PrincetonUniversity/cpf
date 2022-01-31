//===- SLAMP.cpp - Insert SLAMP instrumentation -----------===//
//
// Single Loop Aware Memory Profiler.
//

#define DEBUG_TYPE "SLAMP"

#include "liberty/SLAMP/SLAMP.h"
#include "liberty/SLAMP/externs.h"

#include "llvm/IR/InlineAsm.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstIterator.h"

#include "liberty/Utilities/CastUtil.h"
#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"

#include <sstream>
#include <vector>
#include <map>

#define INST_ID_BOUND ( ((uint32_t)1<<20) - 1 )

using namespace std;
using namespace llvm;

namespace liberty
{
namespace slamp
{

char SLAMP::ID = 0;
static RegisterPass<SLAMP> RP("slamp-insts", "Insert instrumentation for SLAMP profiling", false, false);

static cl::opt<std::string> TargetFcn(
  "slamp-target-fn", cl::init(""), cl::NotHidden,
  cl::desc("Target Function"));

static cl::opt<std::string> TargetLoop(
  "slamp-target-loop", cl::init(""), cl::NotHidden,
  cl::desc("Target Loop"));

cl::opt<std::string> outfile(
  "slamp-outfile", cl::init("result.slamp.profile"), cl::NotHidden,
  cl::desc("Output file name"));

SLAMP::SLAMP() : ModulePass(ID)
{
}

SLAMP::~SLAMP()
{
}

void SLAMP::getAnalysisUsage(AnalysisUsage& au) const
{
  au.addRequired< StaticID >();
  au.addRequired< ModuleLoops >();
  au.setPreservesAll();
}

bool SLAMP::runOnModule(Module& m)
{
  LLVMContext& ctxt = m.getContext();

  // frequently used types

  Void = Type::getVoidTy(ctxt);
  I32 = Type::getInt32Ty(ctxt);
  I64 = Type::getInt64Ty(ctxt);
  I8Ptr = Type::getInt8PtrTy(ctxt);

  // find target function/loop

  if ( !findTarget(m) )
    return false;

  // check if target may call setjmp/longjmp

  if ( mayCallSetjmpLongjmp(this->target_loop) )
  {
    LLVM_DEBUG( errs() << "Warning! target loop may call setjmp/longjmp\n" );
    //return false;
  }

  // replace external function calls to wrapper function calls

  replaceExternalFunctionCalls(m);

  Function* ctor = instrumentConstructor(m);
  instrumentDestructor(m);

  instrumentGlobalVars(m, ctor);

  instrumentMainFunction(m);

  instrumentLoopStartStop(m, this->target_loop);

  instrumentInstructions(m, this->target_loop);

  // insert implementations for runtime wrapper functions, which calls the binary standard function
  addWrapperImplementations(m);

  return true;
}

bool SLAMP::findTarget(Module& m)
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

static bool is_setjmp_or_longjmp(Function* f)
{
  string name = f->getName().str();
  if ( name == "_setjmp" || name == "longjmp" )
    return true;
  else
    return false;
}

bool SLAMP::mayCallSetjmpLongjmp(Loop* loop)
{
  set<Function*> callables;
  getCallableFunctions(loop, callables);

  return ( find_if(callables.begin(), callables.end(), is_setjmp_or_longjmp) != callables.end() );
}

void SLAMP::getCallableFunctions(Loop* loop, set<Function*>& callables)
{
  for (Loop::block_iterator bi = loop->block_begin() ; bi != loop->block_end() ; bi++)
  {
    BasicBlock* bb = *bi;
    for (BasicBlock::iterator ii = bb->begin() ; ii != bb->end() ; ii++)
    {
      CallInst* ci = dyn_cast<CallInst>(&*ii);
      if (!ci)
        continue;
      getCallableFunctions(ci, callables);
    }
  }
}

void SLAMP::getCallableFunctions(CallInst* ci, set<Function*>& callables)
{
  Function* called_fn = ci->getCalledFunction();
  if (called_fn == NULL)
  {
    // analyze indirect function call */
    set<Function*> targets;

    // get functions callable by given callinst
    getFunctionsWithSign(ci, targets);

    // check matched functions
    for (set<Function*>::iterator fi = targets.begin() ; fi != targets.end() ; fi++)
    {
      if ( callables.find(*fi) == callables.end() )
      {
        callables.insert(*fi);
        getCallableFunctions(*fi, callables);
      }
    }
  }
  else
  {
    if (callables.find(called_fn) == callables.end())
    {
      callables.insert(called_fn);
      getCallableFunctions(called_fn, callables);
    }
  }
}

void SLAMP::getCallableFunctions(Function* f, set<Function*>& callables)
{
  for (inst_iterator ii = inst_begin(f) ; ii != inst_end(f) ; ii++)
  {
    CallInst* ci = dyn_cast<CallInst>(&*ii);
    if (!ci)
      continue;
    getCallableFunctions(ci, callables);
  }
}

void SLAMP::getFunctionsWithSign(CallInst* ci, set<Function*> matched)
{
  Module* m = ci->getParent()->getParent()->getParent();
  CallSite cs(ci);

  for (Module::iterator fi = m->begin(), fe = m->end() ; fi != fe ; fi++)
  {
    Function* func = &*fi;

    bool found = true;
    // compare signature
    if (func->isVarArg())
    {
      if (func->arg_size() > cs.arg_size())
        found = false;
    }
    else
    {
      if (func->arg_size() != cs.arg_size())
        found = false;
    }

    if (found)
    {
      Function::arg_iterator fai;
      CallSite::arg_iterator cai;
      for (fai = func->arg_begin(), cai = cs.arg_begin() ; fai != func->arg_end() ; fai++, cai++)
      {
        Value* af = &*fai;
        Value* ac = *cai;
        if (af->getType() != ac->getType())
        {
          found = false;
          break;
        }
      }
    }

    if (found)
      matched.insert(func);
  }
}

void SLAMP::replaceExternalFunctionCalls(Module& m)
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
      errs() << name << " wrapper not implemented \n";
      assert(false && "Wrapper for external function not implemented.\n");
      LLVM_DEBUG( errs() << "WARNING: Wrapper for external function " << name << " not implemented.\n" );
    }
    else
    {
      string    wrapper_name = "SLAMP_" + name;
      /* Function* wrapper = cast<Function>( m.getOrInsertFunction(wrapper_name, func->getFunctionType() ) ); */
      FunctionCallee wrapper = m.getOrInsertFunction(wrapper_name, func->getFunctionType() );

      // replace 'func' to 'wrapper' in uses
      func->replaceAllUsesWith(wrapper.getCallee());
    }
  }
}

/// Create a function `__SLAMP_ctor` that calls `SLAMP_init` and `SLAMP_init_global_vars`
Function* SLAMP::instrumentConstructor(Module& m)
{
  //StaticID& sid = getAnalysis<StaticID>();
  sid = &getAnalysis<StaticID>();

  LLVMContext& c = m.getContext();
  //sot
  //Function* ctor = cast<Function>( m.getOrInsertFunction( "___SLAMP_ctor", Void, (Type*)0 ) );
  Function* ctor = cast<Function>( m.getOrInsertFunction( "___SLAMP_ctor", Void).getCallee() );
  BasicBlock* entry = BasicBlock::Create(c, "entry", ctor, NULL);
  ReturnInst::Create(c, entry);
  callBeforeMain(ctor, 65534);

  // call SLAMP_init function

  //Function* init = cast<Function>( m.getOrInsertFunction( "SLAMP_init", Void, I32, I32, (Type*)0) );
  Function* init = cast<Function>( m.getOrInsertFunction( "SLAMP_init", Void, I32, I32).getCallee() );
  Value*    args[] = {
      ConstantInt::get(I32, sid->getID(this->target_fn)),
      ConstantInt::get(I32, sid->getID(this->target_loop->getHeader()))
  };
  CallInst::Create(init, args, "", entry->getTerminator());

  return ctor;
}

void SLAMP::instrumentDestructor(Module& m)
{
  LLVMContext& c = m.getContext();
  //Function* dtor = cast<Function>( m.getOrInsertFunction( "___SLAMP_dtor", Void, (Type*)0 ) );
  Function* dtor = cast<Function>( m.getOrInsertFunction( "___SLAMP_dtor", Void).getCallee() );
  BasicBlock* entry = BasicBlock::Create(c, "entry", dtor, NULL);
  ReturnInst::Create(c, entry);
  callAfterMain(dtor, 65534);

  // call SLAMP_fini function
  //Function* fini = cast<Function>( m.getOrInsertFunction( "SLAMP_fini", Void, I8Ptr, (Type*)0 ) );
  Function* fini = cast<Function>( m.getOrInsertFunction( "SLAMP_fini", Void, I8Ptr).getCallee() );
  Constant* filename = getStringLiteralExpression(m, outfile);
  Value*    args[] = { filename };

  CallInst::Create(fini, args, "", entry->getTerminator());
}

/// Go through all global variables and call `SLAMP_init_global_vars`
void SLAMP::instrumentGlobalVars(Module& m, Function* ctor)
{
  //DataLayout& td = getAnalysis<DataLayout>();
  const DataLayout& td = m.getDataLayout();
  BasicBlock* entry = &( ctor->getEntryBlock() );

  // call SLAMP_init_global_vars function to initialize shadow memory for global variables

  //Function* init_gvars = cast<Function>( m.getOrInsertFunction( "SLAMP_init_global_vars", Void, I64, I64, (Type*)0 ) );
  Function* init_gvars = cast<Function>( m.getOrInsertFunction( "SLAMP_init_global_vars", Void, I64, I64).getCallee() );

  for (Module::global_iterator gi = m.global_begin() ; gi != m.global_end() ; gi++)
  {
    GlobalVariable* gv = &*gi;

    if (gv->getName() == "llvm.global_ctors")  // explicitly skip global ctor
      continue;
    else if (gv->getName() == "llvm.global_dtors")  // explicitly skip global dtor
      continue;

    PointerType* ty = dyn_cast<PointerType>( gv->getType() );
    assert(ty);

    InstInsertPt pt = InstInsertPt::Before(entry->getTerminator());
    uint64_t     size = td.getTypeStoreSize(ty->getElementType());
    Value*       args[] = { castToInt64Ty(gv, pt), ConstantInt::get(I64, size) };
    pt << CallInst::Create(init_gvars, args);
  }

  for (Module::iterator fi = m.begin() ; fi != m.end() ; fi++) {
    Function* func = &*fi;

    if (func->isIntrinsic())
      continue;

    uint64_t  size = td.getTypeStoreSize(func->getType());

    InstInsertPt pt = InstInsertPt::Before(entry->getTerminator());
    Value*       args[] = { castToInt64Ty(func, pt), ConstantInt::get(I64, size) };
    pt << CallInst::Create(init_gvars, args);
  }

}

void SLAMP::instrumentNonStandards(Module& m, Function* ctor)
{
  // 1) handle __errno_location.
  allocErrnoLocation(m, ctor);
}

void SLAMP::allocErrnoLocation(Module& m, Function* ctor)
{
  //DataLayout&  td = getAnalysis<DataLayout>();
  const DataLayout& td = m.getDataLayout();
  LLVMContext& c = m.getContext();

  // Call dummy __errno_location to allocate a shadow memory for the location
  //Function* f = cast<Function>( m.getOrInsertFunction("SLAMP___errno_location_alloc", Void, (Type*)0) );
  Function* f = cast<Function>( m.getOrInsertFunction("SLAMP___errno_location_alloc", Void).getCallee() );

  BasicBlock* entry = BasicBlock::Create(c, "entry", f, NULL);
  //Function* c0 = cast<Function>( m.getOrInsertFunction("__errno_location", I32->getPointerTo(), (Type*)0) );
  Function* c0 = cast<Function>( m.getOrInsertFunction("__errno_location", I32->getPointerTo()).getCallee() );
  InstInsertPt pt = InstInsertPt::Beginning(entry);
  CallInst* ci = CallInst::Create(c0, "");
  pt << ci;

  // ci is a __errno_location call
  PointerType* ty = dyn_cast<PointerType>(ci->getType());
  assert(ty);
  uint64_t size = td.getTypeStoreSize(ty->getElementType());

  // reuse SLAMP_init_global_vars
  Function* init_gvars = dyn_cast<Function>(
      m.getOrInsertFunction( "SLAMP_init_global_vars", Void, I64, I64).getCallee() );
      //m.getOrInsertFunction( "SLAMP_init_global_vars", Void, I64, I64, (Type*)0 ) );
  assert(init_gvars);

  Value* args[] = { castToInt64Ty(ci, pt), ConstantInt::get(I64, size) };
  CallInst* ci2 = CallInst::Create(init_gvars, args, "");
  pt << ci2;

  pt << ReturnInst::Create(c);

  // call function f from ctor
  BasicBlock* ctor_entry = &( ctor->getEntryBlock() );
  CallInst::Create(f, "", ctor_entry->getTerminator());
}

void SLAMP::instrumentMainFunction(Module& m)
{
  for (Module::iterator fi = m.begin(), fe = m.end() ; fi != fe ; fi++)
  {
    Function* func = &*fi;
    if ( func->getName() != "main" )
      continue;

    BasicBlock* entry = &( func->getEntryBlock() );

    // if the function is a main function, add special instrumentation to handle command line arguments
    Function* f_main_entry = cast<Function>(
        m.getOrInsertFunction("SLAMP_main_entry",
          Void, I32, I8Ptr->getPointerTo(), I8Ptr->getPointerTo(), I64).getCallee() );
          //Void, I32, I8Ptr->getPointerTo(), I8Ptr->getPointerTo(), I64, (Type*)0) );

    vector<Value*> main_args;
    for (Function::arg_iterator ai = func->arg_begin() ; ai != func->arg_end() ; ai++)
      main_args.push_back(&*ai);

    // make up arguments

    if (main_args.size() != 3) // if not all of argc, argv, evn are given
    {
      Value* zeroarg = ConstantInt::get(I32, 0);
      Value* nullarg = ConstantPointerNull::get( I8Ptr->getPointerTo() );

      if (main_args.size() == 0) // no command line input
      {
        main_args.push_back(zeroarg);
        main_args.push_back(nullarg);
        main_args.push_back(nullarg);
      }
      else if (main_args.size() == 2) // only argc, argv given
      {
        main_args.push_back(nullarg);
      }
      else
        assert(false);
    }

    InstInsertPt pt;
    if ( isa<LandingPadInst>(entry->getFirstNonPHI()) )
      pt = InstInsertPt::After( entry->getFirstNonPHI() );
    else
      pt = InstInsertPt::Before( entry->getFirstNonPHI() );

    // read rsp and push it into main_args

    FunctionType* fty = FunctionType::get(I64, false);
    // get the static pointer %rsp
    InlineAsm*    get_rsp = InlineAsm::get(fty, "mov %rsp, $0;", "=r,~{dirflag},~{fpsr},~{flags}", false);

    CallInst* rsp = CallInst::Create(get_rsp, "");
    pt << rsp;
    main_args.push_back(rsp);

    pt << CallInst::Create(f_main_entry, main_args, "");
  }
}

void SLAMP::instrumentLoopStartStop(Module& m, Loop* loop)
{
  // TODO: check setjmp/longjmp

  BasicBlock* header = loop->getHeader();
  BasicBlock* latch = loop->getLoopLatch();

  // check if loop-simplify pass executed
  assert( loop->getNumBackEdges() == 1 && "Should be only 1 back edge, loop-simplify?");
  assert( latch && "Loop latch needs to exist, loop-simplify?");

  // add instrumentation on loop header:
  // if new invocation, call SLAMP_loop_invocation, else, call SLAMP_loop_iteration

  //Function* f_loop_invoke = cast<Function>( m.getOrInsertFunction("SLAMP_loop_invocation", Void, (Type*)0) );
  //Function* f_loop_iter = cast<Function>( m.getOrInsertFunction("SLAMP_loop_iteration", Void, (Type*)0) );
  //Function* f_loop_exit = cast<Function>( m.getOrInsertFunction("SLAMP_loop_exit", Void, (Type*)0) );
  Function* f_loop_invoke = cast<Function>( m.getOrInsertFunction("SLAMP_loop_invocation", Void).getCallee() );
  Function* f_loop_iter = cast<Function>( m.getOrInsertFunction("SLAMP_loop_iteration", Void).getCallee() );
  Function* f_loop_exit = cast<Function>( m.getOrInsertFunction("SLAMP_loop_exit", Void).getCallee() );

  PHINode*  funcphi = PHINode::Create(f_loop_invoke->getType(), 2, "funcphi");
  InstInsertPt pt;

  if ( isa<LandingPadInst>(header->getFirstNonPHI()) )
    pt = InstInsertPt::After( header->getFirstNonPHI() );
  else
    pt = InstInsertPt::Before( header->getFirstNonPHI() );

  pt << funcphi;

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

  // one instrumentation per block
  set<BasicBlock*> s;

  for (unsigned i = 0 ; i < exits.size() ; i++)
  {
    if (s.count(exits[i])) continue;

    CallInst* ci = CallInst::Create(f_loop_exit, "");

    InstInsertPt pt2;
    if ( isa<LandingPadInst>(exits[i]->getFirstNonPHI()) )
      pt2 = InstInsertPt::After( exits[i]->getFirstNonPHI() );
    else
      pt2 = InstInsertPt::Before( exits[i]->getFirstNonPHI() );
    pt2 << ci;

    s.insert(exits[i]);
  }
}

void SLAMP::instrumentInstructions(Module& m, Loop* loop)
{
  //StaticID& sid = getAnalysis<StaticID>();

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
      if(const auto Intrinsic = dyn_cast<IntrinsicInst>(&*ii))
      {
        const auto Id = Intrinsic->getIntrinsicID();
        if (Id == Intrinsic::lifetime_start || Id == Intrinsic::lifetime_end)
        {
          instrumentLifetimeIntrinsics(m, &*ii);
          continue;
        }
      }

      if ( MemIntrinsic* mi = dyn_cast<MemIntrinsic>(&*ii) )
      {
        instrumentMemIntrinsics(m, mi);
      }
      else if ( loopinsts.find(&*ii) != loopinsts.end() )
      {
        instrumentLoopInst(m, &*ii, sid->getID(&*ii));
      }
      else
      {
        //instrumentExtInst(m, &*ii, sid.getFuncLocalIDWithInst(&*fi, &*ii));
        instrumentExtInst(m, &*ii, sid->getID(&*ii));
      }
    }
  }
}

int SLAMP::getIndex(PointerType* ty, size_t& size, const DataLayout& DL)
{
  int i = DL.getTypeStoreSizeInBits( ty->getElementType() );

  // sot: cannot convert a vector value to an int64 so just return variable size
  // n (index 4) and return the actual size, even if i is less than or equal
  // to 64.
  if (isa<VectorType>(ty->getElementType())) {
    size = i / 8;
    return 4;
  }

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

void SLAMP::instrumentMemIntrinsics(Module& m, MemIntrinsic* mi)
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

  m.getOrInsertFunction("SLAMP_llvm_memcpy_p0i8_p0i8_i32", mi_fty_a);
  m.getOrInsertFunction("SLAMP_llvm_memcpy_p0i8_p0i8_i64", mi_fty_b);

  m.getOrInsertFunction("SLAMP_llvm_memmove_p0i8_p0i8_i32", mi_fty_a);
  m.getOrInsertFunction("SLAMP_llvm_memmove_p0i8_p0i8_i64", mi_fty_b);

  m.getOrInsertFunction("SLAMP_llvm_memset_p0i8_i32", mi_fty_c);
  m.getOrInsertFunction("SLAMP_llvm_memset_p0i8_i64", mi_fty_d);

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

  // get corresponding SLAMP runtime function by manipulating callee_name

  ostringstream name;
  name << "SLAMP_";
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

void SLAMP::instrumentLifetimeIntrinsics(Module& m, Instruction* inst)
{
  CallSite cs(inst);
  const Function* callee = cs.getCalledFunction();
  assert(callee);
  string callee_name = callee->getName();

  // add intrinsic handlers

  Type* mi_param_types_a[] = { I64, I8Ptr };

  FunctionType* mi_fty_a = FunctionType::get(Void, mi_param_types_a, false);

  m.getOrInsertFunction("SLAMP_llvm_lifetime_start_p0i8", mi_fty_a);
  m.getOrInsertFunction("SLAMP_llvm_lifetime_end_p0i8", mi_fty_a);

  if( callee_name == "llvm.lifetime.start.p0i8" || callee_name == "llvm.lifetime.end.p0i8" )
  {
    // good
  }
  else
  {
    assert(false && "Unknown lifetime intrinsic");
  }

  // get corresponding SLAMP runtime function by manipulating callee_name

  ostringstream name;
  name << "SLAMP_";
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
  args.push_back( cs.getArgument(0) );
  args.push_back( cs.getArgument(1) );

  CallInst::Create(fcn, args, "", inst);

}

void SLAMP::instrumentLoopInst(Module& m, Instruction* inst, uint32_t id)
{
  const DataLayout& DL = m.getDataLayout();

  assert( id < INST_ID_BOUND );

  if (id == 0) // instrumented instructions
    return;

  // --- loads

  string lf_name[] = { "SLAMP_load1", "SLAMP_load2", "SLAMP_load4", "SLAMP_load8", "SLAMP_loadn" };
  vector<Function*> lf(5);

  for (unsigned i = 0 ; i < 5; i++)
  {
    lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, I32, I64, I32, I64).getCallee() );
    //lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, I32, I64, I32, I64, (Type*)0) );
  }

  // --- stores

  string sf_name[] = { "SLAMP_store1", "SLAMP_store2", "SLAMP_store4", "SLAMP_store8", "SLAMP_storen" };
  vector<Function*> sf(5);

  for (unsigned i = 0 ; i < 4; i++)
  {
    //sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, I32, I64, (Type*)0) );
    sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, I32, I64).getCallee() );
  }
  //sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, I32, I64, I64, (Type*)0) );
  sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, I32, I64, I64).getCallee() );

  // --- calls

  //Function* push = cast<Function>( m.getOrInsertFunction("SLAMP_push", Void, I32, (Type*)0) );
  //Function* pop = cast<Function>( m.getOrInsertFunction("SLAMP_pop", Void, (Type*)0) );
  Function* push = cast<Function>( m.getOrInsertFunction("SLAMP_push", Void, I32).getCallee() );
  Function* pop = cast<Function>( m.getOrInsertFunction("SLAMP_pop", Void).getCallee() );

  if ( LoadInst* li = dyn_cast<LoadInst>(inst) )
  {
    InstInsertPt pt = InstInsertPt::After(li);

    Value*         ptr = li->getPointerOperand();
    vector<Value*> args;

    args.push_back( ConstantInt::get(I32, id) );
    args.push_back( castToInt64Ty(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size, DL );

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
    InstInsertPt pt = InstInsertPt::After(si);

    Value*         ptr = si->getPointerOperand();
    vector<Value*> args;

    args.push_back( ConstantInt::get(I32, id) );
    args.push_back( castToInt64Ty(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size, DL );

    if (index == 4)
    {
      args.push_back( ConstantInt::get(I64, size) );
    }

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
  }
}

void SLAMP::instrumentExtInst(Module& m, Instruction* inst, uint32_t id)
{
  // --- loads

  const DataLayout& DL = m.getDataLayout();

  string lf_name[] = { "SLAMP_load1_ext", "SLAMP_load2_ext", "SLAMP_load4_ext", "SLAMP_load8_ext",
    "SLAMP_loadn_ext" };
  vector<Function*> lf(5);

  for (unsigned i = 0 ; i < 5; i++)
  {
    //lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, I64, I32, I64, (Type*)0) );
    lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, I64, I32, I64).getCallee() );
  }

  // --- stores

  string sf_name[] = { "SLAMP_store1_ext", "SLAMP_store2_ext", "SLAMP_store4_ext",
    "SLAMP_store8_ext", "SLAMP_storen_ext" };
  vector<Function*> sf(5);

  for (unsigned i = 0 ; i < 4; i++)
  {
    //sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, I64, I32, (Type*)0) );
    sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, I64, I32).getCallee() );
  }
  //sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, I64, I32, I64, (Type*)0) );
  sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, I64, I32, I64).getCallee() );

  if ( LoadInst* li = dyn_cast<LoadInst>(inst) )
  {
    InstInsertPt pt = InstInsertPt::After(li);

    Value*         ptr = li->getPointerOperand();
    vector<Value*> args;

    args.push_back( castToInt64Ty(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size, DL);

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
    InstInsertPt pt = InstInsertPt::After(si);

    Value*         ptr = si->getPointerOperand();
    vector<Value*> args;

    args.push_back( castToInt64Ty(ptr, pt) );

    size_t size;
    int    index = getIndex( cast<PointerType>( ptr->getType() ), size, DL );

    args.push_back( ConstantInt::get(I32, id) );

    if (index == 4)
    {
      args.push_back( ConstantInt::get(I64, size) );
    }

    pt << CallInst::Create(sf[index], args);
  }
}

void SLAMP::addWrapperImplementations(Module& m)
{
  LLVMContext& c = m.getContext();
  vector<Value*> args;

  // --- SLAMP___errno_location
  //Function* f0 = cast<Function>( m.getOrInsertFunction("SLAMP___errno_location", I32->getPointerTo(), (Type*)0) );
  Function* f0 = cast<Function>( m.getOrInsertFunction("SLAMP___errno_location", I32->getPointerTo()).getCallee() );
  BasicBlock* entry = BasicBlock::Create(c, "entry", f0, NULL);
  //Function* c0 = cast<Function>( m.getOrInsertFunction("__errno_location", I32->getPointerTo(), (Type*)0) );
  Function* c0 = cast<Function>( m.getOrInsertFunction("__errno_location", I32->getPointerTo()).getCallee() );
  InstInsertPt pt = InstInsertPt::Beginning(entry);
  CallInst* ci = CallInst::Create(c0, "");
  pt << ci;
  pt << ReturnInst::Create(c, ci);
}

}
}
