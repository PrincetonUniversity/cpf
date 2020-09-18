#define DEBUG_TYPE "talkdown-aa"

#include "llvm/ADT/Statistic.h"

#include "liberty/Utilities/ReportDump.h"
#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/TalkdownAA.h"
#include "liberty/Talkdown/Talkdown.h"

#ifndef DEFAULT_PRIV_REMED_COST
#define DEFAULT_PRIV_REMED_COST 100
#endif

using namespace AutoMP;

namespace liberty
{
  STATISTIC(numTalkdownAliasQueries, "Number of alias queries");
  STATISTIC(numTalkdownModRefQueries, "Number of modref queries");
  STATISTIC(numTalkdownAliasDisproved, "Number of alias queries disproved");
  STATISTIC(numTalkdownModRefDisproved, "Number of modref queries disproved");

  bool TalkdownAA::runOnModule(Module &mod)
  {
    const DataLayout &DL = mod.getDataLayout();
    InitializeLoopAA(this, DL);
    talkdown = &getAnalysis<Talkdown>();

    return false;
  }

  void TalkdownAA::getAnalysisUsage(AnalysisUsage &AU) const
  {
    LoopAA::getAnalysisUsage(AU);
    AU.addRequired<Talkdown>();
    AU.setPreservesAll();
  }

  LoopAA::AliasResult TalkdownAA::alias(const Value *P1, unsigned S1, TemporalRelation rel,
                            const Value *P2, unsigned S2, const Loop *l,
                            Remedies &R,
                            DesiredAliasResult dAliasResult)
  {
    numTalkdownAliasQueries++;
    // XXX talkdown doesn't handle alias queries???
    // Q: Or do we need to map it back to a source pointer to use it?
    // A: Probably when a remediator has to be added
    return LoopAA::alias(P1, S1, rel, P2, S2, l, R, dAliasResult);

    if ( !isa<Instruction>(P1) || !isa<Instruction>(P2) )
      return LoopAA::alias(P1, S1, rel, P2, S2, l, R, dAliasResult);

    numTalkdownAliasDisproved++;
    return NoAlias;
  }

  LoopAA::ModRefResult TalkdownAA::modref(const Instruction *A,
                            TemporalRelation rel,
                            const Instruction *B,
                            const Loop *L,
                            Remedies &R)
  {
    numTalkdownModRefQueries++;

    /*
     * When we have something like this:
     * #pragma note noelle independent = 1
     *   for (...) {
     *     inst A
     *     #pragma note noelle critical = 1
     *     {
     *       inst B
     *     }
     *     inst C
     *   }
     *  we want the result of any modref query that has A or C as one of its instructions
     *  to be NoModRef
     *  If both instructions passed are within a critical section (read same critical section)
     *  we should chain the query
     */
    const AnnotationSet &as = talkdown->getAnnotationsForInst(A, L);
    const AnnotationSet &bs = talkdown->getAnnotationsForInst(B, L);

    if ( withinAnnotationSet(as, "independent", "1", L) || withinAnnotationSet(bs, "independent", "1", L) )
    {
      numTalkdownModRefDisproved++;
      return NoModRef;
    }

    return LoopAA::modref(A, rel, B, L, R);
  }

  static RegisterPass<TalkdownAA> X("talkdown-aa", "Uses programmer annotations to remove dependences", false, true);
  static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

  char TalkdownAA::ID = 0;

} // namespace liberty
