#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "scaf/SpeculationModules/Remediator.h"

namespace liberty {
  using namespace llvm;
  struct Planner: public ModulePass {
    static char ID;
    Planner() : ModulePass(ID) {}
    void getAnalysisUsage(AnalysisUsage &au) const override;
    bool runOnModule(Module &m) override;

    StringRef getName() const  {
      return "Planner";
    }

    bool parallelizeLoop(Module &M, Loop *loop);

    using Remediator_ptr = std::unique_ptr<Remediator>;

    std::vector<Remediator_ptr> getAvailableRemediators(Loop *A, PDG *pdg) ;
    vector<LoopAA *> addAndSetupSpecModulesToLoopAA(Module &M, Loop *loop);

  };

} // namespace liberty

