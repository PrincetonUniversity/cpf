/// Determine the /coverage/ of a speculation scheme,
/// by measuring the remaining, non-speculated edges
/// against the profile information.
/// Discrepencies are reported as the limits of coverage.
#ifndef LLVM_LIBERTY_SPEC_PRIV_SPECULATION_COVERAGE_H
#define LLVM_LIBERTY_SPEC_PRIV_SPECULATION_COVERAGE_H

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <set>
#include <map>


#include "liberty/Analysis/CallsiteSearch.h"
#include "Classify.h"

namespace liberty
{
namespace SpecPriv
{


struct Coverage : public ModulePass
{
  static char ID;
  Coverage() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &au) const;

  bool runOnModule(Module &mod);

private:
  bool runOnLoop(raw_ostream &fout, Loop *loop, const HeapAssignment &asgn);

  typedef std::pair< unsigned, unsigned > CoarseEdge;
  struct DetailEdges
  {
    CCPairs flow, anti, output;
  };
  typedef std::map< CoarseEdge, DetailEdges > EdgeDifference;

  void analyze(const PDG &lower, const PDG &upper, EdgeDifference &diff_lc, EdgeDifference &diff_ii) const;

  /// Analyze a loop with a given AA stack, starting
  /// from the given src instruction which may write to memory,
  /// and ending at the given dst instruction, which may read from memory
  /// Produces Loop-Carried FLOW dependences.
  void analyze_loopcarried_flow(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardLoadSearch &dst_reads, CCPairs &out) const;

  /// Analyze a loop with a given AA stack, starting
  /// from the given src instruction which may read from memory,
  /// and ending at the given dst instruction, which may write to memory.
  /// Produces Loop-Carried ANTI dependences.
  void analyze_loopcarried_anti(Loop *loop, LoopAA *aa, Instruction *src, ReverseLoadSearch &src_reads, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &out) const;

  /// Analyze a loop with a given AA stack, starting
  /// from the given src instruction which may write to memory,
  /// and ending at the given dst instruction, which may write to memory.
  /// Produces Loop-Carried and Intra-Iteration OUTPUT dependences.
  void analyze_loopcarried_output(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &out) const;


  /// Analyze a loop with a given AA stack, starting
  /// from the given src instruction which may write to memory,
  /// and ending at the given dst instruction, which may read from memory
  /// Produces Intra-Iteration FLOW dependences.
  void analyze_intraiteration_flow(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardLoadSearch &dst_reads, CCPairs &out) const;

  /// Analyze a loop with a given AA stack, starting
  /// from the given src instruction which may read from memory,
  /// and ending at the given dst instruction, which may write to memory.
  /// Produces Intra-Iteration ANTI dependences.
  void analyze_intraiteration_anti(Loop *loop, LoopAA *aa, Instruction *src, ReverseLoadSearch &src_reads, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &out) const;

  /// Analyze a loop with a given AA stack, starting
  /// from the given src instruction which may write to memory,
  /// and ending at the given dst instruction, which may write to memory.
  /// Produces Intra-Iteration and Intra-Iteration OUTPUT dependences.
  void analyze_intraiteration_output(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &out) const;

  // Various methods to print things in HTML.
  void escape(raw_ostream &fout, const std::vector<Instruction *> &a, const std::vector<Instruction *> &b) const;
  void escape_pstage(raw_ostream &fout, const Vertices &V, const SCCs &sccs) const;
  void escape_sstage(raw_ostream &fout, const Vertices &V, const SCCs &sccs) const;
  void escape(raw_ostream &fout, const Vertices &V, const SCCs::SCC &scc) const;
  void escape(raw_ostream &fout, const std::vector<Instruction *> &inst) const;
  void escape(raw_ostream &fout, const Instruction *inst) const;
  void escape(raw_ostream &fout, const Vertices &V, const EdgeDifference &diff) const;
  void escape(raw_ostream &fout, const CCPairs &pairs) const;
  void escape_ci(raw_ostream &fout, const CtxInst &ci) const;
  void escape_inst(raw_ostream &fout, std::string &buffer) const;
  void escape(raw_ostream &fout, const std::string &buffer) const;
};
}
}

#endif

