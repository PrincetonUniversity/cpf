#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/PostDominators.h"

#include "llvm/ADT/iterator_range.h"

#include "liberty/PDG/PDGAnalysis.hpp"

using namespace llvm;
using namespace liberty;

void llvm::PDGAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired< LoopAA >();
  //AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<PostDominatorTreeWrapperPass>();
  //AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.setPreservesAll();
}

bool llvm::PDGAnalysis::runOnModule (Module &M){
  return false;
}

std::unique_ptr<llvm::PDG> llvm::PDGAnalysis::getLoopPDG(Loop *loop) {
  auto pdg = std::make_unique<llvm::PDG>();
  pdg->populateNodesOf(loop);

  LoopAA *aa = getAnalysis< LoopAA >().getTopAA();
  constructEdgesFromMemory(*pdg, loop, aa);

  auto *F = loop->getHeader()->getParent();
  auto &PDT = getAnalysis<PostDominatorTreeWrapperPass>(*F).getPostDomTree();
  constructEdgesFromControl(*pdg, loop, PDT);

  // constructEdgesFromUseDefs adds external nodes for live-ins and live-outs
  constructEdgesFromUseDefs(*pdg, loop);

  return pdg;
}

void llvm::PDGAnalysis::constructEdgesFromUseDefs(PDG &pdg, Loop *loop) {
  for (auto node : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
    Value *pdgValue = node->getT();
    if (pdgValue->getNumUses() == 0)
      continue;

    for (auto &U : pdgValue->uses()) {
      auto user = U.getUser();

      // is argument possible here?
      if (isa<Instruction>(user) || isa<Argument>(user)) {
        const PHINode *phi = dyn_cast<PHINode>(user);
        bool loopCarried = (phi && phi->getParent() == loop->getHeader());

        // add external node if not already there. Used for live-outs
        if (!pdg.isInternal(user))
          pdg.fetchOrAddNode(user, /*internal=*/ false);

        auto edge = pdg.addEdge(pdgValue, user);
        edge->setMemMustType(false, true, DG_DATA_RAW);
        edge->setLoopCarried(loopCarried);
      }
    }

    // add register dependences and external nodes for live-ins
    Instruction *user = dyn_cast<Instruction>(pdgValue);
    assert(user && "A node of loop pdg is not an Instruction");

    // For each user's (loop inst) operand which is not loop instruction,
    // add reg dep (deps among loop insts are already added)
    for (User::op_iterator j = user->op_begin(), z = user->op_end(); j != z;
         ++j) {
      Value *operand = *j;

      if (!pdg.isInternal(operand) &&
          (isa<Instruction>(operand) || isa<Argument>(operand))) {
        pdg.fetchOrAddNode(operand, /*internal=*/false);

        auto edge = pdg.addEdge(operand, user);
        edge->setMemMustType(false, true, DG_DATA_RAW);
      }
    }
  }
}

void llvm::PDGAnalysis::constructEdgesFromControl(
    PDG &pdg, Loop *loop, PostDominatorTree &postDomTree) {
  for (auto bbi = loop->block_begin(); bbi != loop->block_end(); ++bbi) {
    auto &B = **bbi;
    SmallVector<BasicBlock *, 10> dominatedBBs;
    postDomTree.getDescendants(&B, dominatedBBs);

    /*
     * For each basic block that B post dominates, check if B doesn't stricly
     * post dominate its predecessor If it does not, then there is a control
     * dependency from the predecessor to B
     */
    for (auto dominatedBB : dominatedBBs) {
      for (auto predBB :
           make_range(pred_begin(dominatedBB), pred_end(dominatedBB))) {
        if (postDomTree.properlyDominates(&B, predBB))
          continue;
        auto controlTerminator = predBB->getTerminator();
        // TODO: check if this check for loopCarried is enough
        // predBB should also be a loop exiting block
        bool loopCarried = (&B == loop->getHeader());
        for (auto &I : B) {
          auto edge = pdg.addEdge((Value *)controlTerminator, (Value *)&I);
          edge->setControl(true);
          edge->setLoopCarried(loopCarried);
        }
      }
    }
  }
}

void llvm::PDGAnalysis::constructEdgesFromMemory(PDG &pdg, Loop *loop,
                                                 LoopAA *aa) {
  noctrlspec.setLoopOfInterest(loop->getHeader());
  for (auto nodeI : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
    Value *pdgValueI = nodeI->getT();
    Instruction *i = dyn_cast<Instruction>(pdgValueI);
    assert(i && "Expecting an instruction as the value of a PDG node");

    if (!i->mayReadOrWriteMemory())
      continue;

    for (auto nodeJ : make_range(pdg.begin_nodes(), pdg.end_nodes())) {
      Value *pdgValueJ = nodeJ->getT();
      Instruction *j = dyn_cast<Instruction>(pdgValueJ);
      assert(j && "Expecting an instruction as the value of a PDG node");

      if (!j->mayReadOrWriteMemory())
        continue;

      queryLoopCarriedMemoryDep(i, j, loop, aa, pdg);
      queryIntraIterationMemoryDep(i, j, loop, aa, pdg);
    }
  }
}

void llvm::PDGAnalysis::queryMemoryDep(Instruction *src, Instruction *dst,
                                       LoopAA::TemporalRelation FW,
                                       LoopAA::TemporalRelation RV, Loop *loop,
                                       LoopAA *aa, PDG &pdg) {
  if (!src->mayReadOrWriteMemory())
    return;
  if (!dst->mayReadOrWriteMemory())
    return;
  if (!src->mayWriteToMemory() && !dst->mayWriteToMemory())
    return;

  bool loopCarried = FW != RV;

  // forward dep test
  const LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop);
  if (LoopAA::NoModRef == forward)
    return;

  // forward is Mod, ModRef, or Ref

  assert((forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
         !src->mayWriteToMemory() &&
         "forward modref result is mod or modref but src instruction does not "
         "write to memory");

  assert((forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
         !src->mayReadFromMemory() &&
         "forward modref result is ref or modref but src instruction does not "
         "read from memory");

  // reverse dep test
  LoopAA::ModRefResult reverse = forward;

  // in some cases calling reverse is not needed depending on whether dst writes
  // or/and reads to/from memory but in favor of correctness (AA stack does not
  // just check aliasing) instead of performance we call reverse and use
  // assertions to identify accuracy bugs of AA stack
  if (loopCarried || src != dst)
    reverse = aa->modref(dst, RV, src, loop);

  assert((reverse == LoopAA::Mod || reverse == LoopAA::ModRef) &&
         !dst->mayWriteToMemory() &&
         "reverse modref result is mod or modref but dst instruction does not "
         "write to memory");

  assert((reverse == LoopAA::Ref || reverse == LoopAA::ModRef) &&
         !dst->mayReadFromMemory() &&
         "reverse modref result is ref or modref but dst instruction does not "
         "read from memory");

  if (LoopAA::NoModRef == reverse)
    return;

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
  bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
  bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

  if (RAW) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_DATA_RAW);
    edge->setLoopCarried(loopCarried);
  }
  if (WAR) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_DATA_WAR);
    edge->setLoopCarried(loopCarried);
  }
  if (WAW) {
    auto edge = pdg.addEdge((Value *)src, (Value *)dst);
    edge->setMemMustType(true, false, DG_DATA_WAW);
    edge->setLoopCarried(loopCarried);
  }
}

void llvm::PDGAnalysis::queryIntraIterationMemoryDep(Instruction *src,
                                                     Instruction *dst,
                                                     Loop *loop, LoopAA *aa,
                                                     PDG &pdg) {
  if (noctrlspec.isReachable(src, dst, loop))
    queryMemoryDep(src, dst, LoopAA::Same, LoopAA::Same, loop, aa, pdg);
}

void llvm::PDGAnalysis::queryLoopCarriedMemoryDep(Instruction *src,
                                                  Instruction *dst, Loop *loop,
                                                  LoopAA *aa, PDG &pdg) {
  // there is always a feasible path for inter-iteration deps
  // (there is a path from any node in the loop to the header
  //  and the header dominates all the nodes of the loops)

  // only need to check for aliasing and kill-flow

  queryMemoryDep(src, dst, LoopAA::Before, LoopAA::After, loop, aa, pdg);
}
