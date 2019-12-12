// De-virtualization.
// Replace indirect function calls (calls through a pointer) with a switch
// among possible callees.  Plays nicely with control speculation.

// This pass uses two transformation patterns (see transformCallSite):
//
// - A switch instruction which branches to basic blocks, each of which
// performs a direct call.  This is only possible for the
// load-from-constant-table-by-index idiom.
//
// - A sequence of compare-and-branches which dispatch to basic blocks that
// perform direct calls. Less than ideal, but necessary for all other cases,
// since llvm::SwitchInst only accepts ConstantInts as case values.

#define DEBUG_TYPE "devirtualize"

#include "liberty/Analysis/Devirtualize.h"

#include "llvm/IR/Operator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/TypeSanity.h"
#include "liberty/Utilities/FindUnderlyingObjects.h"
#include "liberty/Utilities/InsertPrintf.h"
#include "liberty/Utilities/InstInsertPt.h"

#include <cstdio>

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

//---------------------------------- transformation

static cl::opt<unsigned> SkipAlternativesPerCallsite(
  "specpriv-devirt-max-alternatives", cl::init(16), cl::NotHidden,
  cl::desc("Skip callsites which would require more than N compare-and-branches (or 0 for no limit)."));

static cl::opt<unsigned> TruncateAlternativesPerCallsite(
  "specpriv-devirt-truncate-alternatives", cl::init(0), cl::NotHidden,
  cl::desc("Truncate alternatives when the callsite would require more than N compare-and-branches (or 0 for no limit)."));

static cl::opt<bool> OnlyUnique(
  "specpriv-devirt-only-unique", cl::init(false), cl::NotHidden,
  cl::desc("Only devirtualize when there is exactly one possible target."));

static cl::opt<bool> OnlyPrecise(
  "specpriv-devirt-only-precise", cl::init(false), cl::NotHidden,
  cl::desc("Only devirtualize when a default case is unnecessary."));

static cl::opt<bool> ShowStats(
  "specpriv-devirt-stats", cl::init(false), cl::NotHidden,
  cl::desc("Show devirtualization statistics."));

static cl::opt<bool> TracerBullets(
  "specpriv-devirt-tracer-bullets", cl::init(false), cl::NotHidden,
  cl::desc("Add tracer bullets"));

STATISTIC(numFail,             "Num indirect calls for which no candidates were found.");
STATISTIC(numDevirtualized,    "Num indirect calls devirtualized.");
STATISTIC(numDefaultCase,      "Num devirtualized calls which need a default case.");
STATISTIC(numCmpBr,            "Num compare-and-branch blocks created.");
STATISTIC(numAlternatives,     "Num alternatives created.");
STATISTIC(numSkipped,          "Num indirect calls skipped because too many alternatives (-specpriv-devirt-max-alternatives=).");
STATISTIC(numSkippedUnique,    "Num indirect calls skipped (-specpriv-devirt-only-unique).");
STATISTIC(numSkippedPrecise,   "Num indirect calls skipped (-specpriv-devirt-only-precise).");
STATISTIC(numTruncatedCalls,   "Num indirect calls with truncated alternative lists (-specpriv-devirt-truncate-alternatives=).");
STATISTIC(numTruncatedCallees, "Num alternatives which were truncated by (-specpriv-devirt-truncate-alternatives=).");

static void three_digits(raw_ostream &fout, unsigned n)
{
  char three_digits[4];

  // screw you c++
  snprintf(three_digits,4, "%3d", n);
  fout << three_digits;
}

static void print_percent(raw_ostream &fout,
  unsigned numerator, unsigned denominator,
  char ch, unsigned total_width)
{
  const unsigned percent = ( 1000 * numerator + (denominator-1) ) / denominator;
  const unsigned whole = percent / 10;
  const unsigned frac  = percent % 10;
  three_digits(fout, whole);
  fout << '.' << frac << "% ";

  const unsigned width = ( total_width * numerator + (denominator-1) ) / denominator;

  fout << '(';
  unsigned j;
  for(j=0; j<width; ++j)
    fout << ch;
  for(; j<total_width; ++j)
    fout << ' ';
  fout << ')';
}

typedef std::map<unsigned,unsigned> Histogram;
static void printHistogram(raw_ostream &fout, Histogram &H, unsigned total, unsigned denseBefore = 17)
{
  if( H.empty() )
  {
    fout << "  (none)\n";
    return;
  }

  fout << "Histogram and CDF:\n";
  unsigned cumulative = 0;
  for(unsigned i=0, max=H.rbegin()->first; i<=max; ++i)
  {
    const unsigned n = H[i];
    cumulative += n;

    if( i >= denseBefore && n == 0 )
      continue;

    char ch = '#';
    if( SkipAlternativesPerCallsite > 0
    &&  i > SkipAlternativesPerCallsite )
      ch = 'X';
    else if( TruncateAlternativesPerCallsite > 0
    &&  i > TruncateAlternativesPerCallsite )
      ch = '%';

    three_digits(fout, i);
    fout << ": ";
    print_percent(fout, n, total, ch, 50);
    print_percent(fout, cumulative, total, ch, 50);
    fout << '\n';
  }
}

/// Transform to eliminate indirect function calls
struct Devirtualization : public ModulePass
{
  static char ID;
  Devirtualization() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const
  {
    au.addRequired< DevirtualizationAnalysis >();
  }

  bool runOnModule(Module &mod)
  {
    const bool modified = transform();

    if( ShowStats )
    {
      const DevirtualizationAnalysis &da =
        getAnalysis< DevirtualizationAnalysis >();
      const unsigned total = da.size();

      errs() << "Indirect calls which were transformed:\n";
      printHistogram( errs(), histoTransformed, total );

      errs() << "Indirect calls which were skipped:\n";
      printHistogram( errs(), histoSkipped, total, 0 );
    }

    return modified;
  };

private:
  typedef std::set<const Value*> ValueSet;
  typedef std::vector<BasicBlock*> BlockList;

  Histogram histoTransformed, histoSkipped;

  /// Predicate is true iff a function is not an external declaration.
  struct IsNotExternalDeclaration
  {
    bool operator()(const Function *fcn)
    {
      return ! fcn->isDeclaration();
    }
  };

  bool transform()
  {
    bool modified = false;

    DevirtualizationAnalysis &da = getAnalysis< DevirtualizationAnalysis >();
    for(DevirtualizationAnalysis::iterator i=da.begin(), e=da.end(); i!=e; ++i)
    {
      DevirtualizationAnalysis::Strategy &alts = i->second;

      bool transformed = false;
      if( shouldTransform(alts) )
        transformed = transformCallSite(getCallSite(i->first), alts);

      modified |= transformed;

      if( ShowStats )
      {
        // statistic: number of distinct, non-null call targets
        unsigned total_alts = 0;
        for(unsigned j=0, N=alts.callees.size(); j<N; ++j)
          if( alts.callees[j] )
            ++total_alts;
        if( transformed )
          ++histoTransformed[ total_alts ];
        else
          ++histoSkipped[ total_alts ];
      }
    }


    return modified;
  }

  /// Enforce the user's policy of which callsites to
  /// devirtualize
  bool shouldTransform(DevirtualizationAnalysis::Strategy &alts)
  {
    DevirtualizationAnalysis::FcnList &callees = alts.callees;

    // Optionally, filter which callsites we transform
    if( OnlyUnique )
      if( callees.size() > 1 || alts.requiresDefaultCase )
      {
        ++numSkippedUnique;
        return false;
      }
    if( OnlyPrecise )
      if( alts.requiresDefaultCase )
      {
        ++numSkippedPrecise;
        return false;
      }

    // If the code will be generated as a sequence of compare-and-branch
    // operations (as opposed to a switch instruction), then there is a
    // penalty associated with the number of alteratives.
    // Optionally, enforce limits on the number of alternatives.
    if( alts.dispatch == DevirtualizationAnalysis::Strategy::CompareAndBranch )
    {
      if( SkipAlternativesPerCallsite > 0
      &&  callees.size() > SkipAlternativesPerCallsite )
      {
        LLVM_LLVM_DEBUG(errs() << "Skipping callsite because too many alternatives\n");
        ++numSkipped;
        return false;
      }

      // Choose an ordering for the comparisons.
      // Heuristic: check for functions defined within
      // this module before functions defined outside of this module.
      IsNotExternalDeclaration isNotExternal;
      std::stable_partition(callees.begin(),callees.end(), isNotExternal);

      if( TruncateAlternativesPerCallsite > 0
      &&  callees.size() > TruncateAlternativesPerCallsite )
      {
        LLVM_LLVM_DEBUG(errs() << "Truncating callee list to "
                     << TruncateAlternativesPerCallsite << '\n');

        ++numTruncatedCalls;
        numTruncatedCallees += callees.size() - TruncateAlternativesPerCallsite;

        callees.resize( TruncateAlternativesPerCallsite );
        alts.requiresDefaultCase = true;
      }
    }

    return true;
  }

  bool transformCallSite(const CallSite &cs, DevirtualizationAnalysis::Strategy &alts)
  {
    DevirtualizationAnalysis::FcnList &callees = alts.callees;

    const unsigned N=callees.size();
    const unsigned N_alternatives = alts.requiresDefaultCase ? (N+1) : N;

    LLVM_LLVM_DEBUG(errs() << "Transforming callsite (" << N_alternatives
                 << ") ``" << *cs.getInstruction() << "'':\n");

    if( N == 0 )
    {
      LLVM_LLVM_DEBUG(errs() << "-- No candidates found :(\n");
      ++numFail;
      return false;
    }

    ++numDevirtualized;

    Instruction *inst = cs.getInstruction();

    // Split the basic block BEFORE the indirect function
    // call into two basic blocks: 'before' and 'after'
    BasicBlock *before = inst->getParent();
    BasicBlock *after = before->splitBasicBlock(inst, "after_indirect_call");

    // If the function has a return value, we will need to
    // PHI over each alternative.  This PHI will appear
    // at the top of 'after'
    PHINode *return_value_phi = 0;
    if( ! inst->use_empty() )
    {
      return_value_phi = PHINode::Create(
        inst->getType(), N_alternatives, "result_of_indirect_call");
      InstInsertPt::Beginning(after) << return_value_phi;
    }

    Value *fcn_ptr = cs.getCalledValue();
    Function *fcn = before->getParent();
    LLVMContext &ctx = fcn->getContext();

    // We will create a separate basic block 'alternatives[i]' for each of
    // the possible callees 'callees[i]'.  We populate those blocks with
    // a direct call, and then a branch to 'after'
    std::map<Function*,BasicBlock*> already;
    BasicBlock *default_case = 0;
    BlockList alternatives(N);
    for(unsigned i=0; i<N; ++i)
    {
      Function *callee = callees[i];

      // The load-from-constant-table-via-index idiom might generate
      // some null entries.
      if( !callee )
        continue;

      // Maybe we already generated a block for this target function.
      // (this is possible since load-from-constant-table-via-index
      // may have duplicate entries)
      alternatives[i] = already[ callee ];
      if( alternatives[i] )
        continue;

      ++numAlternatives;
      alternatives[i] = BasicBlock::Create(
        ctx, "direct_call_to_" + callee->getName(), fcn, after);

      makeNewCall(cs, callee, alternatives[i], after, return_value_phi);

      already[ callee ] = alternatives[i]; // don't re-create
      default_case = alternatives[i]; // arbitrary default case
    }

    // In some cases, we must emit a default case, which
    // falls-back to an indirect call.  In other cases,
    // the choice is arbitrary, since it will never execute.
    if( alts.requiresDefaultCase )
    {
      ++numDefaultCase;
      default_case = BasicBlock::Create(ctx, "default_indirect", fcn, after);

      makeNewCall(cs, 0, default_case, after, return_value_phi);
    }

    // Add instructions to dispatch to the appropriate direct call.
    if( alts.dispatch == DevirtualizationAnalysis::Strategy::CompareAndBranch )
    {
      // Worst case: a sequence of compare and branch instructions.
      BlockList compare_and_branch(N);
      for(unsigned i=0; i<N; ++i)
      {
        ++numCmpBr;
        compare_and_branch[i] = BasicBlock::Create(
          ctx, "compare_to_" + callees[i]->getName(), fcn, after);
        compare_and_branch[i]->moveBefore( alternatives[i] );
      }

      // Stitch the compare-and-branch sequences together
      before->getTerminator()->setSuccessor(0, compare_and_branch[0]);
      for(unsigned i=0; i<N; ++i)
      {
        BasicBlock *next = (i == N-1) ? default_case : compare_and_branch[i+1];

        Constant *callee = callees[i];
        if( callee->getType() != fcn_ptr->getType() )
          callee = ConstantExpr::getPointerCast( callee, fcn_ptr->getType() );

        Instruction *cmp = new ICmpInst(ICmpInst::ICMP_EQ, fcn_ptr, callee);
        InstInsertPt::Beginning(compare_and_branch[i])
          << cmp << BranchInst::Create(alternatives[i], next, cmp);
      }
    }

    else if( alts.dispatch == DevirtualizationAnalysis::Strategy::LoadFromConstantTableViaIndex )
    {
      // Best case: a single switch instruction.

      // Create a switch instruction, controlled by
      // the index expression, which dispatches to the
      // appropriate direct call.
      before->getTerminator()->eraseFromParent();
      SwitchInst *sw = SwitchInst::Create( alts.index, default_case, N);
      InstInsertPt::End(before) << sw;

      IntegerType *intty = cast< IntegerType >(alts.index->getType());
      for(unsigned i=0; i<N; ++i)
      {
        if( !alternatives[i] )
          continue;

        sw->addCase(
          ConstantInt::get(intty, i),
          alternatives[i] );
      }
    }

    // Replace all uses with the PHI.
    if( return_value_phi )
      inst->replaceAllUsesWith( return_value_phi );

    // Remove the old call.
    if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
    {
      // if it was an Invoke, then we need to put
      // a new terminator instruction in 'after'.
      BasicBlock *normal = invoke->getNormalDest(),
                 *exceptional = invoke->getUnwindDest();
      exceptional->removePredecessor(after);
      invoke->eraseFromParent();
      InstInsertPt::End(after) << BranchInst::Create(normal);
    }
    else
      inst->eraseFromParent();

    if( TracerBullets )
    {
      Twine location = "Function " + fcn->getName() + " block " + before->getName() + ": %p\n";
      std::string locstr = location.str();
      InstInsertPt where = InstInsertPt::End(before);
      insertPrintf(where,locstr,fcn_ptr,true);
    }

    return true;
  }

  // Create a new call or invoke instruction modeled after the callsite
  // 'cs'.  Place this new call in 'where'.  Branches to 'after', and
  // update 'return_value_phi' if supplied.  If 'callee' is supplied,
  // redirect the call to 'callee'.
  void makeNewCall(const CallSite &cs, Function *callee, BasicBlock *where,
    BasicBlock *after, PHINode *return_value_phi)
  {
    Value *fcn_ptr = cs.getCalledValue();
    Instruction *inst = cs.getInstruction();

    // Copy actual parameters.
    const SmallVector<Value*,4> actuals(cs.arg_begin(), cs.arg_end());

    // What are we going to call?
    Value *target = fcn_ptr;
    if( callee )
    {
      target = callee;
      if( target->getType() != fcn_ptr->getType() )
        target = ConstantExpr::getPointerCast( callee, fcn_ptr->getType() );
    }

    // If this callsite never unwinds
    Value *return_value = 0;
    if( isa< CallInst >(inst) || (callee && callee->doesNotThrow()) )
    {
      // Create and insert new call instruction
     Instruction *new_call = CallInst::Create(
        target, ArrayRef<Value*>(actuals) );

      new_call->setDebugLoc(inst->getDebugLoc());

      return_value = new_call;

      // Branch to 'after'
      InstInsertPt::End( where )
        << new_call << BranchInst::Create( after );
    }

    // Otherwise, if this callsite may unwind.
    else if( InvokeInst *invoke = dyn_cast< InvokeInst >(inst) )
    {
      BasicBlock *exceptional = invoke->getUnwindDest();

      // Create a new invoke
      InvokeInst *new_invoke = InvokeInst::Create(
        target, after, exceptional, ArrayRef<Value*>(actuals) );

      new_invoke->setDebugLoc(inst->getDebugLoc());

      // Update PHIs in the exceptional destination
      for(BasicBlock::iterator j=exceptional->begin(), z=exceptional->end(); j!=z; ++j)
      {
        PHINode *ephi = dyn_cast< PHINode >( &*j );
        if( !ephi )
          break;

        unsigned idx = ephi->getBasicBlockIndex( invoke->getParent() );
        Value *incoming = ephi->getIncomingValue(idx);

        ephi->addIncoming( incoming, where );
      }

      return_value = new_invoke;

      // Add to basic block.
      InstInsertPt::End( where ) << new_invoke;
    }

    // Update the PHI with this return value.
    if( return_value_phi )
      return_value_phi->addIncoming( return_value, where );
  }
};

char Devirtualization::ID = 0;
static RegisterPass<Devirtualization> mpp(
  "devirtualize", "Devirtualize indirect function calls");

}
}
