#ifndef LLVM_LIBERTY_SPECPRIV_MTCG_H
#define LLVM_LIBERTY_SPECPRIV_MTCG_H

#include "liberty/Analysis/ControlSpeculation.h"
#include "liberty/Speculation/LoopDominators.h"
#include "liberty/Speculation/Selector.h"

#include "liberty/Speculation/Api.h"
#include "liberty/CodeGen/Preprocess.h"

#define MTCG_VALUE_DEBUG 0
#define MTCG_CTRL_DEBUG 0

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

// MTCG is conceptually divided into three phases:
//  - study the parallel region (see PreparedStrategy);
//  - create stage functions; and
//  - join those stage functions into an invocation.

typedef std::set<Value*> VSet;
typedef std::map<Value*,Value*> VMap;
typedef std::set<Instruction*> ISet;
typedef std::set<BasicBlock*> BBSet;
typedef std::vector<BasicBlock*> BBVec;

typedef std::vector<Value*> Stage2Value;

// ------------- Study --------------------
// These methods are about 'studying' the parallel region before we transform
// any code.  The purpose of studying it is to determine: which blocks are
// relevant to each stage?  what values are live-in to each stage?  which
// values must be produced/consumed? etc...
//
// NONE of these functions modify the IR.
//
// These methods are defined in MTCGStudy.cpp

/// Augments a PipelineStrategy with additional information
/// computed before code generation.
struct PreparedStrategy
{
  PreparedStrategy(Loop *L, PipelineStrategy *strat);

  typedef std::vector< ISet > Stage2ISet;
  typedef std::vector< VSet > Stage2VSet;
  typedef std::vector< BBSet > Stage2BBSet;
  typedef std::vector< Function* > Stage2Fcn;

  typedef std::pair<unsigned,unsigned> StagePair;
  typedef std::set< StagePair > StagePairs;

  typedef std::pair<unsigned, bool> ConsumeStartPoint;
  typedef std::map<Instruction*,ConsumeStartPoint> ConsumeFrom;
  typedef std::vector< ConsumeFrom > Stage2ConsumeFrom;

  typedef std::pair<unsigned, bool> ProduceEndPoint;
  typedef std::set<ProduceEndPoint> ProduceEndPoints;
  typedef std::map<Instruction*, ProduceEndPoints> ProduceTo;
  typedef std::vector< ProduceTo > Stage2ProduceTo;

  Loop *loop;
  PipelineStrategy *lps;

  VSet liveIns;
  Stage2ISet instructions;
  Stage2ProduceTo produces;
  Stage2ConsumeFrom consumes;
  Stage2VSet available;
  Stage2BBSet relevant;
  Stage2Fcn functions;
  StagePairs queues;

  unsigned numStages() const { return lps->stages.size(); }

  static void studyStage(
    const PipelineStrategy::Stages &stages, const PipelineStrategy::CrossStageDependences &xdeps,
    const VSet &liveIns, const Stage2VSet &available, unsigned stageno,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel, ConsumeFrom &cons);
  //
  /// Update instructions, value availability, and relevant basic blocks.
  static void addInstsToStage(ISet &insts, VSet &avail, BBSet &rel, const ISet &is);
  static void addInstToStage(ISet &insts, VSet &avail, BBSet &rel, Instruction *inst);

private:
  void gatherLiveIns();

  /// Study stage 'stageno' to determine instructions, value availability, communication, etc.
  void study(unsigned stageno);

  /// Given the set of values that each stage consumes,
  /// Compute the set of produced values.
  static void computeProducesAndQueues(const ConsumeFrom &cons, unsigned stageno, Stage2ProduceTo &produces, StagePairs &queues);

  /// Add additional instructions to stage 'stageno' to satisfy register and control dependences.
  static void fillOutStage(
    const PipelineStrategy::Stages &stages, const PipelineStrategy::CrossStageDependences &xdeps, const PreparedStrategy::Stage2VSet &available, unsigned stageno,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel, ConsumeFrom &cons);

  /// Augment a stage with communication, rematerialization, and duplication of control flow.
  static void addCommunication(VSet &avail, BBSet &rel, ConsumeFrom &cons, unsigned srcStage, unsigned dstStage, Instruction *value, bool consumeOccursInReplicatedStage);

  /// Duplicate branches into stage 'stageno' to satisfy control deps.
  static bool handleControlDeps(
    const PipelineStrategy::CrossStageDependences &xdeps,
    const ConsumeFrom &cons,
    unsigned stageno,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel);

  /// Satisfy register deps by rematerializing side-effect-free computations.
  static bool rematerialize(const PipelineStrategy::Stages &stages, unsigned stageno, ISet &insts, VSet &avail, BBSet &rel);

  /// Satisfy register deps with communication.
  static bool communicateOnce(
    const PipelineStrategy::Stages &stages, unsigned stageno,
    const PreparedStrategy::Stage2VSet &available,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel, ConsumeFrom &cons);
  static bool rematerializeOnce(
    const PipelineStrategy::Stages &stages, unsigned stageno,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel);
};


/// @brief Multi-threaded Code Generation Transform.
/// Per Ottoni dissertation, with parallel-stage extensions and support for
/// replicated stages.
struct MTCG : public ModulePass
{
  static char ID;
  MTCG() : ModulePass(ID) {}
  virtual StringRef getPassName() const { return "specpriv-mtcg"; }
  virtual void getAnalysisUsage(AnalysisUsage &au) const;

  virtual bool runOnModule(Module &module);

private:
  typedef std::vector<PreparedStrategy> StrategiesPlus;
  StrategiesPlus elaboratedStrategies;


  // -------------- Transform --------------------
  // These method are about creating functions representing each stage in the pipeline.
  // These methods are defined in MTCG.cpp

  bool runOnStrategy(PreparedStrategy &strategy);
  Function *createFunction(
    Loop *loop, const PipelineStage &stage, unsigned stageno, unsigned N,
    const VSet &liveIns, const PreparedStrategy::StagePairs &queues,
    // Outputs
    Stage2Value &stage2queue, VMap &vmap,
    Value **repId, Value **repFactor);
  Function *createStage(PreparedStrategy &strategy, unsigned stageno, const LoopDom &dt, const LoopPostDom &pdt);
  BasicBlock *createOnIteration(
    PreparedStrategy &strategy, unsigned stageno, const Stage2Value &stage2queue, Function *fcn, VMap &vmap,
    const LoopDom &dt, const LoopPostDom &pdt);
  BasicBlock *createOffIteration(
    Loop *loop, const PipelineStrategy::Stages &stages,
    const VSet &liveIns, const PreparedStrategy::Stage2VSet &available,
    const PipelineStrategy::CrossStageDependences &xdeps, unsigned stageno,
    const Stage2Value &stage2queue, Function *fcn, VMap &vmap,
    const LoopDom &dt, const LoopPostDom &pdt);
  BasicBlock *stitchLoops(
    Loop *loop, // original function
    const VMap &vmap_on, BasicBlock *preheader_on, // First version
    const VMap &vmap_off, BasicBlock *preheader_off, // Second version
    const VSet &liveIns, // loop live-ins
    Value *repId, Value *repFactor // which worker? how many workers?
    ) const;
  void stitchPhi(
    BasicBlock *oldPreheader, PHINode *oldPhi,
    BasicBlock *newPreheader, PHINode *newPhi) const;
  void replaceIncomingEdgesExcept(BasicBlock *oldTarget, BasicBlock *excludePred, BasicBlock *newTarget) const;

  BasicBlock *copyInstructions(
    Loop *loop, unsigned stageno, const ISet &insts,
    const VSet &liveIns,
    const PreparedStrategy::ConsumeFrom &cons, const PreparedStrategy::ProduceTo &prods,
    const Stage2Value &stage2queue, const BBSet &rel,
    const LoopDom &dt, const LoopPostDom &pdt,
    // Outputs
    Function *fcn, VMap &vmap,
    // Optional
    Twine blockNameSuffix = Twine());

  // Compute a metadata node which we will add onto a produce/consume.
  // This allows us to say which produces feed which consumes.
  MDNode *prodConsCorrespondenceTag(
    Loop *loop, unsigned srcStage, unsigned dstStage,
    BasicBlock *block, unsigned posInBlock);
  void insertProduce(Api &api, BasicBlock *atEnd, Value *q, Value *v, MDNode *tag, bool toReplicatedStage);
  Value *insertConsume(Api &api, BasicBlock *atEnd, Value *q, Type *ty, MDNode *tag, bool toReplicatedStage);

  typedef std::map<BasicBlock*, ControlSpeculation::LoopBlock > BB2LB;

  ControlSpeculation::LoopBlock closestRelevantPostdom(BasicBlock *bb, const BBSet &rel, const LoopPostDom &pdt, BB2LB &cache) const;
  ControlSpeculation::LoopBlock closestRelevantDom(BasicBlock *bb, const BBSet &rel, const LoopDom &dt, BB2LB &cache) const;

  void markIterationBoundaries(BasicBlock *preheader);

  // ----------------- Invocation ----------------------
  // These methods take the functions representing each pipeline stage,
  // and then replaces the original loop with an invoke-and-maybe-recover loop.
  // These methods are defined in MTCGInvocation.cpp

  /// This method replaces a loop invocation with a code sequence that will
  /// perform a parallel invocation and possibly perform non-speculative sequential
  /// recovery.
  void createParallelInvocation(PreparedStrategy &strategy);

#if MTCG_EXTRA_DEBUG
  void addDebugInstruction(Instruction* inst, Instruction* clone);
#endif

};

}
}

#endif
