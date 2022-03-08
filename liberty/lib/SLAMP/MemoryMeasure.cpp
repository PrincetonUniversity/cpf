// Test malloc and load store total size

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/PassAnalysisSupport.h"

#include "liberty/SLAMP/SLAMP.h"
#include "scaf/Utilities/GlobalCtors.h"
#include "scaf/Utilities/Metadata.h"

#include <cstdint>

namespace liberty::slamp {
using namespace std;
using namespace llvm;

class MemoryMeasure : public llvm::ModulePass {

public:
  static char ID;
  MemoryMeasure();

  void getAnalysisUsage(AnalysisUsage &au) const override;
  bool runOnModule(Module &M) override;

private:
  Type *Void;
  Type *I64;
  Type *I32;
  void instrumentConstructor(Module &m);
  void instrumentDestructor(Module &m);
};

char MemoryMeasure::ID = 0;
static RegisterPass<MemoryMeasure>
    RP("memory-measure", "Measure malloc and load store usage", false, false);
void MemoryMeasure::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
}

MemoryMeasure::MemoryMeasure() : ModulePass(ID) {}

bool MemoryMeasure::runOnModule(Module &M) {
  bool modified = false;

  auto &ctx = M.getContext();
  Void = Type::getVoidTy(ctx);
  I64 = Type::getInt64Ty(ctx);
  I32 = Type::getInt32Ty(ctx);

  instrumentConstructor(M);
  instrumentDestructor(M);

  const DataLayout &DL = M.getDataLayout();
  // find all load and store and add a SLAMP_measure_load(addr, size);

  string load_fn_name = "SLAMP_measure_load";
  string store_fn_name = "SLAMP_measure_store";
  auto *load_fn = cast<Function>(
      M.getOrInsertFunction(load_fn_name, Void, I32, I64).getCallee());
  auto *store_fn = cast<Function>(
      M.getOrInsertFunction(store_fn_name, Void, I32, I64).getCallee());

  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        Instruction *inst = &I;
        if (!isa<LoadInst>(inst) && !isa<StoreInst>(inst)) {
          continue;
        }

        modified = true;
        vector<Value *> args;
        auto id = Namer::getInstrId(inst);
        args.push_back(ConstantInt::get(I32, id));

        if (auto *li = dyn_cast<LoadInst>(inst)) {
          Value *ptr = li->getPointerOperand();
          auto ty = cast<PointerType>(ptr->getType());
          uint64_t size = DL.getTypeStoreSize(ty->getElementType());
          args.push_back(ConstantInt::get(I64, size));

          CallInst::Create(load_fn, args, "", inst);
        }

        if (auto *si = dyn_cast<StoreInst>(inst)) {
          Value *ptr = si->getPointerOperand();
          auto ty = cast<PointerType>(ptr->getType());
          uint64_t size = DL.getTypeStoreSize(ty->getElementType());
          args.push_back(ConstantInt::get(I64, size));

          CallInst::Create(store_fn, args, "", inst);
        }
      }
    }
  }
  return modified;
}

/// Create a function `___SLAMP_ctor` that calls `SLAMP_init` and
/// `SLAMP_init_global_vars` before everything (llvm.global_ctors)
void MemoryMeasure::instrumentConstructor(Module &m) {
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
      m.getOrInsertFunction("SLAMP_measure_init", Void).getCallee());
  CallInst::Create(init, "", entry->getTerminator());

  return;
}

/// Create a function `___SLAMP_dtor` that calls `SLAMP_fini`, register through
/// `llvm.global_dtors`
void MemoryMeasure::instrumentDestructor(Module &m) {
  LLVMContext &c = m.getContext();
  auto *dtor =
      cast<Function>(m.getOrInsertFunction("___SLAMP_dtor", Void).getCallee());
  BasicBlock *entry = BasicBlock::Create(c, "entry", dtor, nullptr);
  ReturnInst::Create(c, entry);
  callAfterMain(dtor, 65534);

  // call SLAMP_fini function
  auto *fini = cast<Function>(
      m.getOrInsertFunction("SLAMP_measure_fini", Void).getCallee());
  CallInst::Create(fini, "", entry->getTerminator());
}

} // namespace liberty::slamp
