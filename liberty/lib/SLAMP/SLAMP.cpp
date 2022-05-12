//===- SLAMP.cpp - Insert SLAMP instrumentation -----------===//
//
// Single Loop Aware Memory Profiler.
//

#define DEBUG_TYPE "SLAMP"

#define USE_PDG

#ifdef USE_PDG
#include "scaf/SpeculationModules/PDGBuilder.hpp"
#endif

#include "liberty/SLAMP/SLAMP.h"
#include "liberty/SLAMP/externs.h"

#include "llvm/IR/CFG.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include "liberty/Utilities/CastUtil.h"
#include "scaf/Utilities/GlobalCtors.h"
#include "scaf/Utilities/InsertPrintf.h"
#include "scaf/Utilities/InstInsertPt.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/Metadata.h"
#include "scaf/Utilities/PDGQueries.h"

#include <sstream>
#include <vector>
#include <map>

#define INST_ID_BOUND ( ((uint32_t)1<<20) - 1 )

using namespace std;
using namespace llvm;

namespace liberty::slamp {

char SLAMP::ID = 0;
static uint64_t numElidedNode = 0;
static uint64_t numInstrumentedNode = 0;
STATISTIC(numElidedNodeStats, "Number of instructions in the loop that are ignored for SLAMP due to pruning");
STATISTIC(numInstrumentedNodeStats, "Number of instructions in the loop that are instrumented ");
static RegisterPass<SLAMP> RP("slamp-insts",
                              "Insert instrumentation for SLAMP profiling",
                              false, false);

// passed in based on the target loop info
static cl::opt<std::string> TargetFcn("slamp-target-fn", cl::init(""),
                                      cl::NotHidden,
                                      cl::desc("Target Function"));

// top priority; not compatible with the rest
static cl::list<uint32_t> ExplicitInsts("slamp-explicit-insts",
                  cl::NotHidden, cl::CommaSeparated,
                  cl::desc("Explicitly instrumented instructions"),
                  cl::value_desc("inst_id"));


static cl::opt<bool> ProfileGlobals("slamp-profile-globals",
                                     cl::init(true),
                                     cl::NotHidden,
                                     cl::desc("Profile globals"));

static cl::opt<bool> IgnoreCall("slamp-ignore-call", cl::init(false),
                                       cl::NotHidden,
                                       cl::desc("Ignore dependences from call"));

static cl::opt<bool> IsDOALL("slamp-doall", cl::init(false),
                                       cl::NotHidden,
                                       cl::desc("Doall"));

// whether to turn on dependence module
static cl::opt<bool> UseDependenceModule("slamp-dependence-module", cl::init(true), cl::NotHidden, cl::desc("Use dependence module"));

// constant value module
static cl::opt<bool> UseConstantValueModule("slamp-constant-value-module", cl::init(false), cl::NotHidden, cl::desc("Use constant value module"));

// linear value module
static cl::opt<bool> UseLinearValueModule("slamp-linear-value-module", cl::init(false), cl::NotHidden, cl::desc("Use linear value module"));

// constant address module
static cl::opt<bool> UseConstantAddressModule("slamp-constant-address-module", cl::init(false), cl::NotHidden, cl::desc("Use address module"));

// linear address module
static cl::opt<bool> UseLinearAddressModule("slamp-linear-address-module", cl::init(false), cl::NotHidden, cl::desc("Use linear address module"));

// trace module
static cl::opt<bool> UseTraceModule("slamp-trace-module", cl::init(false), cl::NotHidden, cl::desc("Use trace module"));

// reason module
static cl::opt<bool> UseReasonModule("slamp-reason-module", cl::init(false), cl::NotHidden, cl::desc("Use reason module"));

// localwrite module
static cl::opt<bool> UseLocalWriteModule("slamp-localwrite-module", cl::init(false), cl::NotHidden, cl::desc("Use localwrite module"));

// localwrite mask (size_t)
static cl::opt<size_t> LocalWriteMask("slamp-localwrite-mask", cl::init(0), cl::NotHidden, cl::desc("Localwrite mask"));

// localwrite pattern (size_t)
static cl::opt<size_t> LocalWritePattern("slamp-localwrite-pattern", cl::init(0), cl::NotHidden, cl::desc("Localwrite pattern"));


static cl::opt<bool> UsePruning("slamp-pruning", cl::init(false),
                                       cl::NotHidden,
                                       cl::desc("Use PDG to pruning"));

// target instruction with metadata ID
static cl::opt<uint32_t> TargetInst("slamp-target-inst", cl::init(0),
                                       cl::NotHidden,
                                       cl::desc("Target Instruction"));

// passed in based on the target loop info
static cl::opt<std::string> TargetLoop("slamp-target-loop", cl::init(""),
                                       cl::NotHidden, cl::desc("Target Loop"));

cl::opt<std::string> outfile("slamp-outfile", cl::init("result.slamp.profile"),
                             cl::NotHidden, cl::desc("Output file name"));

SLAMP::SLAMP() : ModulePass(ID) {}

SLAMP::~SLAMP() = default;

void SLAMP::getAnalysisUsage(AnalysisUsage &au) const {
  // au.addRequired<StaticID>(); // use static ID (requires the bitcode to be exact the same)
  au.addRequired<ModuleLoops>();
#ifdef USE_PDG
  au.addRequired<LoopAA>();
  au.addRequired<PDGBuilder>();
#endif
  au.setPreservesAll();
}

static std::vector<uint32_t> elidedLoopInstsId;
// https://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector
static size_t elidedHash(std::vector<uint32_t> const& vec) {
  std::size_t seed = vec.size();
  for(auto& i : vec) {
    seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
  return seed;
}

Value * getGlobalName(GlobalVariable *gv)
{
  Module *mod = gv->getParent();
  std::string name = "global " + gv->getName().str();
  return getStringLiteralExpression( *mod, name);
}

bool SLAMP::runOnModule(Module &m) {
  LLVMContext &ctxt = m.getContext();

  // frequently used types
  Void = Type::getVoidTy(ctxt);
  I32 = Type::getInt32Ty(ctxt);
  I64 = Type::getInt64Ty(ctxt);
  I8Ptr = Type::getInt8PtrTy(ctxt);

  // find target function/loop
  if (!findTarget(m))
    return false;

  // check if target may call setjmp/longjmp
  if (mayCallSetjmpLongjmp(this->target_loop)) {
    LLVM_DEBUG(errs() << "Warning! target loop may call setjmp/longjmp\n");
    // return false;
  }

#ifdef USE_PDG
  // User set the explicit insts through slamp-explicit-insts
  if (!ExplicitInsts.empty()) {
    for (auto *BB: this->target_loop->blocks()) {
      for (Instruction &I: *BB) {
        if (!I.mayReadOrWriteMemory()) {
          continue;
        }

        auto inst_id = Namer::getInstrId(&I);

        // check if the instruction is explicitly instrumented
        if (std::find(ExplicitInsts.begin(), ExplicitInsts.end(), inst_id) != ExplicitInsts.end()) {
          numInstrumentedNode++;
        } else {
          elidedLoopInstsId.push_back(inst_id);
          elidedLoopInsts.insert(&I);
          numElidedNode++;
        }
      }
    }
  }
  // User set the targeted inst through slamp-target-inst
  else if (TargetInst != 0) {
    auto *aa = getAnalysis< LoopAA >().getTopAA();
    aa->dump();


    // find the instruction based on the metadata
    Instruction *target_inst = nullptr;
    for (auto *BB: this->target_loop->blocks()) {
      for (Instruction &I: *BB) {
        if (!I.mayReadOrWriteMemory()) {
          continue;
        }
        if (Namer::getInstrId(&I) == TargetInst) {
          target_inst = &I;
          break;
        }
      }
    }

    if (target_inst == nullptr) {
      errs() << "Error! cannot find target instruction\n";

      for (auto *BB : this->target_loop->blocks()) {
        for (Instruction &I : *BB) {
          if (!I.mayReadOrWriteMemory()) {
            continue;
          }
          elidedLoopInstsId.push_back(Namer::getInstrId(&I));
          elidedLoopInsts.insert(&I);
          numElidedNode++;
        }
      }
    } else {

      errs() << "Target ID: " << TargetInst << "\n";
      errs() << "Target instruction: " << *target_inst << "\n";

      for (auto *BB: this->target_loop->blocks()) {
        for (Instruction &I: *BB) {
          if (!I.mayReadOrWriteMemory()) {
            continue;
          }
          if (&I == target_inst) {
            numInstrumentedNode++;
            continue;
          }
          // any dependence that has a RAW dep to the target instruction should not be elided
          auto retLCFW = liberty::disproveLoopCarriedMemoryDep(target_inst, &I, 0b111, this->target_loop, aa);
          auto retLCBW = liberty::disproveLoopCarriedMemoryDep(&I, target_inst, 0b111, this->target_loop, aa);
          auto retIIFW = liberty::disproveIntraIterationMemoryDep(target_inst, &I, 0b111, this->target_loop, aa);
          auto retIIBW = liberty::disproveIntraIterationMemoryDep(&I, target_inst, 0b111, this->target_loop, aa);

          // debug
          LLVM_DEBUG(if (Namer::getInstrId(&I) == 21182) {
            Remedies remedies;
            LoopAA::ModRefResult modrefIIFW = aa->modref(
                target_inst, LoopAA::Same, &I, target_loop, remedies);
            auto modrefIIBW = aa->modref(&I, LoopAA::Same, target_inst,
                                         target_loop, remedies);
            errs() << "Target inst: " << *target_inst << "\n";
            errs() << "I: " << I << "\n";
            // convert retLCFW to int and print
            errs() << "retLCFW: " << (int)(retLCFW) << "\n";
            errs() << "retLCBW: " << (int)(retLCBW) << "\n";
            errs() << "retIIFW: " << (int)(retIIFW) << "\n";
            errs() << "retIIBW: " << (int)(retIIBW) << "\n";
            errs() << "modrefIIFW: " << (modrefIIFW) << "\n";
            errs() << "modrefIIBW: " << (modrefIIBW) << "\n";
          });

          // RAW disproved for all deps
          if ((retLCFW & 0b001) && (retLCBW & 0b001) && (retIIFW & 0b001) && (retIIBW & 0b001)) {
            elidedLoopInstsId.push_back(Namer::getInstrId(&I));
            elidedLoopInsts.insert(&I);
            numElidedNode++;
          } else {
            numInstrumentedNode++;
          }
        }
      }
    }
  }
  else if (UsePruning || IsDOALL) { // is DOALL implies use pruning
    // get PDG and prune
    auto *pdgbuilder = getAnalysisIfAvailable<PDGBuilder>();

    if (pdgbuilder) {
      auto pdg = pdgbuilder->getLoopPDG(this->target_loop);
      errs() << "Try to elide nodes "  << pdg->numNodes() << "\n";
      // go through all the nodes and see if they still have potential dependences
      for (auto node : pdg->getNodes()) {
        bool canBeElided = true;

        // have to be instruction and mayReadOrWriteMemory
        if (auto inst = dyn_cast<Instruction>(node->getT())){
          if (!inst->mayReadOrWriteMemory()) {
            continue;
          }
          if (IgnoreCall && isa<CallBase>(inst)) {
            continue;
          }
        } else {
          continue;
        }

        numInstrumentedNode++;

        if (dyn_cast<Instruction>(node->getT())->mayWriteToMemory()) {
          for (auto &edge : node->getOutgoingEdges()) {
            if (edge->isRAWDependence() && edge->isMemoryDependence()) {
              // FIXME: hack for DOALL
              if (IsDOALL && !edge->isLoopCarriedDependence()) {
                continue;
              }
              if (IgnoreCall) {
                // ignore dep to call
                if (isa<CallBase>(edge->getIncomingT())) {
                  continue;
                }
              }
              canBeElided = false;
              break;
            }
          }
        }

        if (dyn_cast<Instruction>(node->getT())->mayReadFromMemory()) {
          for (auto &edge : node->getIncomingEdges()) {
            if (edge->isRAWDependence() && edge->isMemoryDependence()) {
              // FIXME: hack for DOALL
              if (IsDOALL && !edge->isLoopCarriedDependence()) {
                continue;
              }
              if (IgnoreCall) {
                // ignore dep to call
                if (isa<CallBase>(edge->getOutgoingT())) {
                  continue;
                }
              }
              canBeElided = false;
              break;
            }
          }
        }

        if (canBeElided) {
          if (auto *inst = dyn_cast<Instruction>(node->getT())) {
            errs() << "Elided: " << *inst << "\n";
            numElidedNode++;
            numInstrumentedNode--;
            elidedLoopInsts.insert(inst);
            elidedLoopInstsId.push_back(Namer::getInstrId(inst));
          }
        }
      }
    } else {
      errs() << "PDGBuilder not added, cannot elide nodes\n";
    }
  } else {
    errs() << "No elision technique is selected\n";
    for (auto *BB : this->target_loop->blocks()) {
      for (Instruction &I : *BB) {
        if (!I.mayReadOrWriteMemory()) {
          continue;
        }
        numInstrumentedNode++;
      }
    }
  }
#endif

  numInstrumentedNodeStats = numInstrumentedNode;
  numElidedNodeStats = numElidedNode;

  errs() << "Instrumented Count: " << numInstrumentedNode << "\n";
  errs() << "Elided Count: " << numElidedNode << "\n";
  std::sort(elidedLoopInstsId.begin(), elidedLoopInstsId.end());
  errs() << "Elided Hash: " << elidedHash(elidedLoopInstsId) << "\n";

  // replace external function calls to wrapper function calls
  replaceExternalFunctionCalls(m);


  auto setGlobalModule = [&m](string name, bool value) {
    m.getOrInsertGlobal(name, Type::getInt1Ty(m.getContext()));
    GlobalVariable *module_var = m.getGlobalVariable(name);
    module_var->setInitializer(ConstantInt::get(Type::getInt1Ty(m.getContext()), value));
    module_var->setConstant(true);
  };

  auto setLocalWriteValue = [&m](string name, size_t value) {
    m.getOrInsertGlobal(name, Type::getInt64Ty(m.getContext()));
    GlobalVariable *module_var = m.getGlobalVariable(name);
    module_var->setInitializer(ConstantInt::get(Type::getInt64Ty(m.getContext()), value));
    module_var->setConstant(true);
  };

  // add a constant variable "DEPENDENCE_MODULE" and set to false
  setGlobalModule("DEPENDENCE_MODULE", UseDependenceModule);
  setGlobalModule("CONSTANT_VALUE_MODULE", UseConstantValueModule);
  setGlobalModule("LINEAR_VALUE_MODULE", UseLinearValueModule);
  setGlobalModule("CONSTANT_ADDRESS_MODULE", UseConstantAddressModule);
  setGlobalModule("LINEAR_ADDRESS_MODULE", UseLinearAddressModule);
  setGlobalModule("TRACE_MODULE", UseTraceModule);
  setGlobalModule("REASON_MODULE", UseReasonModule);
  setGlobalModule("LOCALWRITE_MODULE", UseLocalWriteModule);

  Function *ctor = instrumentConstructor(m);
  instrumentDestructor(m);

  if (ProfileGlobals) {
    instrumentGlobalVars(m, ctor);
  }

  instrumentAllocas(m);

  instrumentMainFunction(m);

  instrumentLoopStartStop(m, this->target_loop);

  instrumentInstructions(m, this->target_loop);

  // insert implementations for runtime wrapper functions, which calls the
  // binary standard function
  addWrapperImplementations(m);

  return true;
}

/// Find target function and loop baed on the options passed in
bool SLAMP::findTarget(Module &m) {
  auto &mloops = getAnalysis<ModuleLoops>();
  bool found = false;

  for (auto & fi : m) {
    Function *f = &fi;

    if (f->getName().str() == TargetFcn) {
      BasicBlock *header = nullptr;

      for (auto & bi : *f) {
        if (bi.getName().str() == TargetLoop) {
          header = &bi;
          break;
        }
      }

      if (header == nullptr)
        break;

      LoopInfo &loopinfo = mloops.getAnalysis_LoopInfo(f);

      this->target_loop = loopinfo.getLoopFor(header);

      if (!this->target_loop)
        break;

      this->target_fn = f;
      found = true;
    }
  }

  return found;
}

static bool is_setjmp_or_longjmp(Function *f) {
  string name = f->getName().str();
  if (name == "_setjmp" || name == "longjmp")
    return true;
  else
    return false;
}

bool SLAMP::mayCallSetjmpLongjmp(Loop *loop) {
  set<Function *> callables;
  getCallableFunctions(loop, callables);

  return (find_if(callables.begin(), callables.end(), is_setjmp_or_longjmp) !=
          callables.end());
}

void SLAMP::getCallableFunctions(Loop *loop, set<Function *> &callables) {
  for (auto &bb: loop->getBlocks()) {
    for (auto & ii : *bb) {
      // FIXME: not only callinst are callable
      auto *ci = dyn_cast<CallInst>(&ii);
      if (!ci)
        continue;
      getCallableFunctions(ci, callables);
    }
  }
}

void SLAMP::getCallableFunctions(CallInst *ci, set<Function *> &callables) {
  Function *called_fn = ci->getCalledFunction();
  if (called_fn == nullptr) {
    // analyze indirect function call */
    set<Function *> targets;

    // get functions callable by given callinst
    getFunctionsWithSign(ci, targets);

    // check matched functions
    for (auto target : targets) {
      if (callables.find(target) == callables.end()) {
        callables.insert(target);
        getCallableFunctions(target, callables);
      }
    }
  } else {
    if (callables.find(called_fn) == callables.end()) {
      callables.insert(called_fn);
      getCallableFunctions(called_fn, callables);
    }
  }
}

void SLAMP::getCallableFunctions(Function *f, set<Function *> &callables) {
  for (inst_iterator ii = inst_begin(f); ii != inst_end(f); ii++) {
      // FIXME: not only callinst are callable
    auto *ci = dyn_cast<CallInst>(&*ii);
    if (!ci)
      continue;
    getCallableFunctions(ci, callables);
  }
}

void SLAMP::getFunctionsWithSign(CallInst *ci, set<Function *> matched) {
  Module *m = ci->getParent()->getParent()->getParent();
  CallSite cs(ci);

  for (auto & fi : *m) {
    Function *func = &fi;

    bool found = true;
    // compare signature
    if (func->isVarArg()) {
      if (func->arg_size() > cs.arg_size())
        found = false;
    } else {
      if (func->arg_size() != cs.arg_size())
        found = false;
    }

    if (found) {
      Function::arg_iterator fai;
      CallSite::arg_iterator cai;
      for (fai = func->arg_begin(), cai = cs.arg_begin();
           fai != func->arg_end(); fai++, cai++) {
        Value *af = &*fai;
        Value *ac = *cai;
        if (af->getType() != ac->getType()) {
          found = false;
          break;
        }
      }
    }

    if (found)
      matched.insert(func);
  }
}

// Replace external functions with SLAMP prefixed ones (SLAMP_xxx)
// The list of SLAMP functions are given in `externs.h`
void SLAMP::replaceExternalFunctionCalls(Module &m) {
  // initialize a set of external function names
  set<string> externs;
  for (unsigned i = 0, e = sizeof(externs_str) / sizeof(externs_str[0]); i < e;
       i++)
    externs.insert(externs_str[i]);

  // initialize a set of external functions not to be implemented
  set<string> ignores;
  for (unsigned i = 0,
                e = sizeof(ignore_externs_str) / sizeof(ignore_externs_str[0]);
       i < e; i++)
    ignores.insert(ignore_externs_str[i]);

  vector<Function *> funcs;

  for (auto & fi : m) {
    Function *func = &fi;

    // only external functions are of interest
    if (!func->isDeclaration())
      continue;

    // filter functions to ignore
    if (ignores.find(func->getName()) != ignores.end())
      continue;

    // FIXME: malloc can be an intrinsic function, not all intrinsics can be ignored
    if (func->isIntrinsic()) {
      // just confirm that all uses is an intrinsic instruction
      for (Value::user_iterator ui = func->user_begin(); ui != func->user_end();
           ui++)
        assert(isa<IntrinsicInst>(*ui));
      continue;
    }

    funcs.push_back(func);
  }

  bool hasUnrecognizedFunction = false;
  for (auto func : funcs) {
     string name = func->getName();

    if (externs.find(name) == externs.end()) {
      // check if the function argument is `readnone`, then it's pure
      if (func->hasFnAttribute(llvm::Attribute::AttrKind::ReadNone)) {
        continue;
      }
      errs() << "WARNING: Wrapper for external function " << name
                        << " not implemented.\n";
      hasUnrecognizedFunction = true;
    } else {
      string wrapper_name = "SLAMP_" + name;
      /* Function* wrapper = cast<Function>( m.getOrInsertFunction(wrapper_name,
       * func->getFunctionType() ) ); */
      FunctionCallee wrapper =
          m.getOrInsertFunction(wrapper_name, func->getFunctionType());

      // replace 'func' to 'wrapper' in uses
      func->replaceAllUsesWith(wrapper.getCallee());
    }
  }

  if (hasUnrecognizedFunction) {
    // assert only turned on for debug
    // assert(false && "Wrapper for external function not implemented.\n");
    LLVM_DEBUG(errs() << "Wrapper for external function not implemented.\n");
  }
}

/// Create a function `___SLAMP_ctor` that calls `SLAMP_init` and
/// `SLAMP_init_global_vars` before everything (llvm.global_ctors)
Function *SLAMP::instrumentConstructor(Module &m) {
  // sid = &getAnalysis<StaticID>();

  LLVMContext &c = m.getContext();
  auto *ctor =
      cast<Function>(m.getOrInsertFunction("___SLAMP_ctor", Void).getCallee());
  BasicBlock *entry = BasicBlock::Create(c, "entry", ctor, nullptr);
  ReturnInst::Create(c, entry);
  callBeforeMain(ctor, 65534);

  // call SLAMP_init function

  // Function* init = cast<Function>( m.getOrInsertFunction( "SLAMP_init", Void,
  // I32, I32, (Type*)0) );
  auto *init = cast<Function>(
      m.getOrInsertFunction("SLAMP_init", Void, I32, I32).getCallee());

  Value *args[] = {
      ConstantInt::get(I32, Namer::getFuncId(this->target_fn)),
      ConstantInt::get(I32, Namer::getBlkId(this->target_loop->getHeader()))};
  CallInst::Create(init, args, "", entry->getTerminator());

  return ctor;
}

/// Create a function `___SLAMP_dtor` that calls `SLAMP_fini`, register through
/// `llvm.global_dtors`
void SLAMP::instrumentDestructor(Module &m) {
  LLVMContext &c = m.getContext();
  auto *dtor =
      cast<Function>(m.getOrInsertFunction("___SLAMP_dtor", Void).getCallee());
  BasicBlock *entry = BasicBlock::Create(c, "entry", dtor, nullptr);
  ReturnInst::Create(c, entry);
  callAfterMain(dtor, 65534);

  // call SLAMP_fini function
  auto *fini = cast<Function>(
      m.getOrInsertFunction("SLAMP_fini", Void, I8Ptr).getCallee());
  Constant *filename = getStringLiteralExpression(m, outfile);
  Value *args[] = {filename};

  CallInst::Create(fini, args, "", entry->getTerminator());
}

/// Go through all global variables and call `SLAMP_init_global_vars`
void SLAMP::instrumentGlobalVars(Module &m, Function *ctor) {
  // DataLayout& td = getAnalysis<DataLayout>();
  const DataLayout &td = m.getDataLayout();
  BasicBlock *entry = &(ctor->getEntryBlock());

  // call SLAMP_init_global_vars function to initialize shadow memory for
  // global variables
  auto *init_gvars = cast<Function>(
      m.getOrInsertFunction("SLAMP_init_global_vars", Void, I8Ptr, I64, I64)
          .getCallee());

  for (GlobalVariable &gvr : m.globals()) {
    GlobalVariable *gv = &gvr;

    if (gv->getName() == "llvm.global_ctors") // explicitly skip global ctor
      continue;
    else if (gv->getName() == "llvm.global_dtors") // explicitly skip global dtor
      continue;

    auto *ty = dyn_cast<PointerType>(gv->getType());
    assert(ty);

    InstInsertPt pt = InstInsertPt::Before(entry->getTerminator());
    // get name of the global

    // Value *name = getGlobalName(gv);
    Value *name = getStringLiteralExpression(m, "hello");
    uint64_t size = td.getTypeStoreSize(ty->getElementType());
    Value *args[] = {name, castToInt64Ty(gv, pt), ConstantInt::get(I64, size)};
    pt << CallInst::Create(init_gvars, args);
  }

  for (auto &&fi : m) {
    Function *func = &fi;

    if (func->isIntrinsic())
      continue;

    uint64_t size = td.getTypeStoreSize(func->getType());

    Value *name = getStringLiteralExpression(m, "hello");
    InstInsertPt pt = InstInsertPt::Before(entry->getTerminator());
    Value *args[] = {name, castToInt64Ty(func, pt), ConstantInt::get(I64, size)};
    pt << CallInst::Create(init_gvars, args);
  }
}

// For each alloca, find the lifetime starts and ends
// and insert calls to `SLAMP_callback_stack_alloca` and
// `SLAMP_callback_stack_free`
void SLAMP::instrumentAllocas(Module &m) {



}

// /// FIXME: not called anywhere
// void SLAMP::instrumentNonStandards(Module &m, Function *ctor) {
//   // 1) handle __errno_location.
//   allocErrnoLocation(m, ctor);
// }

// /// FIXME: not clear what this do
// void SLAMP::allocErrnoLocation(Module &m, Function *ctor) {
//   // DataLayout&  td = getAnalysis<DataLayout>();
//   const DataLayout &td = m.getDataLayout();
//   LLVMContext &c = m.getContext();

//   // Call dummy __errno_location to allocate a shadow memory for the location
//   auto *f = cast<Function>(
//       m.getOrInsertFunction("SLAMP___errno_location_alloc", Void).getCallee());

//   BasicBlock *entry = BasicBlock::Create(c, "entry", f, nullptr);
//   auto *c0 = cast<Function>(
//       m.getOrInsertFunction("__errno_location", I32->getPointerTo())
//           .getCallee());
//   InstInsertPt pt = InstInsertPt::Beginning(entry);
//   CallInst *ci = CallInst::Create(c0, "");
//   pt << ci;

//   // ci is a __errno_location call
//   auto *ty = dyn_cast<PointerType>(ci->getType());
//   assert(ty);
//   uint64_t size = td.getTypeStoreSize(ty->getElementType());

//   // reuse SLAMP_init_global_vars
//   auto *init_gvars = dyn_cast<Function>(
//       m.getOrInsertFunction("SLAMP_init_global_vars", Void, I64, I64)
//           .getCallee());
//   assert(init_gvars);

//   Value *args[] = {castToInt64Ty(ci, pt), ConstantInt::get(I64, size)};
//   CallInst *ci2 = CallInst::Create(init_gvars, args, "");
//   pt << ci2;

//   pt << ReturnInst::Create(c);

//   // call function f from ctor
//   BasicBlock *ctor_entry = &(ctor->getEntryBlock());
//   CallInst::Create(f, "", ctor_entry->getTerminator());
// }


/// Add SLAMP_main_entry as the first thing in main
void SLAMP::instrumentMainFunction(Module &m) {
  for (Module::iterator fi = m.begin(), fe = m.end(); fi != fe; fi++) {
    Function *func = &*fi;
    if (func->getName() != "main")
      continue;

    BasicBlock *entry = &(func->getEntryBlock());

    // if the function is a main function, add special instrumentation to handle
    // command line arguments
    auto *f_main_entry = cast<Function>(
        m.getOrInsertFunction("SLAMP_main_entry", Void, I32,
                              I8Ptr->getPointerTo(), I8Ptr->getPointerTo())
            .getCallee());

    vector<Value *> main_args;
    for (auto &&ai : func->args())
      main_args.push_back(&ai);

    // make up arguments
    if (main_args.size() != 3) { // if not all of argc, argv, evn are given
      Value *zeroarg = ConstantInt::get(I32, 0);
      Value *nullarg = ConstantPointerNull::get(I8Ptr->getPointerTo());

      if (main_args.size() == 0) { // no command line input
        main_args.push_back(zeroarg); // argc
        main_args.push_back(nullarg); // argv
        main_args.push_back(nullarg); // envp
      } else if (main_args.size() == 2) { // only argc, argv given
        main_args.push_back(nullarg);
      } else {
        assert(false && "Only have one argument (argc) in main, do not conform to standard");
      }
    }

    InstInsertPt pt;
    if (isa<LandingPadInst>(entry->getFirstNonPHI()))
      pt = InstInsertPt::After(entry->getFirstNonPHI());
    else
      pt = InstInsertPt::Before(entry->getFirstNonPHI());

    // // read rsp and push it into main_args

    // FunctionType *fty = FunctionType::get(I64, false);
    // // get the static pointer %rsp
    // InlineAsm *get_rsp = InlineAsm::get(
    //     fty, "mov %rsp, $0;", "=r,~{dirflag},~{fpsr},~{flags}", false);

    // CallInst *rsp = CallInst::Create(get_rsp, "");

    pt << CallInst::Create(f_main_entry, main_args, "");
  }
}

/// Pass in the loop and instrument invocation/iteration/exit hooks
void SLAMP::instrumentLoopStartStop(Module &m, Loop *loop) {
  // TODO: check setjmp/longjmp

  BasicBlock *header = loop->getHeader();
  BasicBlock *latch = loop->getLoopLatch();

  // check if loop-simplify pass executed
  assert(loop->getNumBackEdges() == 1 &&
         "Should be only 1 back edge, loop-simplify?");
  assert(latch && "Loop latch needs to exist, loop-simplify?");

  // add instrumentation on loop header:
  // if new invocation, call SLAMP_loop_invocation, else, call
  // SLAMP_loop_iteration
  auto *f_loop_invoke = cast<Function>(
      m.getOrInsertFunction("SLAMP_loop_invocation", Void).getCallee());
  auto *f_loop_iter = cast<Function>(
      m.getOrInsertFunction("SLAMP_loop_iteration", Void).getCallee());
  auto *f_loop_exit = cast<Function>(
      m.getOrInsertFunction("SLAMP_loop_exit", Void).getCallee());

  PHINode *funcphi = PHINode::Create(f_loop_invoke->getType(), 2, "funcphi");
  InstInsertPt pt;

  if (isa<LandingPadInst>(header->getFirstNonPHI()))
    pt = InstInsertPt::After(header->getFirstNonPHI());
  else
    pt = InstInsertPt::Before(header->getFirstNonPHI());

  pt << funcphi;

  // choose which function to execute (iter or invoke)
  for (auto pred : predecessors(header)) {
    if (pred == latch)
      funcphi->addIncoming(f_loop_iter, pred);
    else
      funcphi->addIncoming(f_loop_invoke, pred);
  }

  CallInst::Create(funcphi, "", header->getFirstNonPHI());

  // Add `SLAMP_loop_exit` to all loop exits
  SmallVector<BasicBlock *, 8> exits;
  loop->getExitBlocks(exits);

  // one instrumentation per block
  set<BasicBlock *> s;

  for (unsigned i = 0; i < exits.size(); i++) {
    if (s.count(exits[i]))
      continue;

    CallInst *ci = CallInst::Create(f_loop_exit, "");

    InstInsertPt pt2;
    if (isa<LandingPadInst>(exits[i]->getFirstNonPHI()))
      pt2 = InstInsertPt::After(exits[i]->getFirstNonPHI());
    else
      pt2 = InstInsertPt::Before(exits[i]->getFirstNonPHI());
    pt2 << ci;

    s.insert(exits[i]);
  }
}

/// Instrument all instructions in a loop
void SLAMP::instrumentInstructions(Module &m, Loop *loop) {
  // collect loop instructions
  set<Instruction *> loopinsts;

  for (auto &bb : loop->getBlocks())
    for (auto &ii : *bb)
      loopinsts.insert(&ii);

  // go over all instructions in the module
  // - change some intrinsics functions
  // - for instructions within the loop, replace it with normal load/store
  // - for instructions outside of the loop, replace it with external load/store
  for (auto &&f : m) {
    if (f.isDeclaration())
      continue;

    for (auto &&inst : instructions(f)) {
      //// FIXME: ignore lifetime_start/end instrumentation
      // if (const auto Intrinsic = dyn_cast<IntrinsicInst>(&inst)) {
      //   const auto Id = Intrinsic->getIntrinsicID();
      //   if (Id == Intrinsic::lifetime_start || Id == Intrinsic::lifetime_end) {
      //     instrumentLifetimeIntrinsics(m, &inst);
      //     continue;
      //   }
      // }

      if (auto *mi = dyn_cast<MemIntrinsic>(&inst)) {
        instrumentMemIntrinsics(m, mi);
      } else if (loopinsts.find(&inst) != loopinsts.end()) {
        instrumentLoopInst(m, &inst, Namer::getInstrId(&inst));
      } else {
        // instrumentExtInst(m, &inst, sid.getFuncLocalIDWithInst(&*fi, &inst));
        instrumentExtInst(m, &inst, Namer::getInstrId(&inst));
      }
    }
  }
}

/// get the size of the load or store instruction based on the type
int SLAMP::getIndex(PointerType *ty, size_t &size, const DataLayout &DL) {
  int i = DL.getTypeStoreSizeInBits(ty->getElementType());

  // sot: cannot convert a vector value to an int64 so just return variable size
  // n (index 4) and return the actual size, even if i is less than or equal
  // to 64.
  if (isa<VectorType>(ty->getElementType())) {
    size = i / 8;
    return 4;
  }

  switch (i) {
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

/// handle LLVM Memory intrinsics (memmove, memcpy, memset)
/// This does not replace the function, just add an additional call
// FIXME: is this a complete list?
void SLAMP::instrumentMemIntrinsics(Module &m, MemIntrinsic *mi) {
  CallSite cs(mi);
  const Function *callee = cs.getCalledFunction();
  assert(callee);
  string callee_name = callee->getName();

  // add intrinsic handlers

  Type *mi_param_types_a[] = {I8Ptr, I8Ptr, I32};
  Type *mi_param_types_b[] = {I8Ptr, I8Ptr, I64};
  Type *mi_param_types_c[] = {I8Ptr, I32};
  Type *mi_param_types_d[] = {I8Ptr, I64};

  FunctionType *mi_fty_a = FunctionType::get(Void, mi_param_types_a, false);
  FunctionType *mi_fty_b = FunctionType::get(Void, mi_param_types_b, false);
  FunctionType *mi_fty_c = FunctionType::get(Void, mi_param_types_c, false);
  FunctionType *mi_fty_d = FunctionType::get(Void, mi_param_types_d, false);

  m.getOrInsertFunction("SLAMP_llvm_memcpy_p0i8_p0i8_i32", mi_fty_a);
  m.getOrInsertFunction("SLAMP_llvm_memcpy_p0i8_p0i8_i64", mi_fty_b);

  m.getOrInsertFunction("SLAMP_llvm_memmove_p0i8_p0i8_i32", mi_fty_a);
  m.getOrInsertFunction("SLAMP_llvm_memmove_p0i8_p0i8_i64", mi_fty_b);

  m.getOrInsertFunction("SLAMP_llvm_memset_p0i8_i32", mi_fty_c);
  m.getOrInsertFunction("SLAMP_llvm_memset_p0i8_i64", mi_fty_d);

  if (callee_name == "llvm.memcpy.p0i8.p0i8.i32" ||
      callee_name == "llvm.memcpy.p0i8.p0i8.i64" ||
      callee_name == "llvm.memmove.p0i8.p0i8.i32" ||
      callee_name == "llvm.memmove.p0i8.p0i8.i64" ||
      callee_name == "llvm.memset.p0i8.i32" ||
      callee_name == "llvm.memset.p0i8.i64") {
    // good
  } else {
    assert(false && "Unknown memory intrinsic");
  }

  // get corresponding SLAMP runtime function by manipulating callee_name
  ostringstream name;
  name << "SLAMP_";
  for (char i : callee_name) {
    if (i == '.')
      name << '_';
    else
      name << i;
  }
  Function *fcn = m.getFunction(name.str());

  // set parameters

  vector<Value *> args;

  if (callee_name.find("memset") != string::npos) {
    // memset
    args.push_back(cs.getArgument(0));
    args.push_back(cs.getArgument(2));
  } else {
    // memcpy and memmove
    args.push_back(cs.getArgument(0));
    args.push_back(cs.getArgument(1));
    args.push_back(cs.getArgument(2));
  }

  CallInst::Create(fcn, args, "", mi);
}

/// handle `llvm.lifetime.start/end.p0i8`
void SLAMP::instrumentLifetimeIntrinsics(Module &m, Instruction *inst) {
  CallSite cs(inst);
  const Function *callee = cs.getCalledFunction();
  assert(callee);
  string callee_name = callee->getName();

  // add intrinsic handlers
  Type *mi_param_types_a[] = {I64, I8Ptr};

  FunctionType *mi_fty_a = FunctionType::get(Void, mi_param_types_a, false);

  m.getOrInsertFunction("SLAMP_llvm_lifetime_start_p0i8", mi_fty_a);
  m.getOrInsertFunction("SLAMP_llvm_lifetime_end_p0i8", mi_fty_a);

  if (callee_name == "llvm.lifetime.start.p0i8" ||
      callee_name == "llvm.lifetime.end.p0i8") {
    // good
  } else {
    assert(false && "Unknown lifetime intrinsic");
  }

  // get corresponding SLAMP runtime function by manipulating callee_name
  // replace "." with "_"
  ostringstream name;
  name << "SLAMP_";
  for (char i : callee_name) {
    if (i == '.')
      name << '_';
    else
      name << i;
  }
  Function *fcn = m.getFunction(name.str());

  // set parameters
  vector<Value *> args;
  args.push_back(cs.getArgument(0));
  args.push_back(cs.getArgument(1));

  CallInst::Create(fcn, args, "", inst);
}

/// handle each instruction (load, store, callbase) in the targeted loop
void SLAMP::instrumentLoopInst(Module &m, Instruction *inst, uint32_t id) {
  if (IgnoreCall) {
    if (isa<CallBase>(inst)) {
      LLVM_DEBUG( errs() << "SLAMP: ignore call " << *inst << "\n" );
      return;
    }
  }
  // if elided
  if (elidedLoopInsts.count(inst)) {
    LLVM_DEBUG( errs() << "SLAMP: elided " << *inst << "\n" );
    return;
  }
  else {
    LLVM_DEBUG(if (inst->mayReadOrWriteMemory()) errs()
                   << "SLAMP: instrument " << *inst << "\n";);
  }

  const DataLayout &DL = m.getDataLayout();

  // assert(id < INST_ID_BOUND);

  if (id == 0) // instrumented instructions
    return;

  // FIXME: need to handle 16 bytes naturally
  // --- loads
  string lf_name[] = {"SLAMP_load1", "SLAMP_load2", "SLAMP_load4",
                      "SLAMP_load8", "SLAMP_loadn"};
  vector<Function *> lf(5);

  for (unsigned i = 0; i < 5; i++) {
    // load1-8 the last argument is the value, use to do prediction
    // loadn the last argument is the length
    lf[i] = cast<Function>(
        m.getOrInsertFunction(lf_name[i], Void, I32, I64, I32, I64)
            .getCallee());
  }

  // --- stores
  string sf_name[] = {"SLAMP_store1", "SLAMP_store2", "SLAMP_store4",
                      "SLAMP_store8", "SLAMP_storen"};
  vector<Function *> sf(5);

  for (unsigned i = 0; i < 4; i++) {
    sf[i] = cast<Function>(
        m.getOrInsertFunction(sf_name[i], Void, I32, I64).getCallee());
  }
  sf[4] = cast<Function>(
      m.getOrInsertFunction(sf_name[4], Void, I32, I64, I64).getCallee());

  // --- calls

  auto *push = cast<Function>(
      m.getOrInsertFunction("SLAMP_push", Void, I32).getCallee());
  auto *pop =
      cast<Function>(m.getOrInsertFunction("SLAMP_pop", Void).getCallee());

  if (auto *li = dyn_cast<LoadInst>(inst)) {
    // if the loaded pointer is a global
    if (isa<GlobalVariable>(li->getPointerOperand())) {
      if (!ProfileGlobals) {
        LLVM_DEBUG(errs() << "SLAMP: ignore global load " << *li << "\n");
        return;
      }
    }

    InstInsertPt pt = InstInsertPt::After(li);

    Value *ptr = li->getPointerOperand();
    vector<Value *> args;

    args.push_back(ConstantInt::get(I32, id));
    args.push_back(castToInt64Ty(ptr, pt));

    size_t size;
    int index = getIndex(cast<PointerType>(ptr->getType()), size, DL);

    if (index == 4) {
      args.push_back(ConstantInt::get(I32, id));
      args.push_back(ConstantInt::get(I64, size)); // size
    } else {
      args.push_back(ConstantInt::get(I32, id));
      args.push_back(castToInt64Ty(li, pt)); // value
    }

    pt << CallInst::Create(lf[index], args);
  } else if (auto *si = dyn_cast<StoreInst>(inst)) {
    // if the stored pointer is a global
    if (isa<GlobalVariable>(si->getPointerOperand())) {
      if (!ProfileGlobals) {
        LLVM_DEBUG(errs() << "SLAMP: ignore global store " << *si << "\n");
        return;
      }
    }

    InstInsertPt pt = InstInsertPt::After(si);

    Value *ptr = si->getPointerOperand();
    vector<Value *> args;

    args.push_back(ConstantInt::get(I32, id));
    args.push_back(castToInt64Ty(ptr, pt));

    size_t size;
    int index = getIndex(cast<PointerType>(ptr->getType()), size, DL);

    if (index == 4) {
      args.push_back(ConstantInt::get(I64, size));
    }

    pt << CallInst::Create(sf[index], args);
  } else if (auto *ci = dyn_cast<CallBase>(inst)) {
    // need to handle call and invoke
    vector<Value *> args;

    args.push_back(ConstantInt::get(I32, id));

    InstInsertPt pt = InstInsertPt::Before(ci);
    pt << CallInst::Create(push, args);

    if (isa<CallInst>(inst)) {
      pt = InstInsertPt::After(ci);
      pt << CallInst::Create(pop);
    } else if (auto *invokeI = dyn_cast<InvokeInst>(inst)) {
      // for invoke, need to find the two paths and add pop
      auto insertPop = [&pop](BasicBlock* entry){
        InstInsertPt pt;
        if (isa<LandingPadInst>(entry->getFirstNonPHI()))
          pt = InstInsertPt::After(entry->getFirstNonPHI());
        else
          pt = InstInsertPt::Before(entry->getFirstNonPHI());
        pt << CallInst::Create(pop);
      };

      insertPop(invokeI->getNormalDest());
      // FIXME: will generate mulitiple `slamp_pop` after the landing pad
      //        Fine for now because `slamp_pop` only set the context to 0
      insertPop(invokeI->getUnwindDest());

    } else {
      assert(false && "Call but not CallInst nor InvokeInst");
    }
  }
}

/// Handle each instruction (load, store) outside of the targeted loop.
/// We don't care about about call insts in this case
void SLAMP::instrumentExtInst(Module &m, Instruction *inst, uint32_t id) {
  // --- loads
  const DataLayout &DL = m.getDataLayout();

  string lf_name[] = {"SLAMP_load1_ext", "SLAMP_load2_ext", "SLAMP_load4_ext",
                      "SLAMP_load8_ext", "SLAMP_loadn_ext"};
  vector<Function *> lf(5);

  for (unsigned i = 0; i < 5; i++) {
    // lf[i] = cast<Function>( m.getOrInsertFunction(lf_name[i], Void, I64, I32,
    // I64, (Type*)0) );
    lf[i] = cast<Function>(
        m.getOrInsertFunction(lf_name[i], Void, I64, I32, I64).getCallee());
  }

  // --- stores
  string sf_name[] = {"SLAMP_store1_ext", "SLAMP_store2_ext",
                      "SLAMP_store4_ext", "SLAMP_store8_ext",
                      "SLAMP_storen_ext"};
  vector<Function *> sf(5);

  for (unsigned i = 0; i < 4; i++) {
    // sf[i] = cast<Function>( m.getOrInsertFunction(sf_name[i], Void, I64, I32,
    // (Type*)0) );
    sf[i] = cast<Function>(
        m.getOrInsertFunction(sf_name[i], Void, I64, I32).getCallee());
  }
  // sf[4] = cast<Function>( m.getOrInsertFunction(sf_name[4], Void, I64, I32,
  // I64, (Type*)0) );
  sf[4] = cast<Function>(
      m.getOrInsertFunction(sf_name[4], Void, I64, I32, I64).getCallee());

  if (auto *li = dyn_cast<LoadInst>(inst)) {
    // if the loaded pointer is a global
    if (isa<GlobalVariable>(li->getPointerOperand())) {
      if (!ProfileGlobals) {
        LLVM_DEBUG(errs() << "SLAMP: ignore global load " << *li << "\n");
        return;
      }
    }

    InstInsertPt pt = InstInsertPt::After(li);

    Value *ptr = li->getPointerOperand();
    vector<Value *> args;

    args.push_back(castToInt64Ty(ptr, pt));

    size_t size;
    int index = getIndex(cast<PointerType>(ptr->getType()), size, DL);

    if (index == 4) {
      args.push_back(ConstantInt::get(I32, id));
      args.push_back(ConstantInt::get(I64, size));
    } else {
      args.push_back(ConstantInt::get(I32, id));
      args.push_back(castToInt64Ty(li, pt));
    }

    pt << CallInst::Create(lf[index], args);
  } else if (auto *si = dyn_cast<StoreInst>(inst)) {
    // if the stored pointer is a global
    if (isa<GlobalVariable>(si->getPointerOperand())) {
      if (!ProfileGlobals) {
        LLVM_DEBUG(errs() << "SLAMP: ignore global store " << *si << "\n");
        return;
      }
    }
    InstInsertPt pt = InstInsertPt::After(si);

    Value *ptr = si->getPointerOperand();
    vector<Value *> args;

    args.push_back(castToInt64Ty(ptr, pt));

    size_t size;
    int index = getIndex(cast<PointerType>(ptr->getType()), size, DL);

    args.push_back(ConstantInt::get(I32, id));

    if (index == 4) {
      args.push_back(ConstantInt::get(I64, size));
    }

    pt << CallInst::Create(sf[index], args);
  }
}

/// FIXME: hand-roll an implementation for SLAMP___error_location because
/// __errno_location won't compile
void SLAMP::addWrapperImplementations(Module &m) {
  LLVMContext &c = m.getContext();
  vector<Value *> args;

  // --- SLAMP___errno_location
  auto *f0 = cast<Function>(
      m.getOrInsertFunction("SLAMP___errno_location", I32->getPointerTo())
          .getCallee());
  BasicBlock *entry = BasicBlock::Create(c, "entry", f0, nullptr);
  auto *c0 = cast<Function>(
      m.getOrInsertFunction("__errno_location", I32->getPointerTo())
          .getCallee());
  InstInsertPt pt = InstInsertPt::Beginning(entry);
  CallInst *ci = CallInst::Create(c0, "");
  pt << ci;
  pt << ReturnInst::Create(c, ci);
}

} // namespace liberty::slamp
