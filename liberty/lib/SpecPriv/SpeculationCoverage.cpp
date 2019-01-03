#define DEBUG_TYPE "coverage"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/FileSystem.h"

#include "liberty/Analysis/CallsiteDepthCombinator.h"
#include "liberty/Analysis/KillFlow.h"
#include "liberty/Analysis/EdgeCountOracleAA.h"
#include "liberty/LAMP/LAMPLoadProfile.h"
#include "liberty/LAMP/LampOracleAA.h"
#include "liberty/LoopProf/Targets.h"
#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/DAGSCC.h"
#include "liberty/SpecPriv/PDG.h"
#include "liberty/SpecPriv/PredictionSpeculator.h"
#include "liberty/SpecPriv/Read.h"
#include "liberty/Utilities/ModuleLoops.h"

#include "LocalityAA.h"
#include "PtrResidueAA.h"
#include "SpeculationCoverage.h"

#include <sys/stat.h>

namespace liberty
{
namespace SpecPriv
{


void Coverage::getAnalysisUsage(AnalysisUsage &au) const
{
  au.addRequired< LoopAA >();
  au.addRequired< LAMPLoadProfile >();
  au.addRequired< ModuleLoops >();
  au.addRequired< ReadPass >();
  au.addRequired< KillFlow >();
  au.addRequired< Targets >();
  au.addRequired< ProfileGuidedControlSpeculator >();
  au.addRequired< ProfileGuidedPredictionSpeculator >();
  au.addRequired< PtrResidueSpeculationManager >();
  au.addRequired< Classify >();

  au.setPreservesAll();
}

/*
// Analyze flow deps: write->read
void Coverage::analyze(Loop *loop, LoopAA *aa, xPDG &pdg, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardLoadSearch &dst_reads, bool intraIteration)
{
  KillFlow &kill = getAnalysis< KillFlow >();
  for(InstSearch::iterator i=src_writes.begin(), e=src_writes.end(); i!=e; ++i)
  {
    const CtxInst &write = *i;

    for(InstSearch::iterator j=dst_reads.begin(), z=dst_reads.end(); j!=z; ++j)
    {
      const CtxInst &read = *j;

      if( CallsiteDepthCombinator::mayFlowCrossIter(kill,src,dst,loop,write,read) )
      {
        // Found a cross-iteration flow.
        pdg.loopCarried.flow.push_back( CCPair(write,read) );
      }

      if( intraIteration )
      {
        if( CallsiteDepthCombinator::mayFlowIntraIter(kill,src,dst,loop,write,read) )
        {
          // Found an intra-iteration flow.
          pdg.intraIteration.flow.push_back( CCPair(write,read) );
        }
      }
    }
  }
}

// Analyze output deps: write->write
void Coverage::analyze(Loop *loop, LoopAA *aa, xPDG &pdg, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardStoreSearch &dst_writes, bool intraIteration)
{
  for(InstSearch::iterator i=src_writes.begin(), e=src_writes.end(); i!=e; ++i)
  {
    const CtxInst &write1 = *i;

    for(InstSearch::iterator j=dst_writes.begin(), z=dst_writes.end(); j!=z; ++j)
    {
      const CtxInst &write2 = *j;

      (void)write1;
      (void)write2;
      // Cross-iteration
//      TODO

      // Intra-iteration
      if( intraIteration )
      {
//      TODO
      }
    }
  }
}

// Analyze anti deps: read->write
void Coverage::analyze(Loop *loop, LoopAA *aa, xPDG &pdg, Instruction *src, ReverseLoadSearch &src_reads, Instruction *dst, ForwardStoreSearch &dst_writes, bool intraIteration)
{
  for(InstSearch::iterator i=src_reads.begin(), e=src_reads.end(); i!=e; ++i)
  {
    const CtxInst &write = *i;

    for(InstSearch::iterator j=dst_writes.begin(), z=dst_writes.end(); j!=z; ++j)
    {
      const CtxInst &read = *j;

      (void)write;
      (void)read;
      // Cross-iteration
//      TODO

      // Intra-iteration
      if( intraIteration )
      {
//      TODO
      }
    }
  }
}


// Analyze deps write->x
void Coverage::analyze(Loop *loop, LoopAA *aa, xPDG &pdg, Instruction *src, ReverseStoreSearch &src_writes)
{
  KillFlow &kill = getAnalysis< KillFlow >();
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;

    const bool isFeasibleIntraIteration = isFeasible( src->getParent(), bb, loop );

    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *dst = &*j;

      // Flow: write->read
      if( dst->mayReadFromMemory() )
      {
        ForwardLoadSearch dst_reads(dst,kill);

        analyze(loop,aa,pdg,src,src_writes, dst,dst_reads, isFeasibleIntraIteration);
      }

      // Output: write->write
      if( dst->mayWriteToMemory() )
      {
        ForwardStoreSearch dst_writes(dst,kill);

        analyze(loop,aa,pdg,src,src_writes, dst,dst_writes, isFeasibleIntraIteration);
      }
    }
  }
}

void Coverage::analyze(Loop *loop, LoopAA *aa, xPDG &pdg, Instruction *src, ReverseLoadSearch &src_reads)
{
  KillFlow &kill = getAnalysis< KillFlow >();
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;

    const bool isFeasibleIntraIteration = isFeasible( src->getParent(), bb, loop );

    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *dst = &*j;

      // False: read->read
      if( dst->mayReadFromMemory() )
      {
        // who cares
      }

      // Anti: read->write
      if( dst->mayWriteToMemory() )
      {
        ForwardStoreSearch dst_writes(dst,kill);

        analyze(loop,aa,pdg,src,src_reads, dst,dst_writes, isFeasibleIntraIteration);
      }
    }
  }
}

void Coverage::analyze(Loop *loop, LoopAA *aa, xPDG &pdg)
{
  // This function will very-much resemble CallsiteDepthCombinator::doFlowSearchCrossIter
  // except we are doing flow,anti, and output; and, we are doing both intra-iteration and cross-iteration.

  // Foreach operation in this loop (and its callees) which
  // may access memory...
  KillFlow &kill = getAnalysis< KillFlow >();
  for(Loop::block_iterator i=loop->block_begin(), e=loop->block_end(); i!=e; ++i)
  {
    BasicBlock *bb = *i;

    for(BasicBlock::iterator j=bb->begin(), z=bb->end(); j!=z; ++j)
    {
      Instruction *src = &*j;

      if( src->mayWriteToMemory() )
      {
        ReverseStoreSearch writes(src,kill);

        analyze(loop,aa,pdg,src,writes);
      }

      if( src->mayReadFromMemory() )
      {
        ReverseLoadSearch reads(src,kill);

        analyze(loop,aa,pdg,src,reads);
      }
    }
  }
}
*/

// Analyze flow deps: write->read
void Coverage::analyze_loopcarried_flow(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardLoadSearch &dst_reads, CCPairs &flow) const
{
  KillFlow &kill = getAnalysis< KillFlow >();
  for(InstSearch::iterator i=src_writes.begin(), e=src_writes.end(); i!=e; ++i)
  {
    const CtxInst &write = *i;

    for(InstSearch::iterator j=dst_reads.begin(), z=dst_reads.end(); j!=z; ++j)
    {
      const CtxInst &read = *j;

      if( CallsiteDepthCombinator::mayFlowCrossIter(kill,src,dst,loop,write,read) )
      {
        // Found a cross-iteration flow.
        flow.push_back( CCPair(write,read) );
      }
    }
  }
}

// Analyze output deps: write->write
void Coverage::analyze_loopcarried_output(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &output) const
{
  for(InstSearch::iterator i=src_writes.begin(), e=src_writes.end(); i!=e; ++i)
  {
    const CtxInst &write1 = *i;

    for(InstSearch::iterator j=dst_writes.begin(), z=dst_writes.end(); j!=z; ++j)
    {
      const CtxInst &write2 = *j;

      if( aa->modref(write1.getInst(), LoopAA::Before, write2.getInst(), loop) != LoopAA::NoModRef )
        output.push_back( CCPair(write1,write2) );
    }
  }
}

// Analyze anti deps: read->write
void Coverage::analyze_loopcarried_anti(Loop *loop, LoopAA *aa, Instruction *src, ReverseLoadSearch &src_reads, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &anti) const
{
  for(InstSearch::iterator i=src_reads.begin(), e=src_reads.end(); i!=e; ++i)
  {
    const CtxInst &read = *i;

    for(InstSearch::iterator j=dst_writes.begin(), z=dst_writes.end(); j!=z; ++j)
    {
      const CtxInst &write = *j;

      if( aa->modref(read.getInst(), LoopAA::Before, write.getInst(), loop) != LoopAA::NoModRef )
        anti.push_back( CCPair(read,write) );
    }
  }
}


// Analyze flow deps: write->read
void Coverage::analyze_intraiteration_flow(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardLoadSearch &dst_reads, CCPairs &flow) const
{
  KillFlow &kill = getAnalysis< KillFlow >();
  for(InstSearch::iterator i=src_writes.begin(), e=src_writes.end(); i!=e; ++i)
  {
    const CtxInst &write = *i;

    for(InstSearch::iterator j=dst_reads.begin(), z=dst_reads.end(); j!=z; ++j)
    {
      const CtxInst &read = *j;

      if( CallsiteDepthCombinator::mayFlowIntraIter(kill,src,dst,loop,write,read) )
      {
        // Found a cross-iteration flow.
        flow.push_back( CCPair(write,read) );
      }
    }
  }
}

// Analyze output deps: write->write
void Coverage::analyze_intraiteration_output(Loop *loop, LoopAA *aa, Instruction *src, ReverseStoreSearch &src_writes, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &output) const
{
  for(InstSearch::iterator i=src_writes.begin(), e=src_writes.end(); i!=e; ++i)
  {
    const CtxInst &write1 = *i;

    for(InstSearch::iterator j=dst_writes.begin(), z=dst_writes.end(); j!=z; ++j)
    {
      const CtxInst &write2 = *j;

      if( aa->modref(write1.getInst(), LoopAA::Same, write2.getInst(), loop) != LoopAA::NoModRef )
        output.push_back( CCPair(write1,write2) );
    }
  }
}

// Analyze anti deps: read->write
void Coverage::analyze_intraiteration_anti(Loop *loop, LoopAA *aa, Instruction *src, ReverseLoadSearch &src_reads, Instruction *dst, ForwardStoreSearch &dst_writes, CCPairs &anti) const
{
  for(InstSearch::iterator i=src_reads.begin(), e=src_reads.end(); i!=e; ++i)
  {
    const CtxInst &read = *i;

    for(InstSearch::iterator j=dst_writes.begin(), z=dst_writes.end(); j!=z; ++j)
    {
      const CtxInst &write = *j;

      if( aa->modref(read.getInst(), LoopAA::Same, write.getInst(), loop) != LoopAA::NoModRef )
        anti.push_back( CCPair(read,write) );
    }
  }
}

static std::string name(StringRef prefix, const Loop *loop, StringRef suffix)
{
  std::string result;
  raw_string_ostream sout(result);

  const BasicBlock *header = loop->getHeader();
  const Function *fcn = header->getParent();

  sout << "coverage/" << prefix << "_" << fcn->getName() << "_" << header->getName() << "." << suffix;

  return sout.str();
}

/*
static void dotToSvg(const std::string &dot, const std::string &svg)
{
  StringRef PATH_TO_GRAPHVIZ = "/usr/bin/dot";

  sys::Path binary(PATH_TO_GRAPHVIZ);
  sys::Path input;
  sys::Path output;
  sys::Path error;

  StringRef args[] = {PATH_TO_GRAPHVIZ, "-Tsvg", dot.c_str(), "-o", svg.c_str(), 0};
  const sys::Path *redirects[] = {&input, &output, &error};
  std::string errMsg;

  sys::Program::ExecuteAndWait(binary,args,(const char**)0,redirects,10U,0U,&errMsg);
}
*/

bool Coverage::runOnLoop(raw_ostream &fout, Loop *loop, const HeapAssignment &asgn)
{
  std::string upper_pdg_full_dot = name("upper", loop, "dot");
  std::string upper_pdg_tred_dot = name("upper", loop, "tred");
  std::string upper_pdg_tred_svg = name("upper", loop, "svg");

  std::string lower_pdg_full_dot = name("lower", loop, "dot");
  std::string lower_pdg_tred_dot = name("lower", loop, "tred");
  std::string lower_pdg_tred_svg = name("lower", loop, "svg");

  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();
  errs() << "  Coverage analysis for loop " << fcn->getName() << " :: " << header->getName() << '\n';

  // Want to compare two AA stacks:
  //  Speculated: PtrResidueAA, LocalityAA, PredictionAA, EdgeCountOracle
  //  Speculatable: LampOracle, PredictionAA, EdgeCountOracle

  // Common set of vertices
  Vertices V(loop);

  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
  PtrResidueSpeculationManager &prman = getAnalysis< PtrResidueSpeculationManager >();
  const DataLayout &td = fcn->getParent()->getDataLayout();

  // The upper-bound AA stack
  PDG upper(V,*ctrlspec, predspec);
  SCCs upper_sccs(upper);
  std::string upper_stack;
  std::vector<Instruction*> upper_pstage;
  {
    // Control Speculation
    EdgeCountOracle edgeaa(ctrlspec);
    edgeaa.InitializeLoopAA(this, td);
    // Value predictions
    PredictionAA predaa(&predspec);
    predaa.InitializeLoopAA(this, td);
    // LAMP
    LAMPLoadProfile &lamp = getAnalysis< LAMPLoadProfile >();
    LampOracle lampaa(&lamp);
    lampaa.InitializeLoopAA(this, td);

    // This AA stack includes static analysis, flow dependence speculation.
    LoopAA *aa = lampaa.getTopAA();

    upper.setAA(aa);
    SCCs::computeDagScc(upper, upper_sccs);
    upper_sccs.getUpperBoundParallelStage(upper, upper_pstage);

    raw_string_ostream sout(upper_stack);
    aa->print(sout);

    upper_sccs.print_dot(upper, upper_pdg_full_dot.c_str(), upper_pdg_tred_dot.c_str());
    //dotToSvg(upper_pdg_tred_dot, upper_pdg_tred_svg);
  }

  EdgeDifference diff_lc, diff_ii;

  // The lower-bound stack
  PDG lower(V,*ctrlspec, predspec);
  SCCs lower_sccs(lower);
  std::string lower_stack;
  std::vector<Instruction*> lower_pstage;
  {
    // Control Speculation
    EdgeCountOracle edgeaa(ctrlspec);
    edgeaa.InitializeLoopAA(this, td);
    // Value predictions
    PredictionAA predaa(&predspec);
    predaa.InitializeLoopAA(this, td);
    // Apply locality reasoning (i.e. an object is local/private to a context,
    // and thus cannot source/sink loop-carried deps).
    const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
    LocalityAA localityaa(spresults,asgn);
    localityaa.InitializeLoopAA(this, td);
    // Pointer-residue speculation
    PtrResidueAA residueaa(td,prman);
    residueaa.InitializeLoopAA(this, td);

    // This AA stack includes static analysis, locality, prediction, and control speculation.
    LoopAA *aa = localityaa.getTopAA();

    lower.setAA(aa);
    SCCs::computeDagScc(lower, lower_sccs);
    lower_sccs.getUpperBoundParallelStage(lower, lower_pstage);

    raw_string_ostream sout(lower_stack);
    aa->print(sout);

    lower_sccs.print_dot(lower, lower_pdg_full_dot.c_str(), lower_pdg_tred_dot.c_str());
    //dotToSvg(lower_pdg_tred_dot, lower_pdg_tred_svg);

    // Now, we will find the root cause of the difference.
    // We want to do this BEFORE we destruct the stack.
    // Specifically, we are looking for dependences which
    // exist in the lower stack, but not in the upper stack.
    analyze(lower,upper, diff_lc, diff_ii);
  }

  fout << "<hr/>\n";
  fout << "<h1>Loop " << fcn->getName() << " :: " << header->getName() << "</h1>\n";
  const uint64_t id = (uint64_t) loop;

  fout << "<table><tbody>\n";

  // Which stacks were used?
  fout << "<tr><th>Lower Stack</th><th>Upper Stack</th></tr>\n";
  fout << "<tr><td>\n";
  escape(fout,lower_stack);
  fout << "</td><td>\n";
  escape(fout,upper_stack);
  fout << "</td></tr>\n";

  // Efficiency?
  fout << "<tr><th colspan=\"2\">Efficiency</th></tr>\n";
  fout << "<tr><td>\n";
  lower.pstats(fout);
  fout << "</td><td>\n";
  upper.pstats(fout);
  fout << "</td></tr>\n";

  // PDGs
  fout << "<tr><th colspan=\"2\">Efficiency</th></tr>\n";
  fout << "<tr><td>\n";
  fout << "<img style=\"width: 500px;\" src=\"" << lower_pdg_tred_svg << "\"/>\n";
  fout << "</td><td>\n";
  fout << "<img style=\"width: 500px;\" src=\"" << upper_pdg_tred_svg << "\"/>\n";
  fout << "</td></tr>\n";


  // Parallel stages
  fout << "<tr><th colspan=\"2\">Parallel Stage SCCs";
  fout << " <a onclick=\"document.getElementById('ps" << id << "').style.display='';\">[+]</a>";
  fout << "</th></tr>\n";
  fout << "<tr id=\"ps"<<id<<"\" style=\"display:none;\"><td>\n";
  fout << " <a onclick=\"document.getElementById('ps" << id << "').style.display='none';\">[-]</a>";
  escape_pstage(fout,V,lower_sccs);
  fout << "</td><td>\n";
  escape_pstage(fout,V,upper_sccs);
  fout << " <a onclick=\"document.getElementById('ps" << id << "').style.display='none';\">[-]</a>";
  fout << "</td></tr>\n";

  // Parallel Stage difference
  fout << "<tr><th colspan=\"2\">Parallel Stage, Difference in Instructions</th></tr>\n";
  fout << "<tr><th>Lower-Upper</th><th>Upper-Lower</th></tr>\n";
  fout << "<tr><td>\n";
  escape(fout, lower_pstage, upper_pstage);
  fout << "</td><td class=\"important\">\n";
  escape(fout, upper_pstage, lower_pstage);
  fout << "</td></tr>\n";

  // Sequential stages
  fout << "<tr><th colspan=\"2\">Sequential Stage SCCs";
  fout << " <a onclick=\"document.getElementById('ss" << id << "').style.display='';\">[+]</a>";
  fout << "</th></tr>\n";
  fout << "<tr id=\"ss"<<id<<"\" style=\"display:none;\"><td>\n";
  fout << " <a onclick=\"document.getElementById('ss" << id << "').style.display='none';\">[-]</a>";
  escape_sstage(fout,V,lower_sccs);
  fout << "</td><td>\n";
  escape_sstage(fout,V,upper_sccs);
  fout << " <a onclick=\"document.getElementById('ss" << id << "').style.display='none';\">[-]</a>";
  fout << "</td></tr>\n";




  // Dependence difference
  fout << "<tr><th colspan=\"2\">Dependence Difference, Loop-Carried</th></tr>\n";
  fout << "<tr><th>Lower-Upper</th><th>Upper-Lower</th></tr>\n";
  fout << "<tr><td class=\"important\">\n";
  escape(fout, V, diff_lc);
  fout << "</td><td>\n";
  fout << "<i>(not computed)</i>\n";
  fout << "</td></tr>\n";

  fout << "<tr><th colspan=\"2\">Dependence Difference, Intra-Iteration</th></tr>\n";
  fout << "<tr><th>Lower-Upper</th><th>Upper-Lower</th></tr>\n";
  fout << "<tr><td class=\"important\">\n";
  escape(fout, V, diff_ii);
  fout << "</td><td>\n";
  fout << "<i>(not computed)</i>\n";
  fout << "</td></tr>\n";


  fout << "</tbody></table>\n";

  return false;
}

void Coverage::analyze(const PDG &lower, const PDG &upper, EdgeDifference &diff_lc, EdgeDifference &diff_ii) const
{
  KillFlow &kill = getAnalysis< KillFlow >();

  const Vertices &V = lower.getV();
  Loop *loop = V.getLoop();
  LoopAA *aa = lower.getAA();

  const PartialEdgeSet &lpes = lower.getE();
  const PartialEdgeSet &upes = upper.getE();

  // For each vertex:
  for(Vertices::ID vsrc=0, N=V.size(); vsrc<N; ++vsrc)
  {
    Instruction *isrc = V.get(vsrc);

    if( !isrc->mayReadOrWriteMemory() )
      continue;

    ReverseStoreSearch rss_src(isrc,kill);
    ReverseLoadSearch  rls_src(isrc,kill);

    // For each successor
    for(PartialEdgeSet::iterator vdst=lpes.successor_begin(vsrc), ee=lpes.successor_end(vsrc); vdst!=ee; ++vdst)
    {
      Vertices::ID vidst = *vdst;
      Instruction *idst = V.get(vidst);
      if( !idst->mayReadOrWriteMemory() )
        continue;
      if( !isrc->mayWriteToMemory() && !idst->mayWriteToMemory() )
        continue;

      CoarseEdge key(vsrc,vidst);

      ForwardStoreSearch fss_dst(idst,kill);
      ForwardLoadSearch  fls_dst(idst,kill);

      // Does this edge exist in the other?
      if( lpes.hasLoopCarriedEdge(vsrc, vidst) )
      {
        if( upes.knownLoopCarriedEdge(vsrc, vidst) && !upes.hasLoopCarriedEdge(vsrc, vidst) )
        {
          // This edge exists in the lower, but not in the upper!

          DetailEdges &details_lc = diff_lc[key];
          analyze_loopcarried_flow(  loop,aa, isrc,rss_src, idst,fls_dst, details_lc.flow);
          analyze_loopcarried_anti(  loop,aa, isrc,rls_src, idst,fss_dst, details_lc.anti);
          analyze_loopcarried_output(loop,aa, isrc,rss_src, idst,fss_dst, details_lc.output);
        }
      }

      else if( lpes.hasEdge(vsrc, vidst) )
      {
        if( upes.knownIntraIterationEdge(vsrc, vidst) && !upes.hasEdge(vsrc, vidst) )
        {
          // This edge exists in the lower, but not in the upper!

          DetailEdges &details_ii = diff_ii[key];
          analyze_intraiteration_flow(  loop,aa, isrc,rss_src, idst,fls_dst, details_ii.flow);
          analyze_intraiteration_anti(  loop,aa, isrc,rls_src, idst,fss_dst, details_ii.anti);
          analyze_intraiteration_output(loop,aa, isrc,rss_src, idst,fss_dst, details_ii.output);
        }
      }

    }
  }
}

void Coverage::escape(raw_ostream &fout, const Vertices &V, const EdgeDifference &diff) const
{
  const uint64_t id = (uint64_t) V.getLoop();

  for(EdgeDifference::const_iterator i=diff.begin(), e=diff.end(); i!=e; ++i)
  {
    const CoarseEdge &coarse = i->first;
    const DetailEdges &details = i->second;

    fout << "<p>\n";
    Instruction *tail = V.get( coarse.first );
    escape(fout, tail);
    fout << " &rarr; ";
    Instruction *head = V.get( coarse.second );
    escape(fout, head);

    if( !details.flow.empty() || !details.anti.empty() || !details.output.empty() )
    {
      fout << " <a onclick=\"document.getElementById('details_"<<id<<"_"<<coarse.first<<"_"<<coarse.second<<"').style.display='';\">[+]</a>\n";
      fout << "<div class=\"details\" id=\"details_" << id << "_" << coarse.first << "_" << coarse.second << "\" style=\"display: none;\">\n";
      fout << " <a onclick=\"document.getElementById('details_"<<id<<"_"<<coarse.first<<"_"<<coarse.second<<"').style.display='none';\">[-]</a>\n";
      if( ! details.flow.empty() )
      {
        fout << "Flow:\n";
        escape(fout, details.flow);
      }

      if( ! details.anti.empty() )
      {
        fout << "Anti:\n";
        escape(fout, details.anti);
      }

      if( ! details.output.empty() )
      {
        fout << "Output:\n";
        escape(fout, details.output);
      }

      fout << " <a onclick=\"document.getElementById('details_"<<id<<"_"<<coarse.first<<"_"<<coarse.second<<"').style.display='none';\">[-]</a>\n";
      fout << "</div>\n";
    }
    fout << "</p>\n";


  }

  fout << "&nbsp;\n";
}

bool Coverage::runOnModule(Module &mod)
{
  errs() << "#################################################\n"
         << " Coverage Analysis\n\n\n";
  const Read &spresults = getAnalysis< ReadPass >().getProfileInfo();
  if( !spresults.resultsValid() )
    return false;

  Targets &targets = getAnalysis< Targets >();
  Classify &classify = getAnalysis< Classify >();

  // Create a subdirectory for results
  mkdir("coverage", 0770);

  StringRef filename = "coverage/comparison.html";
  std::error_code ec;
  raw_fd_ostream fout(filename, ec, sys::fs::F_RW);
  fout << "<html>\n";
  fout << "<style type=\"text/css\">\n";
  fout << "span.global { font-weight: bold; }\n";
  fout << "table.quadrants { border-collapse: collapse; }\n";
  fout << "table.quadrants, table.quadrants th, table.quadrants td { border: solid 1px black; }\n";
  fout << "table.edgelist th, table.edgelist td { border: none; text-align:left; }\n";
  fout << "table.edgelist td.tail { text-align: right; width: 24em; }\n";
  fout << "table.edgelist td.head { text-align: left; width: 24em; }\n";
  fout << "div.details { border-left: solid 1em gray; font-size: small; }\n";
  fout << "div.scc { margin-top: 1ex; margin-bottom: 1ex; background: #cccccc; }\n";
  fout << "span.trivial { color:gray; }\n";
  fout << "span.callsite { color:green; }\n";
  fout << "span.memory { color:blue; }\n";
  fout << "span.dead { text-decoration: line-through; }\n";
  fout << "td { vertical-align: top; }\n";
  fout << "td.important { border: solid 1px red; }\n";
  fout << "a:hover { color: white; background: green; }\n";
  fout << "</style>\n";
  fout << "<body>\n";

  ModuleLoops &mloops = getAnalysis< ModuleLoops >();
  for(Targets::iterator i=targets.begin(mloops), e=targets.end(mloops); i!=e; ++i)
  {
    Loop *loop = *i;

    const HeapAssignment &asgn = classify.getAssignmentFor(loop);
    runOnLoop(fout, loop, asgn);
  }

  fout << "</body>\n";
  fout << "</html>\n";

  errs() << "See " << filename << '\n';

  return false;
}

/*
void Coverage::compare(raw_ostream &fout, const xPDG &lesser, const xPDG &greater) const
{
  compare(fout, "intra-iteration", lesser.intraIteration, greater.intraIteration);
  compare(fout, "loop-carried", lesser.loopCarried, greater.loopCarried);
}

void Coverage::compare(raw_ostream &fout, StringRef name, const Edges &lesser, const Edges &greater) const
{
  compare(fout, name, "flow", lesser.flow, greater.flow);
  compare(fout, name, "anti", lesser.anti, greater.anti);
  compare(fout, name, "output", lesser.output, greater.output);
}

void Coverage::compare(raw_ostream &fout, StringRef name, StringRef type, const CCPairs &lesser, const CCPairs &greater) const
{
  fout << "<p>" << name << " " << type << "</p>\n";
  fout << "<table class=\"quadrants\"><tbody>\n";
  fout << "<tr><th></th><th>Greater Absent</th><th>Greater Present</th></tr>\n";
  fout << "<tr><th>Lesser Absent</th><td>X</td><td>\n";
  // Those edges which are present in greater yet absent in lesser
  xAndNotY(fout, greater,lesser);
  fout << "</td></tr>\n";
  fout << "<tr><th>Lesser Present</th><td>\n";
  // Those edges which are present in lesser yet absent in greater
  xAndNotY(fout, lesser,greater);
  fout << "</td><td>X</td></tr>\n";
  fout << "</tbody></table>\n";
}
*/

void Coverage::escape(raw_ostream &fout, const std::vector<Instruction *> &a, const std::vector<Instruction *> &b) const
{
  for(unsigned i=0, N=a.size(); i<N; ++i)
  {
    Instruction *inst = a[i];

    if( std::find(b.begin(), b.end(), inst) != b.end() )
      continue;

    fout << "<p>\n";
    escape( fout, inst );
    fout << "</p>\n";
  }
  fout << "&nbsp;\n";

}
void Coverage::escape_pstage(raw_ostream &fout, const Vertices &V, const SCCs &sccs) const
{
  // count size
  unsigned numParInsts=0, numParSCCs=0;
  unsigned numSeqInsts=0, numSeqSCCs=0;
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;

    if( sccs.mustBeInSequentialStage(scc) )
    {
      numSeqInsts += scc.size();
      ++numSeqSCCs;
    }
    else
    {
      numParInsts += scc.size();
      ++numParSCCs;
    }
  }

  fout << "Parallel stage is " << numParInsts << " instructions over " << numParSCCs << " SCCs.\n";

  // Draw each SCC
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;

    if( sccs.mustBeInSequentialStage(scc) )
      continue;

    escape(fout, V, scc);
  }
}


void Coverage::escape_sstage(raw_ostream &fout, const Vertices &V, const SCCs &sccs) const
{
  // count size
  unsigned numParInsts=0, numParSCCs=0;
  unsigned numSeqInsts=0, numSeqSCCs=0;
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;

    if( sccs.mustBeInSequentialStage(scc) )
    {
      numSeqInsts += scc.size();
      ++numSeqSCCs;
    }
    else
    {
      numParInsts += scc.size();
      ++numParSCCs;
    }
  }

  fout << "Sequential stage is " << numSeqInsts << " instructions over " << numSeqSCCs << " SCCs.\n";

  // Draw each SCC
  for(SCCs::iterator i=sccs.begin(), e=sccs.end(); i!=e; ++i)
  {
    const SCCs::SCC &scc = *i;

    if( !sccs.mustBeInSequentialStage(scc) )
      continue;

    escape(fout, V, scc);
  }


}

void Coverage::escape(raw_ostream &fout, const Vertices &V, const SCCs::SCC &scc) const
{
  fout << "<div class=\"scc\">\n";
  for(SCCs::SCC::const_iterator i=scc.begin(), e=scc.end(); i!=e; ++i)
  {
    Vertices::ID vid = *i;
    const Instruction *inst = V.get(vid);

    fout << "<p>";
    escape(fout, inst);
    fout << "</p>\n";
  }
  fout << "</div>";
}

void Coverage::escape(raw_ostream &fout, const std::vector<Instruction *> &insts) const
{
  fout << insts.size() << '\n';

  for(unsigned i=0, N=insts.size(); i<N; ++i)
  {
    fout << "<p>\n";
    escape( fout, insts[i] );
    fout << "</p>\n";
  }
}

void Coverage::escape_ci(raw_ostream &fout, const CtxInst &ci) const
{
  std::string buffer;
  raw_string_ostream sout(buffer);
  ci.getContext().print(sout);
  escape(fout, sout.str());

  fout << ' ';

  escape(fout, ci.getInst());
}

void Coverage::escape(raw_ostream &fout, const Instruction *inst) const
{
  std::string buffer;
  raw_string_ostream  sout(buffer);
  sout << *inst;


  fout << "<span class=\"";

  if( isa<PHINode>(inst)
  ||  isa<SwitchInst>(inst)
  ||  isa<BranchInst>(inst) )
    fout << "trivial ";

  if( isa<CallInst>(inst)
  ||  isa<InvokeInst>(inst) )
    fout << "callsite ";
  else if( inst->mayReadOrWriteMemory() )
    fout << "memory ";

  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  if( ctrlspec->isSpeculativelyDead(inst) )
    fout << "dead ";

  fout << "\">";

  fout << "<a>";
  escape_inst(fout, buffer);
  fout << "</a>";

  fout << "</span>\n";
}

void Coverage::escape_inst(raw_ostream &fout, std::string &buffer) const
{
  const size_t align = buffer.find(", align ");
  if( align != std::string::npos )
    buffer.erase(align);
  const size_t metadata = buffer.find('!');
  if( metadata != std::string::npos )
    buffer.erase(metadata);
  const size_t comment = buffer.find(';');
  if( comment != std::string::npos )
    buffer.erase(comment);

  escape(fout,buffer);
}

void Coverage::escape(raw_ostream &fout, const std::string &buffer) const
{
  bool spacePrefix = true;
  for(unsigned i=0, N=buffer.size(); i<N; ++i)
  {
    char ch = buffer[i];

    if( spacePrefix && isspace(ch) )
      continue;
    else
      spacePrefix = false;

    if( ch == '<' )
      fout << "&lt;";
    else if( ch == '>' )
      fout << "&gt;";
    else if( ch == '&' )
      fout << "&amp;";
    else if( ch == '\n' )
      fout << "<br/>";
    else if( ch == '(' )
      fout << " (";
    else if( isspace(ch) && buffer[i-1] != ',' && (i+1==N || buffer[i+1] != '(') )
      fout << "&nbsp;";
    else
      fout << ch;
  }
}

void Coverage::escape(raw_ostream &fout, const CCPairs &pairs) const
{
  fout << "<table class=\"edgelist\"><tbody>\n";

  unsigned n=0;
  for(CCPairs::const_iterator i=pairs.begin(), e=pairs.end(); i!=e; ++i)
  {
    const CCPair &edge = *i;

    fout << "<tr>\n";

    fout << "<td class=\"tail\">\n";
    escape_ci(fout, edge.first);
    fout << "</td>\n";

    fout << "<td>&rarr;</td>\n";

    fout << "<td class=\"head\">\n";
    escape_ci(fout, edge.second);
    fout << "</td>\n";

    fout << "</tr>\n";
    ++n;
  }

  fout << "</tbody></table>\n";

  if( 0 == n )
    fout << "<i>(none)</i>\n";
}


/*
void Coverage::xAndNotY(raw_ostream &fout, const CCPairs &x, const CCPairs &y) const
{
  fout << "<table class=\"edgelist\"><tbody>\n";

  unsigned n=0;
  for(CCPairs::const_iterator i=x.begin(), e=x.end(); i!=e; ++i)
  {
    const CCPair &edge = *i;

    if( std::find(y.begin(), y.end(), edge) == y.end() )
    {
      fout << "<tr>\n";

      fout << "<td class=\"tail\">\n";
      escape(fout, edge.first);
      fout << "</td>\n";

      fout << "<td>&rarr;</td>\n";

      fout << "<td class=\"head\">\n";
      escape(fout, edge.second);
      fout << "</td>\n";

      fout << "</tr>\n";
      ++n;
    }
  }

  fout << "</tbody></table>\n";

  if( 0 == n )
    fout << "<i>(none)</i>\n";
}
*/

char Coverage::ID = 0;
static RegisterPass< Coverage > rp("spec-priv-coverage", "SpecPriv Coverage");

}
}
