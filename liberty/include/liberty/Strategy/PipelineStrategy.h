// A Speculative PS-DSWP transform.
#ifndef LLVM_LIBERTY_SPEC_PRIV_TRANSFORM_PIPELINE_STRATEGY_H
#define LLVM_LIBERTY_SPEC_PRIV_TRANSFORM_PIPELINE_STRATEGY_H

#include "PDG.hpp"
#include "SCCDAG.hpp"
#include "SCC.hpp"

#include "llvm/Support/Casting.h"

#include <vector>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;


struct LoopParallelizationStrategy
{
  enum LPSKind
  {
    LPSK_DOALL = 0,
    LPSK_Pipeline
  };

  LoopParallelizationStrategy(LPSKind k) : header(0), kind(k) {}
  ~LoopParallelizationStrategy() {}

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
  virtual void addInstruction(Instruction *newInst, Instruction *gravity,
                              bool forceReplication = false) = 0;
  virtual void replaceInstruction(Instruction *newInst, Instruction *oldInst) = 0;
  virtual void deleteInstruction(Instruction *inst) = 0;

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

struct DoallStrategy : public LoopParallelizationStrategy
{
  DoallStrategy(BasicBlock *header=0) : LoopParallelizationStrategy(LPSK_DOALL) { setValidFor(header); }

  virtual void summary(raw_ostream &fout) const
  {
    fout << "DOALL";
  }

  // Update strategies.
  virtual void addInstruction(Instruction *newInst, Instruction *gravity,
                              bool forceReplication = false) {}
  virtual void replaceInstruction(Instruction *newInst, Instruction *oldInst) {}
  virtual void deleteInstruction(Instruction *inst) {};

  virtual void getExecutingStages(Instruction* inst, std::vector<unsigned>& stages) {}
  virtual bool ifI2IsInI1IsIn(Instruction* i1, Instruction* i2) { return true; }

  virtual unsigned getStageNum() const { return 1; }

  static bool classof(const LoopParallelizationStrategy *lps)
  {
    return lps->getKind() == LPSK_DOALL;
  }
};

// syntactic sugar for strat.summary(fout)
raw_ostream &operator<<(raw_ostream &fout, const LoopParallelizationStrategy &strat);

struct PipelineStage
{
  typedef std::set<Instruction*> ISet;
  enum Type
  {
    Sequential = 0,
    Replicable,
    Parallel
  };

  PipelineStage(Type t, const PDG &pdg, const SCCDAG::SCCSet &scc_list);
  PipelineStage(Type t, std::vector<Instruction *> &parallelInstV);

  /// Create a degenerate sequential stage containing all instructions from this loop
  PipelineStage(Loop *loop);

  ISet      replicated;
  ISet      instructions;
  Type      type;
  unsigned  parallel_factor;

  bool communicatesTo(const PipelineStage &other) const;

  void print_txt(raw_ostream &fout, StringRef line_suffix = "") const;

private:
  //sot
  //void print_inst_txt(raw_ostream &fout, ControlSpeculation *ctrlspec, Instruction *inst, const char *line_suffix = 0) const;
  void print_inst_txt(raw_ostream &fout, Instruction *inst, StringRef line_suffix = "") const;
};

// Represents dependences between instructions
// which span pipeline stages.
struct CrossStageDependence
{
  CrossStageDependence(Instruction *s, Instruction *d, DGEdge<Value> *e)
    : src(s), dst(d), edge(e) {}

  Instruction *src, *dst;
  DGEdge<Value> *edge;
};

struct PipelineStrategy : public LoopParallelizationStrategy
{
  PipelineStrategy() : LoopParallelizationStrategy(LPSK_Pipeline) {}

  typedef std::vector<PipelineStage> Stages;
  Stages  stages;

  typedef std::vector<CrossStageDependence> CrossStageDependences;
  CrossStageDependences crossStageDeps;

  virtual void summary(raw_ostream &fout) const;
  void dump_pipeline(raw_ostream &fout, StringRef line_suffix = "") const;

  bool expandReplicatedStages();
  static bool expandReplicatedStages(Stages &stages);

  /*
  // sot: remove for now
  void print_dot(raw_ostream &fout, const PDG &pdg, const SCCs &sccs, ControlSpeculation *ctrlspec=0) const;
  void print_dot(const PDG &pdg, const SCCs &sccs, StringRef dot, StringRef tred, ControlSpeculation *ctrlspec=0) const;
  */

  // Interrogate a pipeline:
  bool mayExecuteInStage(const Instruction *inst, unsigned stageno) const;
  bool mayExecuteInParallelStage(const Instruction *inst) const;

  // Would the dep 'src' -> 'dst' violate pipeline order?
  bool maybeAntiPipelineDependence(const Instruction *src, const Instruction *dst) const;
  // Would the loop-carried dep 'src' -> 'dst' violate a parallel stage?
  bool maybeAntiParallelStageDependence(const Instruction *src, const Instruction *dst) const;

  // Update strategies
  virtual void addInstruction(Instruction *newInst, Instruction *gravity,
                              bool forceReplication = false);
  virtual void replaceInstruction(Instruction *newInst, Instruction *oldInst);
  virtual void deleteInstruction(Instruction *inst);

  virtual void getExecutingStages(Instruction* inst, std::vector<unsigned>& stages);
  // check if I1 is always in the stage that I2 is in
  virtual bool ifI2IsInI1IsIn(Instruction* i1, Instruction* i2);

  virtual unsigned getStageNum() const { return stages.size(); }

  // Sanity check
  /*
  // sot: remove for now, no longer computing partial pdg
  void assertPipelineProperty(const PDG &pdg) const;
  */
  virtual void assertConsistentWithIR(Loop *loop);

  static bool classof(const LoopParallelizationStrategy *lps)
  {
    return lps->getKind() == LPSK_Pipeline;
  }

private:
  /*
  // sot: remove for now, no longer computing partial pdg
  void assertPipelineProperty(const PDG &pdg, const PipelineStage &earlier, const PipelineStage &later) const;
  void assertCheckedPipelineProperty(const PDG &pdg, const PipelineStage &earlier, const PipelineStage &later) const;
  void assertParallelStageProperty(const PDG &pdg, const PipelineStage &parallel, const PipelineStage &other) const;
  */
};


}
}

#endif

