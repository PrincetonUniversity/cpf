#define DEBUG_TYPE "talkdown-aa"

#include "llvm/ADT/Statistic.h"

#include "liberty/Analysis/LoopAA.h"
#include "liberty/Orchestration/TalkdownAA.h"

#ifndef DEFAULT_PRIV_REMED_COST
#define DEFAULT_PRIV_REMED_COST 100
#endif

namespace liberty
{
namespace AutoMP
{
STATISTIC(numTalkdownQueries, "Number of queries");
STATISTIC(numTalkdownApplicable, "Number of queries applicable");
STATISTIC(numAliasDisproved, "Number of alias queries disproved");

bool TalkdownAA::runOnModule(Module &mod)
{
  const DataLayout &DL = mod.getDataLayout();
  InitializeLoopAA(this, DL);
  LLVM_DEBUG( errs() << "Initialized talkdown-aa\n"; );

  return false;
}

void TalkdownAA::getAnalysisUsage(AnalysisUsage &AU) const
{
  LoopAA::getAnalysisUsage(AU);
  AU.setPreservesAll();
}

LoopAA::AliasResult TalkdownAA::alias(const Value *P1, unsigned S1, TemporalRelation rel,
                          const Value *P2, unsigned S2, const Loop *l,
                          Remedies &R,
                          DesiredAliasResult dAliasResult)
{

  /* auto meta = getMetadataAsStrings( P1 ); */
  /* LLVM_DEBUG(errs() << "Metadata for " << *P1 << "\n";); */
  /* for ( auto &meta_iter : meta ) */
  /* { */
  /*   LLVM_DEBUG(errs() << *meta_iter.first << ": " << *meta_iter.second << "\n";); */
  /* } */
  numTalkdownQueries++;
  LLVM_DEBUG(errs() << "TalkdownAA returned NoAlias from " << *P1 << " to " << *P2 << "\n";);
  return NoAlias;
}

} // namespace AutoMP

static RegisterPass<AutoMP::TalkdownAA> X("talkdown-aa", "Uses programmer annotations to remove dependences", false, true);
static RegisterAnalysisGroup<liberty::LoopAA> Y(X);

char AutoMP::TalkdownAA::ID = 0;

} // namespace liberty
