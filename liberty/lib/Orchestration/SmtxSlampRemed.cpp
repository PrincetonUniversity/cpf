#define DEBUG_TYPE "smtx-slamp-remed"

#include "liberty/Orchestration/SmtxSlampRemed.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEFAULT_SLAMP_REMED_COST 1000

namespace liberty {

using namespace llvm;

STATISTIC(numQueries, "Num queries");
STATISTIC(numEligible, "Num eligible queries");
STATISTIC(numNoFlow, "Num no-flow results");

void SmtxSlampRemedy::apply(Task *task) {
  // TODO: transfer the code for application of smtxSlamp here.
}

bool SmtxSlampRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<SmtxSlampRemedy> smtxRhs =
      std::static_pointer_cast<SmtxSlampRemedy>(rhs);
  if (this->writeI == smtxRhs->writeI)
    return this->readI < smtxRhs->readI;
  return this->writeI < smtxRhs->writeI;
}

static cl::opt<unsigned>
    Threshhold("smtx-slamp-threshhold2", cl::init(0), cl::NotHidden,
               cl::desc("Maximum number of observed flows to report NoModRef"));

static bool isMemIntrinsic(const Instruction *inst) {
  return isa<MemIntrinsic>(inst);
}

static bool intrinsicMayRead(const Instruction *inst) {
  ImmutableCallSite cs(inst);
  StringRef name = cs.getCalledFunction()->getName();
  if (name == "llvm.memset.p0i8.i32" || name == "llvm.memset.p0i8.i64")
    return false;

  return true;
}

/*
void SmtxSlampRemediator::queryAcrossCallsites(
  const Instruction* A,
  LoopAA::TemporalRelation rel,
  const Instruction* B,
  const Loop *L)
{
  std::vector<const Instruction*> writes;
  std::vector<const Instruction*> reads;

  if ( isa<StoreInst>(A) || isMemIntrinsic(A) )
  {
    writes.push_back(A);
  }
  else
  {
    const CallInst* ci = cast<CallInst>(A);
    smtxMan->collectWrites(ci->getCalledFunction(), writes);
  }

  if ( isa<LoadInst>(B) || isMemIntrinsic(B) )
  {
    reads.push_back(B);
  }
  else
  {
    const CallInst* ci = cast<CallInst>(B);
    smtxMan->collectReads(ci->getCalledFunction(), reads);
  }

  for (unsigned i = 0 ; i < writes.size() ; i++)
  {
    for (unsigned j = 0 ; j < reads.size() ; j++)
    {
      IIKey key(A,rel,B,L);
      if (queried[key]) continue;
      queried[key] = true;

      Remediator::getTopRemed()->modref(A,rel,B,L);
    }
  }
}
*/

Remediator::RemedResp SmtxSlampRemediator::memdep(const Instruction *A,
                                                  const Instruction *B,
                                                  bool LoopCarried, bool RAW,
                                                  const Loop *L) {
  ++numQueries;
  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<SmtxSlampRemedy> remedy =
      std::shared_ptr<SmtxSlampRemedy>(new SmtxSlampRemedy());
  remedy->cost = DEFAULT_SLAMP_REMED_COST;

  slamp::SLAMPLoadProfile &slamp = smtxMan->getSlampResult();

  // Slamp profile data is loop sensitive.
  if (!L || !slamp.isTargetLoop(L)) {
    // Inapplicable
    // std::string space(getDepth()+1, ' ');
    // errs() << space << "si\n";
    remedResp.remedy = remedy;
    return remedResp;
  }

  // both instructions should be included in the target loop
  bool includeA = false;
  bool includeB = false;

  for (Loop::block_iterator bi = L->block_begin(); bi != L->block_end(); bi++) {
    if (*bi == A->getParent())
      includeA = true;
    if (*bi == B->getParent())
      includeB = true;
  }

  if (!includeA || !includeB) {
    // Inapplicable
    // std::string space(getDepth()+1, ' ');
    // errs() << space << "si\n";
    remedResp.remedy = remedy;
    return remedResp;
  }

  // Slamp profile data is colected for loads, stores, and callistes.
  // Slamp only collect FLOW info.
  // Thus, we are looking for Store/CallSite -> Load/CallSite

  if (!(isa<StoreInst>(A) || isMemIntrinsic(A) || isa<CallInst>(A))) {
    // Inapplicable
    remedResp.remedy = remedy;
    return remedResp;
  }

  if (!(isa<LoadInst>(B) || (isMemIntrinsic(B) && intrinsicMayRead(B)) ||
        isa<CallInst>(B))) {
    // Inapplicable
    remedResp.remedy = remedy;
    return remedResp;
  }

  ++numEligible;

  remedy->writeI = A;
  remedy->readI = B;

  if (LoopCarried) {

    // Query profile data for a loop-carried flow from A to B
    if (slamp.numObsInterIterDep(L->getHeader(), B, A) <= Threshhold) {
      // No flow.
      ++numNoFlow;
      remedResp.depRes = DepResult::NoDep;

      DEBUG(errs() << "No observed InterIterDep between " << *A << "  and  "
                   << *B << "\n");

      // Keep track of this

      // queryAcrossCallsites(A,Before,B,L);
      smtxMan->setAssumedLC(L, A, B);
    } else if (slamp.isPredictableInterIterDep(L->getHeader(), B, A)) {
      // TODO: produce more fine-grained cost for predictable values

      // No flow.
      ++numNoFlow;
      remedResp.depRes = DepResult::NoDep;

      DEBUG(errs() << "PredictableInterIterDep between " << *A << "  and  "
                   << *B << "\n");

      slamp::PredMap predictions =
          slamp.getPredictions(L->getHeader(), B, A, true);
      for (slamp::PredMap::iterator i = predictions.begin(),
                                    e = predictions.end();
           i != e; ++i) {
        LoadInst *li = i->first;

        if (i->second.type == slamp::LI_PRED) {
          smtxMan->setAssumedLC(L, A, li, B);
        } else if (i->second.type == slamp::LINEAR_PRED) {
          smtxMan->setAssumedLC(L, A, li, B, i->second.a, i->second.b, false);
        } else if (i->second.type == slamp::LINEAR_PRED_DOUBLE) {
          smtxMan->setAssumedLC(L, A, li, B, i->second.a, i->second.b, true);
        } else {
          assert(false);
        }
      }
    } else {
      // errs() << "--- SLAMP failed to speculate\n";
      // errs() << "    src : " ; A->dump();
      // errs() << "    dst : " ; B->dump();

      // slamp.dumpValuePredictionForEdge(L->getHeader(), B, A, true);
    }
  }

  // sot: avoid intra-iter memory speculation with slamp. Presence of speculated
  // II complicates validation process, and potentially forces high false
  // positive rate of misspecs or extensive and regular checkpoints. Benefits
  // for parallelization have not proven to be significant. If further
  // experiments prove otherwise this change might be reverted. Note that
  // neither Hanjun nor Taewook used II mem spec for similar reasons.
  /*
  else {
    // Query profile data for an intra-iteration flow from A to B
    if (slamp.numObsIntraIterDep(L->getHeader(), B, A) <= Threshhold) {
      // No flow
      ++numNoFlow;
      remedResp.depRes = DepResult::NoDep;

      DEBUG(errs() << "No observed IntraIter dep from " << *A << "  and  " << *B
                   << "\n");

      // Keep track of this

      // queryAcrossCallsites(A,Same,B,L);
      smtxMan->setAssumedII(L, A, B);
    } else if (slamp.isPredictableIntraIterDep(L->getHeader(), B, A)) {
      // No flow
      ++numNoFlow;
      remedResp.depRes = DepResult::NoDep;

      DEBUG(errs() << "PredictableIntraIterDep between " << *A << "  and  "
                   << *B << "\n");

      slamp::PredMap predictions =
          slamp.getPredictions(L->getHeader(), B, A, false);
      for (slamp::PredMap::iterator i = predictions.begin(),
                                    e = predictions.end();
           i != e; ++i) {
        LoadInst *li = i->first;

        if (i->second.type == slamp::LI_PRED) {
          smtxMan->setAssumedII(L, A, li, B);
        } else if (i->second.type == slamp::LINEAR_PRED) {
          smtxMan->setAssumedII(L, A, li, B, i->second.a, i->second.b, false);
        } else if (i->second.type == slamp::LINEAR_PRED_DOUBLE) {
          smtxMan->setAssumedII(L, A, li, B, i->second.a, i->second.b, true);
        } else {
          assert(false);
        }
      }
    }
  }
  */
  remedResp.remedy = remedy;
  return remedResp;
}

} // namespace liberty
