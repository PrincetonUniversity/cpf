#define DEBUG_TYPE "ExternICallProf"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/IndirectCallVisitor.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"

#include "scaf/Utilities/GlobalCtors.h"
#include "scaf/Utilities/InstInsertPt.h"
#include "scaf/Utilities/SplitEdge.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <list>

using namespace llvm;
using namespace std;
using namespace liberty;

#define PROF_INVOC_FUNC_NAME "__extern_icall_prof_invoc"
#define PROF_INIT_FUNC_NAME "__extern_icall_prof_init"

// Command line option to set the maximum number of indirect call targets
// for a single indirect call callsite.
static cl::opt<unsigned> MaxNumTarget(
    "extern-icall-prof-max-targets", cl::init(5), cl::Hidden, cl::ZeroOrMore,
    cl::desc("Max number of indirect call targets for a single indirect "
             "call callsite"));

namespace
{
  class ExternICallProf : public ModulePass
  {
    bool runOnModule(Module& M);
    void instrumentICall(Instruction *I, unsigned id, Module &M);


    unsigned numICall;

    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const
    {
      //AU.addRequired<LoopInfoWrapperPass>();
      AU.setPreservesAll();
    }

    StringRef getPassName() const { return "ExternICallProf"; }
    void *getAdjustedAnalysisPointer(AnalysisID PI) { return this; }

    static char ID;
    ExternICallProf() : ModulePass(ID) {}
  };
}

char ExternICallProf::ID = 0;
static RegisterPass<ExternICallProf> RP10("extern-icall-prof", "Insert external indirect call profiling instrumentation", false, false);


static std::string getICallProfGVName(unsigned id) {
  return ("extern_icall_prof_node." + Twine(id)).str();
}

static FunctionCallee getOrInsertProfilingCall(Module &M) {
  const char* InvocName = PROF_INVOC_FUNC_NAME; // Macro defined name

  auto &Ctx = M.getContext();

  auto *ReturnTy = Type::getVoidTy(Ctx);
  Type *formals[] = {
    Type::getInt8PtrTy(Ctx),
    Type::getInt8PtrTy(Ctx),
    Type::getInt32Ty(Ctx),
    Type::getInt32Ty(Ctx)
  };

  auto *FunctionTy = FunctionType::get(ReturnTy, makeArrayRef(formals), false); 
  auto InvocFcn = M.getOrInsertFunction(InvocName, FunctionTy);

  return InvocFcn;
}
/*
 * Step 1: Create a global struct { total_target_cnt = 0,  [ (value, cnt,  Dl_info*) * MaxNumTarget ] 
 * Step 2: Add a call to "__extern_icall_prof_invoc(void * fn_ptr, void * global struct, i32 maxnumtarget)
 *
 */
void ExternICallProf::instrumentICall(Instruction *I, unsigned id, Module &M){
  CallSite CS(I);
  Value *Callee = CS.getCalledValue(); // get the called pointer

  IRBuilder<> Builder(I);

  LLVMContext &Ctx = M.getContext();
  
  // 1. Create a new global struct for the id
  Type *CounterNodeTypes[] = {
    Type::getInt8PtrTy(Ctx), // function ptr
    Type::getInt32Ty(Ctx), // counter
    Type::getInt8PtrTy(Ctx) // (void *) (Dl_info *) dlinfo
  };

  // [ (value, cnt, DL_info*) * MaxNumTarget ]
  auto *CounterNodeTy = StructType::get(Ctx, makeArrayRef(CounterNodeTypes)); 
  auto *CounterTy = ArrayType::get(CounterNodeTy, MaxNumTarget);

  Type *NodeType[] = {
    Type::getInt32Ty(Ctx),
    CounterTy
  };

  auto *NodeTy = StructType::get(Ctx, makeArrayRef(NodeType));

  auto *gv = new GlobalVariable(M, 
      NodeTy,
      false, // isConstant
      GlobalValue::CommonLinkage, // linkage
      Constant::getNullValue(NodeTy), // initializer
      getICallProfGVName(id) // name
      );

  gv->setAlignment(8);

  // 2. add a call to __extern_icall_prof_invoc(fn_ptr, gv_ptr, maxnumtarget)
  Builder.CreateCall(
      getOrInsertProfilingCall(M),
      {Builder.CreateBitCast(Callee, Builder.getInt8PtrTy()),
       ConstantExpr::getBitCast(gv, Type::getInt8PtrTy(Ctx)),
       Builder.getInt32(id),
       Builder.getInt32(MaxNumTarget)});
}


bool ExternICallProf::runOnModule(Module& M)
{
  numICall = 0;

  // Go through and instrument each function
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    std::vector<Instruction *> icalls = findIndirectCalls(F); 
    // instrument all the indirect call site
    for (auto &I : icalls){
      instrumentICall(I, numICall, M);
      numICall++;
    }
  }

  // The initializer will register the file and dump profiled result
  FunctionCallee wrapper_InitFn =  M.getOrInsertFunction(PROF_INIT_FUNC_NAME,
      Type::getVoidTy(M.getContext()),
      Type::getInt32Ty(M.getContext()),
      Type::getInt32Ty(M.getContext()));

  Constant *InitFn = cast<Constant>(wrapper_InitFn.getCallee());

  std::vector<Value*> Args(2);
  Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numICall, false);
  Args[1] = ConstantInt::get(Type::getInt32Ty(M.getContext()), MaxNumTarget, false);

  // Create the GlobalCtor function
  std::vector<Type*>FuncTy_0_args;
  FunctionType* FuncTy_0 = FunctionType::get(
      /*Result=*/Type::getVoidTy( M.getContext() ),
      /*Params=*/FuncTy_0_args,
      /*isVarArg=*/false);

  Function* func_initor = Function::Create(
      /*Type=*/FuncTy_0,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Name=*/"prof_initor", &M);

  BasicBlock *initor_entry = BasicBlock::Create(M.getContext(), "entry", func_initor,0);
  CallInst::Create(InitFn, Args, "", initor_entry);
  ReturnInst::Create(M.getContext(), initor_entry);

  // Function has been created, now add it to the global ctor list
  callBeforeMain(func_initor, 0);

  if (numICall > 0)
    return true;
  else
    return false;
}



#undef DEBUG_TYPE
