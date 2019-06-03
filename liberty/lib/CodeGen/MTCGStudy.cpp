#define DEBUG_TYPE "mtcg"

#include "llvm/ADT/Statistic.h"

#include "liberty/CodeGen/MTCG.h"

#include "liberty/Utilities/AllocaHacks.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/GepAndLoad.h"
#include "liberty/Speculation/Selector.h"

#include "liberty/CodeGen/PrintStage.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

PreparedStrategy::PreparedStrategy(Loop *L, PipelineStrategy *strat)
  : loop(L), lps(strat)
{
  const unsigned N = numStages();

  instructions.resize(N);
  produces.resize(N);
  consumes.resize(N);
  available.resize(N);
  relevant.resize(N);
  functions.resize(N);

  gatherLiveIns();

  // Force replication of loop-exit branches
  // from sequential stages into the replicated
  // portion of later parallel stages.
  SmallVector<BasicBlock*,1> exitingBlocks;
  L->getExitingBlocks( exitingBlocks );
  const unsigned M = exitingBlocks.size();
  // Foreach sequential stage.
  for(unsigned stageno=0; stageno<N; ++stageno)
  {
    PipelineStage &sequential = strat->stages[stageno];
    if( sequential.type != PipelineStage::Sequential )
      continue;

    // Does this stage include an exiting terminator?
    for(unsigned i=0; i<M; ++i)
    {
      TerminatorInst *exitingTerminator = exitingBlocks[i]->getTerminator();

      if( sequential.instructions.count(exitingTerminator) )
      {
        // Yes.  This sequential stage contains an exiting instruction.
        // We will force-replicate it in all later parallel stages.
        for(unsigned later=stageno+1; later<N; ++later)
        {
          PipelineStage &parallel = strat->stages[later];
          if( parallel.type != PipelineStage::Parallel )
            continue;

          parallel.replicated.insert(exitingTerminator);
        }
      }
    }
  }

  // We want to study each stage so that we know:
  //  (1) The set of instructions that will appear in that stage.
  //  (2) The set of produces in a stage.
  //  (3) The set of consumes in a stage.
  //  (4) The set of values that are available in that stage.
  //  (5) The set of relevant basic blocks for each stage.
  for(unsigned stageno=0; stageno<N; ++stageno)
    study(stageno);
}

void PreparedStrategy::gatherLiveIns()
{
  // Foreach instruction in the loop:
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;

      // If it has an operand which is not defined within the loop.
      for(User::op_iterator k=inst->op_begin(), K=inst->op_end(); k!=K; ++k)
      {
        Value *v = &**k;
        Instruction *iv = dyn_cast< Instruction >(v);
        if( iv && ! loop->contains(iv) )
          liveIns.insert(v);
        else if( isa< Argument >(v) )
          liveIns.insert(v);
      }
    }
  }
}

void PreparedStrategy::study(unsigned stageno)
{
  DEBUG(errs() << "-------------- Study Stage " << stageno << " -------------\n");

  const PipelineStage &stage = lps->stages[stageno];
  assert( stage.type != PipelineStage::Replicable
  && "Should only be sequential/parallel stages at this point.");

  ISet &insts = instructions[stageno];
  VSet &avail = available[stageno];
  BBSet &rel = relevant[stageno];
  ConsumeFrom &cons = consumes[stageno];

  // This stage includes instructions specified in the partition.
  addInstsToStage( insts,avail,rel, stage.instructions );
  studyStage(lps->stages, lps->crossStageDeps, liveIns, available, stageno,
             loop, insts, avail, rel, cons);
  computeProducesAndQueues(cons, stageno, produces, queues);
  assert( !insts.empty() && "How can there be NO instructions in this stage?");
  assert( !rel.empty() && "How can there be NO relevant BBs?");
}

void PreparedStrategy::studyStage(
    const PipelineStrategy::Stages &stages,
    const PipelineStrategy::CrossStageDependences &xdeps, const VSet &liveIns,
    const Stage2VSet &available, unsigned stageno, Loop *loop,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel, ConsumeFrom &cons) {
  addInstsToStage( insts,avail,rel, stages[stageno].replicated );
  avail.insert( liveIns.begin(), liveIns.end() );
  fillOutStage(stages, xdeps, available, stageno, loop, insts, avail, rel,
               cons);
}

void PreparedStrategy::computeProducesAndQueues(
  const ConsumeFrom &cons, unsigned stageno,
  Stage2ProduceTo &produces, StagePairs &queues)
{
  for(ConsumeFrom::const_iterator i=cons.begin(), e=cons.end(); i!=e; ++i)
  {
    Instruction *inst = i->first;
    const ConsumeStartPoint &startPoint = i->second;

    unsigned srcStage = startPoint.first;
    bool     toReplicated = startPoint.second;

    ProduceEndPoint endPoint(stageno,toReplicated);

    produces[srcStage][inst].insert(endPoint);
    queues.insert( StagePair(srcStage,stageno) );
  }
}

void PreparedStrategy::addInstsToStage(ISet &insts, VSet &avail, BBSet &rel, const ISet &is)
{
  for(ISet::const_iterator i=is.begin(), e=is.end(); i!=e; ++i)
    addInstToStage(insts, avail, rel, *i);
}

void PreparedStrategy::addInstToStage(ISet &insts, VSet &avail, BBSet &rel, Instruction *inst)
{
  insts.insert(inst);
  avail.insert(inst);
  rel.insert( inst->getParent() );
}

// Record that a communication must take place
// by adding an entry to this stage's consume set.
// A later call to computeProducesAndQueues will
// fill in the produce sets.
void PreparedStrategy::addCommunication(
  VSet &avail, BBSet &rel, ConsumeFrom &cons,
  unsigned srcStage, unsigned dstStage, Instruction *value,
  bool consumeOccursInReplicatedStage)
{
  ConsumeStartPoint startPoint(srcStage,consumeOccursInReplicatedStage);
  cons[value] = startPoint;
  avail.insert( value );
  rel.insert( value->getParent() );
}

void PreparedStrategy::fillOutStage(
    const PipelineStrategy::Stages &stages,
    const PipelineStrategy::CrossStageDependences &xdeps,
    const Stage2VSet &available, unsigned stageno, Loop *loop,
    // Outputs
    ISet &insts, VSet &avail, BBSet &rel, ConsumeFrom &cons) {
  while( handleControlDeps(xdeps,cons, stageno, insts, avail, rel)
  ||     rematerialize(stages,stageno, loop, insts, avail, rel)
  ||     communicateOnce(stages,stageno, available, insts, avail, rel, cons) )
  { /* Iterate until convergence */ }
}

bool PreparedStrategy::handleControlDeps(
  const PipelineStrategy::CrossStageDependences &xdeps,
  const ConsumeFrom &cons, unsigned stageno,
  // Outputs
  ISet &insts, VSet &avail, BBSet &rel)
{
  bool changed = false;
  // Add transitive control deps (i.e. branches)
  // Foreach control dependence (src->dst):
  for(PipelineStrategy::CrossStageDependences::const_iterator i=xdeps.begin(), e=xdeps.end(); i!=e; ++i)
  {
    const CrossStageDependence &dep = *i;
    if( !dep.edge->isControlDependence() )
      continue;

    // If (dst) will appear in this stage
    if( insts.count(dep.dst) || cons.count(dep.dst) )
    {
      Instruction *srcTerm = dep.src;
      if( !insts.count(srcTerm) )
      {
        // Then so too must be (src)
        DEBUG(errs() << "Relevant to stage " << stageno
                     << " because transitive ctrl deps: "
                     << dep.src->getParent()->getName()
                     << " -> " << *dep.dst << '\n');
        addInstToStage( insts,avail,rel, srcTerm );
        changed = true;
      }
    }
  }

  return changed;
}

bool PreparedStrategy::rematerialize(const PipelineStrategy::Stages &stages,
                                     unsigned stageno, Loop *loop, ISet &insts,
                                     VSet &avail, BBSet &rel) {
  bool changed = false;
  while (rematerializeOnce(stages, stageno, loop, insts, avail, rel))
    changed = true;
  return changed;
}

bool PreparedStrategy::rematerializeOnce(const PipelineStrategy::Stages &stages,
                                         unsigned stageno, Loop *loop,
                                         // Outputs
                                         ISet &insts, VSet &avail, BBSet &rel) {
  // Foreach value which is used, but which is not available.
  for(ISet::iterator i=insts.begin(), e=insts.end(); i!=e; ++i)
  {
    Instruction *inst = *i;

    // If inst in the replicable stage then the replicable stage might not be
    // able to rematerialize as well. Thus, it will communicate instead, but
    // there will be no replicable produce
    // TODO: be less conservative by calling studyStage on the off iteration
    // before the on_iteration (aka the regular study[parallel stage no]) and
    // note the ones that the replicable part cannot rematerialize
    // TODO: maybe delete off_iterations whatsover
    if (stages[stageno].replicated.count(inst))
      continue;

    for(User::op_iterator j=inst->op_begin(), jj=inst->op_end(); j!=jj; ++j)
    {
      Instruction *operand = dyn_cast<Instruction>( &**j );
      if( !operand || avail.count(operand) )
        continue;
      // 'operand' is used, but not available.

      // Can 'operand' be rematerialized instead of communicated?
      if( operand->mayReadOrWriteMemory() )
        continue; // NO. It accesses memory.

      bool allOperandsAreAvailable = true;
      for(User::op_iterator k=operand->op_begin(), kk=operand->op_end(); k!=kk && allOperandsAreAvailable; ++k)
      {
        Instruction *opOfOp = dyn_cast< Instruction >( &**k );
        if( !opOfOp )
          continue;
        if( !avail.count(opOfOp) )
          allOperandsAreAvailable = false;
      }
      if( !allOperandsAreAvailable )
        continue; // NO. Not all operands are available.

      // If operand is phi node in header and stage is parallel do not
      // rematerialize since parallel workers do not execute all iterations.
      // Stale values (from older than the previous iteration, will possibly be
      // feeded to the phi node if rematerialized.
      if (isa<PHINode>(operand) && operand->getParent() == loop->getHeader())
        continue;

      DEBUG(errs() << "rematerialized operand, used but not available: "
                   << *operand << "\n");

      // This instruction may be rematerialized.
      addInstToStage(insts,avail,rel, operand);
      // restart for cascading effect.
      return true;
    }
  }
  return false;
}

bool PreparedStrategy::communicateOnce(
  const PipelineStrategy::Stages &stages, unsigned stageno,
  const Stage2VSet &available,
  // Outputs
  ISet &insts, VSet &avail, BBSet &rel, ConsumeFrom &cons)
{
  // Foreach value which is used, but which is not available.
  for(ISet::iterator i=insts.begin(), e=insts.end(); i!=e; ++i)
  {
    // 'inst' is an instruction in this stage.
    Instruction *inst = *i;
    for(User::op_iterator j=inst->op_begin(), jj=inst->op_end(); j!=jj; ++j)
    {
      // 'operand' is a value used by an instruction in this stage.
      Instruction *operand = dyn_cast<Instruction>( &**j );
      if( !operand || avail.count(operand) )
        continue;
      // 'operand' is used, but not available.

      DEBUG(errs() << "communicated operand, used but not available: "
                   << *operand << "\n");

      // Communicate it!
      unsigned sourceStage=0;
      for(sourceStage=0; sourceStage<stageno; ++sourceStage)
        if( available[sourceStage].count(operand) )
          break;
      assert( sourceStage < stageno
      && "Operand should be available from an *earlier* stage");

      // Does this consume occur in the replicated stage?
      bool consumeOccursInReplicatedStage = ( stages[stageno].replicated.count(inst) );

      addCommunication(avail,rel,cons, sourceStage, stageno, operand, consumeOccursInReplicatedStage);
      return true;
    }
  }
  return false;
}

}
}

