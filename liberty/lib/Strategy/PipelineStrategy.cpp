#define DEBUG_TYPE "pipeline"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/FileSystem.h"

#include "liberty/Strategy/PipelineStrategy.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/ModuleLoops.h"
#include "liberty/Utilities/Timer.h"
#include "liberty/Utilities/Tred.h"

//#include "Classify.h"
//#include "Preprocess.h"
//#include "EdmondsKarp.h"

#include <iterator>
#include <set>
#include <unordered_set>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

void PipelineStage::print_inst_txt(raw_ostream &fout, Instruction *inst, StringRef line_suffix) const
{
  fout << "    ";

  if( inst->mayReadOrWriteMemory() )
    fout << "#####  ";
  else if( isa<BranchInst>(inst) || isa<SwitchInst>(inst) || isa<PHINode>(inst) )
    fout << "/////  ";
  else
    fout << "XXXXX  ";

  fout << *inst;
  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';
}

bool PipelineStage::communicatesTo(const PipelineStage &other) const
{
  // TODO
  return true;
}

PipelineStage::PipelineStage(Loop *loop)
  : type( PipelineStage::Sequential ), parallel_factor(1)
{
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
      instructions.insert( &*j );
  }
}


PipelineStage::PipelineStage(Type t, const PDG &pdg, const SCCDAG::SCCSet &scc_list)
  : type(t), parallel_factor(1)
{
  for (auto scc : scc_list)
  {
    for (auto instPair : scc->internalNodePairs())
    {
      Instruction *inst = dyn_cast<Instruction>(instPair.first);
      assert(inst);
      instructions.insert(inst);
    }
  }
}

PipelineStage::PipelineStage(Type t, std::vector<Instruction *> &parallelInstV)
    : type(t), parallel_factor(1)
{
  for (auto &inst : parallelInstV)
    instructions.insert(inst);
}

void PipelineStage::print_txt(raw_ostream &fout, StringRef line_suffix) const
{
  if( ! replicated.empty() )
  {
    fout << "  Replicated Prefix";
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
    for(ISet::const_iterator i=replicated.begin(), e=replicated.end(); i!=e; ++i)
      print_inst_txt(fout, *i, line_suffix);
  }

  switch(type)
  {
    case Sequential:
      fout << "  Sequential Stage";
      if( line_suffix.empty() )
        fout << line_suffix;
      fout << '\n';
      break;
    case Replicable:
      fout << "  Replicable Stage";
      if( line_suffix.empty() )
        fout << line_suffix;
      fout << '\n';
      break;
    case Parallel:
    default:
      assert(parallel_factor > 1);
      fout << "  Parallel Stage, " << parallel_factor << " workers";
      if( line_suffix.empty() )
        fout << line_suffix;
      fout << '\n';
      break;
  }

  for(ISet::const_iterator i=instructions.begin(), e=instructions.end(); i!=e; ++i)
    print_inst_txt(fout, *i, line_suffix);
}

#if 0
// sot: remove for now, no longer computing partial pdg

// Assert that there are no deps which violate pipeline order.
void PipelineStrategy::assertPipelineProperty(const PDG &pdg) const
{
  // Foreach pair (earlier,later) of pipeline stages,
  // where 'earlier' precedes 'later' in the pipeline
  for(Stages::const_iterator i=stages.begin(), e=stages.end(); i!=e; ++i)
  {
    const PipelineStage &earlier = *i;
    for(Stages::const_iterator j=i+1; j!=e; ++j)
    {
      const PipelineStage &later = *j;
      assertPipelineProperty(pdg, earlier,later);
    }
  }

  // Foreach pair (earlier,later) of pipeline stages,
  // where 'earlier' precedes 'later' in the pipeline
  for(Stages::const_iterator i=stages.begin(), e=stages.end(); i!=e; ++i)
  {
    const PipelineStage &earlier = *i;
    for(Stages::const_iterator j=i+1; j!=e; ++j)
    {
      const PipelineStage &later = *j;
      assertCheckedPipelineProperty(pdg, earlier,later);
    }
  }


  // Foreach parallel stage
  for(Stages::const_iterator i=stages.begin(), e=stages.end(); i!=e; ++i)
  {
    const PipelineStage &pstage = *i;
    if( pstage.type != PipelineStage::Parallel )
      continue;

    // If this is a parallel stage,
    // assert that no instruction in this stage
    // is incident on a loop-carried edge.

    assertParallelStageProperty(pdg, pstage, pstage);
/*
    for(Stages::const_iterator j=stages.begin(); j!=e; ++j)
    {
      const PipelineStage &other = *j;
      assertParallelStageProperty(pdg, pstage, other);
    }
*/
  }
}

static bool couldHaveMemdep(const Instruction *a, const Instruction *b)
{
  if( a->mayReadOrWriteMemory() && b->mayReadOrWriteMemory() )
    return (a->mayWriteToMemory() || b->mayWriteToMemory() );
  return false;
}

// Assert that there are no loop-carried edges from
// any instruction in 'parallel' to any instruction
// in 'other'
void PipelineStrategy::assertParallelStageProperty(const PDG &pdg, const PipelineStage &parallel, const PipelineStage &other) const
{
  const Vertices &V = pdg.getV();

  for(PipelineStage::ISet::const_iterator i=parallel.instructions.begin(), e=parallel.instructions.end(); i!=e; ++i)
  {
    Instruction *p = *i;
    Vertices::ID pid = V.get(p);

    for(PipelineStage::ISet::const_iterator j=other.instructions.begin(), z=other.instructions.end(); j!=z; ++j)
    {
      Instruction *q = *j;
      Vertices::ID qid = V.get(q);

      // There should be no loop-carried edge
      if( pdg.hasLoopCarriedEdge(pid,qid) )
      {
        errs() << "\n\n\n"
               << "From: " << *p << '\n'
               << "  to: " << *q << '\n'
               << "From parallel stage:\n";
        parallel.print_txt(errs());
        errs() << "To other stage:\n";
        other.print_txt(errs());

        assert(false && "Violated parallel stage property");
      }

      // Check that we have performed enough memory
      // queries to guarantee the parallel stage property
      if( couldHaveMemdep(p,q) )
      {
        if( pdg.unknownLoopCarried(pid,qid) )
        {
          errs() << "\n\n\n"
                 << "From: " << *p << '\n'
                 << "  to: " << *q << '\n'
                 << "From parallel stage:\n";
          parallel.print_txt(errs());
          errs() << "To other stage:\n";
          other.print_txt(errs());

          // I see you are looking at this assertion.
          // It's complicated, and a half-assed comment
          // would be a disservice.
          // Your best bet is to ask Nick how to fix it.
          assert(false && "We missed a check (parallel)");
        }
      }
    }
  }
}

// We must assert that there is NO dependence from an instruction in 'later'
// to an instruction in 'earlier'
void PipelineStrategy::assertPipelineProperty(const PDG &pdg, const PipelineStage &earlyStage, const PipelineStage &lateStage) const
{
  const Vertices &V = pdg.getV();

  PipelineStage::ISet all_early = earlyStage.instructions;
  all_early.insert( earlyStage.replicated.begin(), earlyStage.replicated.end() );

  PipelineStage::ISet all_late = lateStage.instructions;
  all_early.insert( lateStage.replicated.begin(), lateStage.replicated.end() );

  // For each operation from an earlier stage
  for(PipelineStage::ISet::const_iterator i=all_early.begin(), e=all_early.end(); i!=e; ++i)
  {
    Instruction *early = *i;
    Vertices::ID eid = V.get(early);

    // For each operation from a later stage
    for(PipelineStage::ISet::const_iterator j=all_late.begin(), z=all_late.end(); j!=z; ++j)
    {
      Instruction *late = *j;
      Vertices::ID lid = V.get(late);

      // There should be no backwards dependence
      if( pdg.hasEdge(lid,eid) )
      {
        errs() << "\n\n\n"
               << "From: " << *late << '\n'
               << "  to: " << *early << '\n'
               << "From late stage:\n";
        lateStage.print_txt(errs());
        errs() << "To early stage:\n";
        earlyStage.print_txt(errs());

        assert(false && "Violated pipeline property");
      }
    }
  }
}

void PipelineStrategy::assertCheckedPipelineProperty(const PDG &pdg, const PipelineStage &earlyStage, const PipelineStage &lateStage) const
{
  const Vertices &V = pdg.getV();
  const Loop *loop = V.getLoop();

  PipelineStage::ISet all_early = earlyStage.instructions;
  all_early.insert( earlyStage.replicated.begin(), earlyStage.replicated.end() );

  PipelineStage::ISet all_late = lateStage.instructions;
  all_early.insert( lateStage.replicated.begin(), lateStage.replicated.end() );

  // For each operation from an earlier stage
  for(PipelineStage::ISet::const_iterator i=all_early.begin(), e=all_early.end(); i!=e; ++i)
  {
    Instruction *early = *i;
    Vertices::ID eid = V.get(early);

    // For each operation from a later stage
    for(PipelineStage::ISet::const_iterator j=all_late.begin(), z=all_late.end(); j!=z; ++j)
    {
      Instruction *late = *j;
      Vertices::ID lid = V.get(late);

      // Since we are only checking a fraction of the
      // memory dependences, a natural concern is:
      // did we check /enough/ memory dependences to
      // know that this pipeline is valid?

      // Check that we have performed enough memory
      // queries to determine the absence of a backwards
      // dependence
      if( couldHaveMemdep(late,early) )
      {
        if( loop->contains(late) && loop->contains(early) )
        {
          if( pdg.unknownLoopCarried(lid,eid) )
          {
            errs() << "\n\n\n"
                   << "From: " << *late << '\n'
                   << "  to: " << *early << '\n'
                   << "From late stage:\n";
            lateStage.print_txt(errs());
            errs() << "To early stage:\n";
            earlyStage.print_txt(errs());

            // I see you are looking at this assertion.
            // It's complicated, and a half-assed comment
            // would be a disservice.
            // Your best bet is to ask Nick how to fix it.
            assert(false && "We missed a check (lc pipeline)");
          }
          else if( pdg.unknown(lid,eid) )
          {
            errs() << "\n\n\n"
                   << "From: " << *late << '\n'
                   << "  to: " << *early << '\n'
                   << "From late stage:\n";
            lateStage.print_txt(errs());
            errs() << "To early stage:\n";
            earlyStage.print_txt(errs());

            // I see you are looking at this assertion.
            // It's complicated, and a half-assed comment
            // would be a disservice.
            // Your best bet is to ask Nick how to fix it.
            assert(false && "We missed a check (pipeline)");
          }

        }
      }
    }
  }
}

#endif

void PipelineStrategy::dump_pipeline(raw_ostream &fout, StringRef line_suffix) const
{
  if( !isValid() )
  {
    fout << "Invalid pipeline\n";
    return;
  }

  BasicBlock *header = getHeader();
  Function *fcn = header->getParent();
  fout << "Pipeline for loop " << fcn->getName() << " :: " << header->getName();
  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';

  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    fout << " Stage " << i;
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
    const PipelineStage &stage = stages[i];
    stage.print_txt(fout, line_suffix);
  }

  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';

  /*
  fout << "These deps cross pipeline stages:";
  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';

  for(PipelineStrategy::CrossStageDependences::const_iterator i=crossStageDeps.begin(), e=crossStageDeps.end(); i!=e; ++i)
  {
    const CrossStageDependence &dep = *i;

    dep.edge->print(fout);
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
    fout << "  " << *dep.src;
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
    fout << "  " << *dep.dst;
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
  }

  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';
  */

  fout << "These memory (forward) flows cross pipeline stages (needed for "
          "uncommitted value forwarding) :";
  if (line_suffix.empty())
    fout << line_suffix;
  fout << '\n';

  if (crossStageMemFlows.empty()) {
    fout << " No forward mem flows. No comm required for uncommitted value "
            "forwarding.";
    if (line_suffix.empty())
      fout << line_suffix;
    fout << '\n';
  }

  // collect the values that need to be communicated
  std::unordered_set<Instruction *> commValues;

  for(PipelineStrategy::CrossStageDependences::const_iterator i=crossStageMemFlows.begin(), e=crossStageMemFlows.end(); i!=e; ++i)
  {
    const CrossStageDependence &dep = *i;
    commValues.insert(dep.src);

    dep.edge->print(fout);
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
    fout << "  " << *dep.src;
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
    fout << "  " << *dep.dst;
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';

  }

  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';

  fout << "Uncommitted value forwarding count: " << commValues.size();
  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';

  for (Instruction *val : commValues) {
    fout << "Forwarded value: " << *val;
    if( line_suffix.empty() )
      fout << line_suffix;
    fout << '\n';
  }

  if( line_suffix.empty() )
    fout << line_suffix;
  fout << '\n';
}

bool PipelineStrategy::expandReplicatedStages()
{
  return expandReplicatedStages( this->stages );
}

bool PipelineStrategy::expandReplicatedStages(Stages &stages)
{
  const int N=stages.size();

  // Foreach replicable stage
  for(int i=N-1; i>=0; --i)
  {
    PipelineStage &replicable = stages[i];
    if( replicable.type != PipelineStage::Replicable )
      continue;

    // Foreach later stage which is not replicable.
    unsigned numUsers = 0;
    for(int j=i+1; j<N; ++j)
    {
      PipelineStage &nonReplicable = stages[j];
      if( nonReplicable.type == PipelineStage::Replicable )
        continue;

      // Would there be any communication or synchronization
      // from 'replicable' to 'nonReplicable' ?
      if( replicable.communicatesTo(nonReplicable) )
      {
        numUsers += 1;

        // Merge this replicable stage into the non-replicable stage.
        nonReplicable.replicated.insert(
          replicable.instructions.begin(), replicable.instructions.end() );
      }
    }

    // If the replicable stage is not used by anyone,
    // make it sequential
    if( numUsers < 1 )
      replicable.type = PipelineStage::Sequential;
  }

  // Remove *all* replicable stages from the pipeline.
  unsigned put=0;
  for(int i=0; i<N; ++i)
    if( stages[i].type != PipelineStage::Replicable )
      stages[put++] = stages[i];
  while( stages.size() > put )
    stages.pop_back();

  return (stages.size() != (unsigned) N);
}

void PipelineStrategy::summary(raw_ostream &fout) const
{
  fout << "DSWP[";
  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    if( i > 0 )
      fout << '-';

    switch( stages[i].type )
    {
      case PipelineStage::Sequential:
        fout << 'S';
        break;
      case PipelineStage::Replicable:
        fout << 'R';
        break;
      case PipelineStage::Parallel:
        fout << 'P' << stages[i].parallel_factor;
        break;
    }

    if( !stages[i].replicated.empty() )
      fout << '\'';
  }
  fout << ']';
}

void PipelineStrategy::addInstruction(Instruction *newInst,
                                      Instruction *gravity,
                                      bool forceReplication)
{
  bool added = false;

  if(!gravity)
    return;

  // Search for the gravity instruction;
  // It is either in a stage, or in the replicated
  // prefix of one or more stages.

  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    PipelineStage &stage = stages[i];

    if( stage.replicated.count(gravity) )
    {
      // The gravity instruction exists in
      // the replicated prefix of this stage.
      // Insert the new instruction here too.
      stage.replicated.insert(newInst);
      added = true;
      // And continue, since the instruction
      // may also exist in other replicated
      // prefices.
    }

    else if( stage.instructions.count(gravity) )
    {
      // The gravity instruction exists in this
      // stage.
      // Insert the new instruction here too.
      if (!forceReplication || stage.type != PipelineStage::Parallel)
        stage.instructions.insert(newInst);
      else {
        stage.replicated.insert(newInst);
        if (gravity == newInst)
          stage.instructions.erase(newInst);
      }
      added = true;
      // By construction, the stages are
      // disjoint.  The gravity instruction
      // cannot exist elsewhere.  We are
      // done.
      break;
    }
  }

  // Duplicate control dependences which may
  // exist among the cross-iteration deps
  if (gravity != newInst) {
    for (unsigned i = 0; i < crossStageDeps.size(); ++i) {
      const CrossStageDependence &dep = crossStageDeps[i];
      if (dep.edge->isControlDependence())
        if (dep.dst == gravity) {
          crossStageDeps.push_back(
              CrossStageDependence(dep.src, newInst, dep.edge));
          added = true;
        }
    }
  }

  DEBUG(if( !added )
    errs() << "Warning: pipeline does not include gravity instruction " << *gravity << '\n';);
}

void PipelineStrategy::replaceInstruction(Instruction *newInst, Instruction *oldInst)
{
  bool replaced = false;

  // Search for the gravity instruction;
  // It is either in a stage, or in the replicated
  // prefix of one or more stages.

  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    PipelineStage &stage = stages[i];

    PipelineStage::ISet::iterator ff = stage.replicated.find(oldInst);
    if( ff  != stage.replicated.end() )
    {
      // The gravity instruction exists in
      // the replicated prefix of this stage.
      // Replace it.
      stage.replicated.erase(ff);
      stage.replicated.insert(newInst);
      replaced = true;
      // And continue, since the instruction
      // may also exist in other replicated
      // prefices.
    }

    else
    {
      ff = stage.instructions.find(oldInst);
      if( ff != stage.instructions.end() )
      {
        // The gravity instruction exists in this
        // stage.
        // Replace it.
        stage.instructions.erase(ff);
        stage.instructions.insert(newInst);
        replaced = true;
        // By construction, the stages are
        // disjoint.  The gravity instruction
        // cannot exist elsewhere.  We are
        // done.
        break;
      }
    }
  }

  // the cross-iteration deps
  for(unsigned i=0, N=crossStageDeps.size(); i<N; ++i)
  {
    CrossStageDependence &dep = crossStageDeps[i];
    if( oldInst == dep.src )
    {
      dep.src = newInst;
      replaced = true;
    }
    if( oldInst == dep.dst )
    {
      dep.dst = newInst;
      replaced = true;
    }
  }

  DEBUG(if( !replaced )
    errs() << "Warning: pipeline does not include old instruction " << *oldInst << '\n';);
}

void PipelineStrategy::deleteInstruction(Instruction *inst)
{
  bool deleted = false;

  // Search for the instruction;
  // It is either in a stage, or in the replicated
  // prefix of one or more stages.

  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    PipelineStage &stage = stages[i];

    if( stage.replicated.count(inst) )
    {
      // The instruction exists in
      // the replicated prefix of this stage.
      // Delete the instruction here too.
      stage.replicated.erase(inst);
      deleted = true;
      // And continue, since the instruction
      // may also exist in other replicated
      // prefices.
    }

    else if( stage.instructions.count(inst) )
    {
      // The inst instruction exists in this
      // stage.
      // Delete the instruction here too.
      stage.instructions.erase(inst);
      deleted = true;
      // By construction, the stages are
      // disjoint.  The inst instruction
      // cannot exist elsewhere.  We are
      // done.
      break;
    }
  }

  // Duplicate control dependences which may
  // exist among the cross-iteration deps
  for(unsigned i=0; i<crossStageDeps.size(); ++i)
  {
    const CrossStageDependence &dep = crossStageDeps[i];
    if( dep.edge->isControlDependence() )
      if( dep.dst == inst )
      {
        crossStageDeps.erase( crossStageDeps.begin() + i );
        --i;
        deleted = true;
      }
  }

  DEBUG(if( !deleted)
    errs() << "Warning: pipeline does not include inst instruction " << *inst << '\n';);
}

void PipelineStrategy::getExecutingStages(Instruction* inst, std::vector<unsigned>& executing_stages)
{
  for (unsigned i=0, N=stages.size() ; i<N ; ++i)
  {
    if (mayExecuteInStage(inst, i)) executing_stages.push_back(i);
  }
}

bool PipelineStrategy::ifI2IsInI1IsIn(Instruction *i1, Instruction *i2)
{
  assert( i1 && i2 );

  for (unsigned i=0, N=stages.size() ; i<N ; ++i)
  {
    PipelineStage &stage = stages[i];

    if ( stage.replicated.count(i2) || stage.instructions.count(i2) )
    {
      if ( !stage.replicated.count(i1) && !stage.instructions.count(i1) )
      {
        return false;
      }
    }
  }

  return true;
}

void LoopParallelizationStrategy::assertConsistentWithIR(Loop *loop)
{
  assert( isValid() && "Not a valid parallelization strategy");
  assert( getHeader() == loop->getHeader() && "Wrong loop?");
  loop->verifyLoop();
}

void PipelineStrategy::assertConsistentWithIR(Loop *loop)
{
  bool okay = true;

  // common invariants
  LoopParallelizationStrategy::assertConsistentWithIR(loop);

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  // 1. Ensure that no instruction exists
  // in more than one stage.
  std::set<Instruction*> all_stages;
  for(unsigned i=0, N=stages.size(); i<N; ++i)
  {
    PipelineStage::ISet &stage = stages[i].instructions;
    for(PipelineStage::ISet::const_iterator j=stage.begin(), z=stage.end(); j!=z; ++j)
    {
      Instruction *inst = *j;
      if( all_stages.count(inst) )
      {
        errs() << "\n\n*****************************************\n";
        errs() << "Instruction in more than one stage:\n";
        errs() << "PipelineStrategy inconsistent with IR for loop "
          << fcn->getName() << " :: " << header->getName() << '\n';
        BasicBlock *bb = inst->getParent();
        errs() << "BasicBlock:\n" << *bb << "\nInst:\n" << *inst << '\n';
        okay = false;
      }
      all_stages.insert(inst);
    }
  }

  // 2. Ensure that no instruction exists
  // in both a replicated prefix and a
  // stage.
  {
    std::set<Instruction*> all_replicated;
    for(unsigned i=0, N=stages.size(); i<N; ++i)
    {
      PipelineStage::ISet &prefix = stages[i].replicated;
      for(PipelineStage::ISet::const_iterator j=prefix.begin(), z=prefix.end(); j!=z; ++j)
      {
        Instruction *inst = *j;
        if( all_replicated.count(inst) )
          continue;

        if( all_stages.count(inst) )
        {
          errs() << "\n\n*****************************************\n";
          errs() << "Instruction in both a stage and a replicated prefix:\n";
          errs() << "PipelineStrategy inconsistent with IR for loop "
            << fcn->getName() << " :: " << header->getName() << '\n';
          BasicBlock *bb = inst->getParent();
          errs() << "BasicBlock:\n" << *bb << "\nInst:\n" << *inst << '\n';
          okay = false;
        }

        all_replicated.insert(inst);
        all_stages.insert(inst);
      }
    }
  }

  // 3. Ensure that the set of instructions
  // in this partition is the same as the
  // set of instructions in the loop.
  std::set<Instruction*> all_loop;
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;
    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *inst = &*j;
      if( !all_stages.count(inst) )
      {
        errs() << "\n\n*****************************************\n";
        errs() << "Instruction exists in loop, but not in parallelization strategy:\n";
        errs() << "PipelineStrategy inconsistent with IR for loop "
          << fcn->getName() << " :: " << header->getName() << '\n';
        errs() << "BasicBlock:\n" << *bb << "\nInst:\n" << *inst << '\n';
        okay = false;
      }
      all_loop.insert(inst);
    }
  }
  // We know that all_stages superset-equal loop.
  if( all_loop.size() != all_stages.size() )
  {
    for(std::set<Instruction*>::iterator i=all_stages.begin(), e=all_stages.end(); i!=e; ++i)
    {
      Instruction *inst = *i;
      if( ! all_loop.count(inst) )
      {
        BasicBlock *bb = inst->getParent();

        errs() << "\n\n*****************************************\n";
        errs() << "Instruction exists in parallelization streategy, but not in loop:\n";

        BasicBlock *upred = bb->getUniquePredecessor();
        BasicBlock *usucc = bb->getUniqueSuccessor();
        if( upred && loop->contains(upred) )
        {
          // We allow this because the validation checks for control speculation
          // occur in the branch target, and speculatively dead loop exits
          // cause a validation check OUTSIDE of the loop.  This is okay;
          // the transform must be aware of it.
          // storing redux registers also occurs on loop exits
          errs() << "  (Okay, since this is a loop exit...)\n";
        }
        else if (usucc && usucc == loop->getHeader() && !loop->contains(bb))
        {
          // sot: do not have in stages insts in the preheader or in a split BB
          // between the original preheader and the target loop header
          this->deleteInstruction(inst);
          errs() << " Remove this inst from stages since this is a preheader to loop\n";
        }
        else
        {
          errs() << "PipelineStrategy inconsistent with IR for loop "
            << fcn->getName() << " :: " << header->getName() << '\n';
          errs() << "BasicBlock:\n" << *bb << "\nInst:\n" << *inst << '\n';

          if ( upred )
            errs() << "\nUpred:\n" << *upred << "\n";
          else
          {
            errs() << "\nPred: ";
            for ( pred_iterator pi = pred_begin(bb) ; pi != pred_end(bb) ; pi++)
              errs() << (*pi)->getName() << " ";
            errs() << "\n";
          }
          okay = false;
        }
      }
    }
  }

  // 4. Ensure that all instructions mentioned in crossStageDeps
  // are instructions within the partition.
  for(CrossStageDependences::const_iterator i=crossStageDeps.begin(), e=crossStageDeps.end(); i!=e; ++i)
  {
    const CrossStageDependence &dep = *i;
    if( ! all_stages.count(dep.src) )
    {
      errs() << "\n\n*****************************************\n";
      errs() << "Instruction exists in cross-stage deps, but not in pipeline stages:\n";
      errs() << "PipelineStrategy inconsistent with IR for loop "
        << fcn->getName() << " :: " << header->getName() << '\n';
      BasicBlock *bb = dep.src->getParent();
      errs() << "BasicBlock:\n" << *bb << "\nInst:\n" << *dep.src << '\n';
      okay = false;
    }
    if( ! all_stages.count(dep.dst) )
    {
      errs() << "\n\n*****************************************\n";
      errs() << "Instruction exists in cross-stage deps, but not in pipeline stages:\n";
      errs() << "PipelineStrategy inconsistent with IR for loop "
        << fcn->getName() << " :: " << header->getName() << '\n';
      BasicBlock *bb = dep.dst->getParent();
      errs() << "BasicBlock:\n" << *bb << "\nInst:\n" << *dep.dst << '\n';
      okay = false;
    }
  }

  assert(okay && "sanity test failed.");
}

/*
void PipelineStrategy::print_dot(raw_ostream &fout, const PDG &pdg, const SCCs &sccs, ControlSpeculation *ctrlspec) const
{
  const Vertices &V = pdg.getV();

  fout << "digraph \"Pipeline\" {\n";
  V.print_dot(fout,ctrlspec);

  // Draw stages as subgraphs as well
  for(unsigned stageno=0; stageno<stages.size(); ++stageno)
  {
    const PipelineStage &stage = stages[stageno];

    fout << "subgraph cluster_Stage" << stageno
         << " {\n"
         << "  label=\"Stage " << stageno << "\";\n"
         << "  style=filled;\n";

    if( stage.type == PipelineStage::Sequential )
      fout << "  color=pink;\n";
    else
      fout << "  color=greenyellow;\n";

    std::set<unsigned> already;

    // which SCCs are in this stage?
    for(unsigned sccno=0; sccno<sccs.size(); ++sccno)
    {
      const SCCs::SCC &scc = sccs.get(sccno);
      for(unsigned k=0, Z=scc.size(); k!=Z; ++k)
      {
        Instruction *inst = V.get( scc[k] );

        if( stage.instructions.count(inst) )
        {
          sccs.print_scc_dot(fout,sccno);
          break;
        }
        else if( stage.replicated.count(inst) )
        {
          sccs.print_replicated_scc_dot(fout,sccno,stageno,!already.count(sccno));
          already.insert(sccno);
          break;
        }
      }
    }

    fout << "}\n";
  }

  fout << pdg.getE();
  fout << "}\n";
}

void PipelineStrategy::print_dot(const PDG &pdg, const SCCs &sccs, StringRef dot, StringRef tred, ControlSpeculation *ctrlspec) const
{
  {
    std::error_code ec;
    raw_fd_ostream fout(dot, ec, sys::fs::F_RW);

    print_dot(fout, pdg, sccs, ctrlspec);
  }

  runTred(dot.data(),tred.data());
}

*/

raw_ostream &operator<<(raw_ostream &fout, const LoopParallelizationStrategy &strat)
{
  strat.summary(fout);
  return fout;
}

bool PipelineStrategy::mayExecuteInStage(const Instruction *inst, unsigned stageno) const
{
  const PipelineStage &stage = stages[stageno];

  // Definitely yes: we see it in the partition.
  Instruction *i_swear_i_wont_modify_it = const_cast<Instruction*>(inst);
  if( stage.instructions.count(i_swear_i_wont_modify_it)
  ||  stage.replicated.count(i_swear_i_wont_modify_it) )
    return true;

  // Maybe 'inst' is not in the partition because its hidden in a callsite.
  Function *par_fcn = getHeader()->getParent();
  if( par_fcn == inst->getParent()->getParent() )
    // Not hidden within a callsite => definitely no.
    return false;

  // conservative maybe
  return true;
}

bool PipelineStrategy::mayExecuteInParallelStage(const Instruction *inst) const
{
  for(unsigned stageno=0, N=stages.size(); stageno<N; ++stageno)
    if( stages[stageno].type == PipelineStage::Parallel )
      if( mayExecuteInStage(inst,stageno) )
        return true;
  return false;
}

bool PipelineStrategy::maybeAntiPipelineDependence(const Instruction *src, const Instruction *dst) const
{
  // Consider every stage
  const int N=stages.size();
  for(int j=N-1; j>=0; --j)
    if( mayExecuteInStage(src,j) )
    {
      // 'dst' may execute in several stages (e.g., for replicated stages).
      // Consider every stage
      for(int i=0; i<N; ++i)
        if( mayExecuteInStage(dst,i) )
        {
          if( i < j  )
            return true;
          else  // i>=j and not in an earlier stage
            return false;
        }

      // Conservative maybe: we don't know which stage 'dst' will run in...
      return true;
    }

  // Conservative maybe: we don't know which stage 'src' will run in...
  return true;
}

bool PipelineStrategy::maybeAntiParallelStageDependence(const Instruction *src, const Instruction *dst) const
{
  return mayExecuteInParallelStage(src)
  ||     mayExecuteInParallelStage(dst);
}

}
}
