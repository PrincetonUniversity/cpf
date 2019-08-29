// A Speculative PS-DSWP transform.
#ifndef LLVM_LIBERTY_SPEC_PRIV_TRANSFORM_PIPELINE_STRATEGY_OLD_H
#define LLVM_LIBERTY_SPEC_PRIV_TRANSFORM_PIPELINE_STRATEGY_OLD_H

#include "liberty/Speculation/ControlSpeculator.h"
#include "DAGSCC.h"
#include "PDG.h"

#include "llvm/Support/Casting.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct LoopParallelizationStrategyOLD
{
  enum LPSKind
  {
    LPSK_DOALL = 0,
    LPSK_Pipeline
  };

  LoopParallelizationStrategyOLD(LPSKind k) : header(0), kind(k) {}
  ~LoopParallelizationStrategyOLD() {}

  void invalidate() { header = 0; }
  void setValidFor(BasicBlock *h) { header = h; }
  BasicBlock *getHeader() const { return header; }
  bool isValid() const { return (header != 0); }

  // Write a short summary message,
  // such as 'DOALL' or 'DSWP [S-P-S]'
  virtual void summary(raw_ostream &fout) const = 0;

  // To apply speculation independently of parallelization.
  // When we modify the parallel region by adding
  // instructions, update the parallelization strategy
  // to include those instructions.
  // Instructions have 'gravity', which means that the
  // added instructions should be placed near the existing
  // instructions.
  virtual void addInstruction(Instruction *newInst, Instruction *gravity) = 0;
  virtual void replaceInstruction(Instruction *newInst, Instruction *oldInst) = 0;

  // collect the stages that the given instruction is executed in
  virtual void getExecutingStages(Instruction* inst, std::vector<unsigned>& stages) = 0;

  // check if I1 is always in the stage that I2 is in
  virtual bool ifI2IsInI1IsIn(Instruction* i1, Instruction* i2) = 0;

  virtual unsigned getStageNum() const = 0;

  // Sanity check
  virtual void assertConsistentWithIR(Loop *loop);

  LPSKind getKind() const { return kind; }

private:
  BasicBlock *header;

  const LPSKind kind;
};

struct DoallStrategyOLD : public LoopParallelizationStrategyOLD
{
  DoallStrategyOLD(BasicBlock *header=0) : LoopParallelizationStrategyOLD(LPSK_DOALL) { setValidFor(header); }

  virtual void summary(raw_ostream &fout) const
  {
    fout << "DOALL";
  }

  // Update strategies.
  virtual void addInstruction(Instruction *newInst, Instruction *gravity) {}
  virtual void replaceInstruction(Instruction *newInst, Instruction *oldInst) {}

  virtual void getExecutingStages(Instruction* inst, std::vector<unsigned>& stages) {}
  virtual bool ifI2IsInI1IsIn(Instruction* i1, Instruction* i2) { return true; }

  virtual unsigned getStageNum() const { return 1; }

  static bool classof(const LoopParallelizationStrategyOLD *lps)
  {
    return lps->getKind() == LPSK_DOALL;
  }
};

// syntactic sugar for strat.summary(fout)
raw_ostream &operator<<(raw_ostream &fout, const LoopParallelizationStrategyOLD &strat);

struct PipelineStageOLD
{
  typedef std::set<Instruction*> ISet;
  enum Type
  {
    Sequential = 0,
    Replicable,
    Parallel
  };

  PipelineStageOLD(Type t, const PDG &pdg, const SCCs &sccs, const SCCs::SCCSet &scc_list);

  /// Create a degenerate sequential stage containing all instructions from this loop
  PipelineStageOLD(Loop *loop);

  ISet      replicated;
  ISet      instructions;
  Type      type;
  unsigned  parallel_factor;

  bool communicatesTo(const PipelineStageOLD &other) const;

  void print_txt(raw_ostream &fout, ControlSpeculation *ctrlspec=0, StringRef line_suffix = "") const;

private:
  //sot
  //void print_inst_txt(raw_ostream &fout, ControlSpeculation *ctrlspec, Instruction *inst, const char *line_suffix = 0) const;
  void print_inst_txt(raw_ostream &fout, ControlSpeculation *ctrlspec, Instruction *inst, StringRef line_suffix = "") const;
};

// Represents dependences between instructions
// which span pipeline stages.
struct CrossStageDependenceOLD
{
  CrossStageDependenceOLD(Instruction *s, Instruction *d, const PartialEdge &pe)
    : src(s), dst(d), edge(pe) {}

  Instruction *src, *dst;
  PartialEdge edge;
};

struct PipelineStrategyOLD : public LoopParallelizationStrategyOLD
{
  PipelineStrategyOLD() : LoopParallelizationStrategyOLD(LPSK_Pipeline) {}

  typedef std::vector<PipelineStageOLD> Stages;
  Stages  stages;

  typedef std::vector<CrossStageDependenceOLD> CrossStageDependenceOLDs;
  CrossStageDependenceOLDs crossStageDeps;

  virtual void summary(raw_ostream &fout) const;
  void dump_pipeline(raw_ostream &fout, ControlSpeculation *ctrlspec=0, StringRef line_suffix = "") const;

  bool expandReplicatedStages();
  static bool expandReplicatedStages(Stages &stages);

  void print_dot(raw_ostream &fout, const PDG &pdg, const SCCs &sccs, ControlSpeculation *ctrlspec=0) const;
  void print_dot(const PDG &pdg, const SCCs &sccs, StringRef dot, StringRef tred, ControlSpeculation *ctrlspec=0) const;

  // Interrogate a pipeline:
  bool mayExecuteInStage(const Instruction *inst, unsigned stageno) const;
  bool mayExecuteInParallelStage(const Instruction *inst) const;

  // Would the dep 'src' -> 'dst' violate pipeline order?
  bool maybeAntiPipelineDependence(const Instruction *src, const Instruction *dst) const;
  // Would the loop-carried dep 'src' -> 'dst' violate a parallel stage?
  bool maybeAntiParallelStageDependence(const Instruction *src, const Instruction *dst) const;

  // Update strategies
  virtual void addInstruction(Instruction *newInst, Instruction *gravity);
  virtual void replaceInstruction(Instruction *newInst, Instruction *oldInst);

  virtual void getExecutingStages(Instruction* inst, std::vector<unsigned>& stages);
  // check if I1 is always in the stage that I2 is in
  virtual bool ifI2IsInI1IsIn(Instruction* i1, Instruction* i2);

  virtual unsigned getStageNum() const { return stages.size(); }

  // Sanity check
  void assertPipelineProperty(const PDG &pdg) const;
  virtual void assertConsistentWithIR(Loop *loop);

  static bool classof(const LoopParallelizationStrategyOLD *lps)
  {
    return lps->getKind() == LPSK_Pipeline;
  }

private:
  void assertPipelineProperty(const PDG &pdg, const PipelineStageOLD &earlier, const PipelineStageOLD &later) const;
  void assertCheckedPipelineProperty(const PDG &pdg, const PipelineStageOLD &earlier, const PipelineStageOLD &later) const;
  void assertParallelStageProperty(const PDG &pdg, const PipelineStageOLD &parallel, const PipelineStageOLD &other) const;
};


}
}

#endif

