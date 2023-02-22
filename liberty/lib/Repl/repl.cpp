#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Function.h"

#include "liberty/LoopProf/Targets.h"
#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Orchestration/PSDSWPCritic.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/ReportDump.h"
#include "scaf/Utilities/Metadata.h"


#include "noelle/core/DGBase.hpp"
#include "noelle/core/PDG.hpp"
#include "noelle/core/PDGPrinter.hpp"
#include "noelle/core/SCCDAG.hpp"
#include "noelle/core/LoopDependenceInfo.hpp"
#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/SpeculationModules/SLAMPLoad.h"
#include "scaf/SpeculationModules/SlampOracleAA.h"
#include "noelle/tools/Repl.hpp"

#include <algorithm>
#include <bits/stdint-uintn.h>
#include <cctype>
#include <iomanip>
#include <iostream>
// GNU Readline
#include <readline/history.h>
#include <readline/readline.h>
#include <string>


using namespace llvm;
using namespace std;
using namespace liberty;
using namespace llvm::noelle;

cl::opt<string> HistoryFileName("cpf-repl-history", cl::desc("Specify command history file name"), cl::init(""));

class CpfRepl : public ModulePass {
  public:
    static char ID;
    void getAnalysisUsage(AnalysisUsage &au) const;
    StringRef getPassName() const { return "cpf-repl"; }
    bool runOnModule(Module &M);
    CpfRepl() : ModulePass(ID) {}
};

char CpfRepl::ID = 0;
static RegisterPass<CpfRepl> rp("cpf-repl", "CPF Repl");

void CpfRepl::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<Noelle>();
  au.addRequired< LoopProfLoad >();
  au.addRequired< ProfilePerformanceEstimator >();
  au.addRequired< LoopInfoWrapperPass >();
  au.addRequired<TargetLibraryInfoWrapperPass>();

  if (EnableEdgeProf) {
    au.addRequired<ProfileGuidedControlSpeculator>();
    // au.addRequired<KillFlow_CtrlSpecAware>();
    // au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
  }

  if (EnableLamp) {
    au.addRequired<LAMPLoadProfile>();
    au.addRequired<SmtxSpeculationManager>();
  }

  if (EnableSlamp) {
    au.addRequired<slamp::SLAMPLoadProfile>();
    au.addRequired<SlampOracleAA>();
  }

  au.addRequired<ModuleLoops>();
  au.addRequired<Targets>();

  au.setPreservesAll();
}

class CpfReplDriver: public Repl::ReplDriver {
  private:
    LoopAA* loopAA;
    const Targets &targets;
    ModuleLoops &mloops;
    vector<LoopAA*> loopAAs;
    Pass &pass;
    vector<bool> loopAAEnabled;

    Loop *getSelectedLLVMLoop() {
      auto loops = &pass.getAnalysis<LoopInfoWrapperPass>(*selectedLoop->getLoopStructure()->getFunction()).getLoopInfo();
      auto loop = loops->getLoopFor(selectedLoop->getLoopStructure()->getHeader());
      return loop;
    }

    vector<LoopAA *> initializeSpecModules(Module &M) {

      vector<LoopAA *> specAAs;

      if (EnableSlamp) {
        auto slamp = &pass.getAnalysis<SLAMPLoadProfile>();
        auto slampaa = new SlampOracleAA(slamp);
       // slampaa->InitializeLoopAA(&pass, *DL);
        specAAs.push_back(slampaa);
      }

      // PerformanceEstimator *perf = &pass.getAnalysis<ProfilePerformanceEstimator>();
      if (EnableEdgeProf) {
        // auto ctrlspec =
        //   pass.getAnalysis<ProfileGuidedControlSpeculator>().getControlSpecPtr();
        // auto edgeaa = new EdgeCountOracle(ctrlspec); // Control Spec
        // edgeaa->InitializeLoopAA(&pass, *DL);
        // specAAs.push_back(edgeaa);

        // // auto killflow_aware = &getAnalysis<KillFlow_CtrlSpecAware>(); // KillFlow
        // // auto callsite_aware = &getAnalysis<CallsiteDepthCombinator_CtrlSpecAware>();
        // // // CallsiteDepth

        // // Setup
        // ctrlspec->setLoopOfInterest(loop->getHeader());
        // // killflow_aware->setLoopOfInterest(ctrlspec, loop);
        // // callsite_aware->setLoopOfInterest(ctrlspec, loop);
      }

      return specAAs;
    }

  public:
    CpfReplDriver(Noelle &noelle, Module &m, LoopAA* loopAA, const Targets &targets, ModuleLoops &mloops, Pass &pass) : ReplDriver(noelle, m), loopAA(loopAA), targets(targets), mloops(mloops), pass(pass) {

      auto DL = &m.getDataLayout();
      TargetLibraryInfoWrapperPass *tliWrap = &pass.getAnalysis<TargetLibraryInfoWrapperPass>();
      TargetLibraryInfo *ti = &tliWrap->getTLI();

      // FIXME: handle other loopAAs
      vector<LoopAA *> aas = initializeSpecModules(m);

      for (auto *aa : aas) {
        aa->InitializeLoopAA(DL, ti, loopAA);
      }

      // initialize loop aa
      auto aa = loopAA;
      while (aa) {
        loopAAs.push_back(aa);
        aa = aa->getNextAA();
      }
      unsigned numLoopAAs = loopAAs.size();
      loopAAEnabled = vector<bool>(numLoopAAs, true);

      outs() << "LoopAA (" << numLoopAAs << "): ";
      loopAA->dump();
    }

    string prompt() override {
      stringstream ss;
      ss << "(cpf-repl";
      if (selectedLoopId != -1)
        ss << " loop " << selectedLoopId;
      ss << ") ";
      return ss.str();
    }

    void createLoopMap() override {

      outs() << "CPF create loop map\n";

      // prepare hot loops from the targets
      unsigned loopId = 0;
      for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
          i != e; ++i) {
        Loop *loop = *i;
        LoopStructure loopStructure(loop);
        auto ldi = noelle.getLoop(&loopStructure);
        loopIdMap[loopId++] = ldi;
        continue;
      }
    }

    void dumpFn() override {
      Repl::ReplDriver::dumpFn();

      // if verbose
      if (parser.isVerbose()) {
        // Create internal SCCDAG
        std::vector<Value *> loopInternals;
        for (auto internalNode : selectedPDG->internalNodePairs()) {
          loopInternals.push_back(internalNode.first);
        }

        auto internalPDG = selectedPDG->createSubgraphFromValues(loopInternals, false);
        auto internalSCCDAG = new SCCDAG(internalPDG);
        llvm::noelle::DGPrinter::writeGraph<llvm::noelle::SCCDAG, SCC>("sccdag.dot", internalSCCDAG);
        // FIXME: should the SCCDAG or PDG be deleted?

        // Check if "graph-easy" is a command line command
        if (system("which graph-easy > /dev/null") == 0) {
          // fix the file for graph-easy
          // Go over the file and replace "...:s[digits] " with "..."
          ifstream in("sccdag.dot");
          ofstream out("sccdag.for-graph-easy.dot");
          string line;
          while (getline(in, line)) {
            size_t pos, pos2;
            pos = line.find(":s");
            // find the first space after pos
            if (pos != string::npos) {
              pos2 = line.find(" ", pos);
              if (pos2 != string::npos) {
                line.erase(pos, pos2 - pos);
              }
            }
            // trim all attributes
            pos = line.find("[");
            if (pos != string::npos) {
              //  find the last "]"
              pos2 = line.rfind("]");
              if (pos2 != string::npos) {
                line.erase(pos, pos2 - pos + 1);
              }
            }
            out << line << endl;
          }

          // call "graph-easy" command to visualize the SCCDAG
          system("graph-easy --as boxart --input sccdag.for-graph-easy.dot");
        }
        else {
          outs() << "Cannot visualize the SCCDAG because 'graph-easy' is not installed.\n";
        }

      }
    }

    void loopsFn() override {

      outs() << "List of hot loops:\n";

      auto &load = pass.getAnalysis< LoopProfLoad >();

      for (auto &[loopId, loop] : loopIdMap) {
        auto header = loop->getLoopStructure()->getHeader();
        // get loop id and print it
        outs() << loopId;

        auto loopNamerId = Namer::getBlkId(header);
        if (loopNamerId != -1)
          outs() << " (" << loopNamerId << ")";


        outs() << ": " << header->getParent()->getName()
          << "::" << header->getName();
        Instruction *term = header->getTerminator();
        if (term)
          liberty::printInstDebugInfo(term);

        char percent[10];
        const unsigned long loop_time = load.getLoopTime(header);

        snprintf(percent,10, "%.1f", 100.0 * loop_time / load.getTotTime());
        errs() << "\tTime " << loop_time << " / " << load.getTotTime()
          << " Coverage: " << percent << "%\n";
      }
    }

    void instsFn() override {

      auto printDebug = parser.isVerbose();
      int queryInstId = parser.getActionId();
      if (selectedLoopId == -1) {
        errs() << "No loop selected\n";
        return;
      }

      // print the selected instruction
      if (queryInstId != -1) {
        bool found = false;
        for (auto &[instId, node] : *instIdMap) {
          auto *inst = dyn_cast<Instruction>(node->getT());
          auto instNamerId = Namer::getInstrId(inst);
          if (queryInstId == instNamerId) {
            outs() << instId << " (" << queryInstId << ")\t" << *inst;
            if (printDebug) {
              liberty::printInstDebugInfo(inst);
            }
            outs() << "\n";
            found = true;
            break;
          }
        }

        if (!found) {
          outs() << "Instruction with NamerId " << queryInstId
                 << " not found\n";
        }
      } else { // print all instructions
        // if instructions are load, store, or call, print it with colors
        // store: red; load: green; call: blue

        // map the color to terminal color strings
        map<unsigned int, string> instColorMap = {
          {Instruction::Store, "\033[1;31m"},
          {Instruction::Load, "\033[1;32m"},
          {Instruction::Call, "\033[1;34m"},
          {Instruction::Invoke, "\033[1;34m"},
        };
        for (auto &[instId, node] : *instIdMap) {
          auto *inst = dyn_cast<Instruction>(node->getT());
          // not an instruction
          if (!inst) {
            outs() << instId << "\t" << *node->getT() << "\n";
            continue;
          }

          // set color if it is a load, store, or call
          auto op = inst->getOpcode();
          if (instColorMap.find(op) != instColorMap.end()) {
            auto color = instColorMap[op];
            // FIXME: ignore if it is a call to "llvm.dbg.value"
            if (op == Instruction::Call) {
              auto *call = dyn_cast<CallInst>(inst);
              auto *callee = call->getCalledFunction();
              if (callee && callee->getName().startswith("llvm.dbg.value")) {
                color = "";
              }
            }
            outs() << color;
          }

          auto instNamerId = Namer::getInstrId(inst);
          outs() << instId << " (" << instNamerId << ")\t" << *node->getT();

          if (printDebug) {
            liberty::printInstDebugInfo(inst);
          }
          outs() << "\033[0m";

          outs() << "\n";
        }
      }
    }

    // FIXME: duplicated code with Planner
    // intialize additional remedieators
    // these remediators should not be on the SCAF AA stack
    std::vector<Remediator_ptr> getAvailableRemediators(Loop *A) {
      std::vector<Remediator_ptr> remeds;

      if (EnableSlamp) {
        auto slamp = &pass.getAnalysis<SLAMPLoadProfile>();
        auto slampaa = std::make_unique<SlampOracleAA>(slamp);
        remeds.push_back(std::move(slampaa));
      }

      if (EnableLamp) {
        auto lamp = &pass.getAnalysis<LAMPLoadProfile>();
        auto lampaa = std::make_unique<LampOracle>(lamp);
        remeds.push_back(std::move(lampaa));
      }

      /* produce remedies for control deps */
      if (EnableEdgeProf) {
        auto ctrlspec = pass.getAnalysis<ProfileGuidedControlSpeculator>()
                            .getControlSpecPtr();
        ctrlspec->setLoopOfInterest(A->getHeader());
        auto ctrlSpecRemed = std::make_unique<ControlSpecRemediator>(ctrlspec);
        ctrlSpecRemed->processLoopOfInterest(A);
        remeds.push_back(std::move(ctrlSpecRemed));
      }

      /* produce remedies for register deps */
      // reduction remediator
      auto mloops = &pass.getAnalysis<ModuleLoops>();

      auto reduxRemed =
          std::make_unique<ReduxRemediator>(mloops, loopAA, selectedPDG.get());
      reduxRemed->setLoopOfInterest(A);
      remeds.push_back(std::move(reduxRemed));

      remeds.push_back(std::make_unique<MemVerRemediator>());
      remeds.push_back(std::make_unique<TXIOAA>());
      // remeds.push_back(std::make_unique<CommutativeLibsAA>());

      return remeds;
    }

    // optimize the selected loop with speculation
    void speculativelyOptimizeFn() {
      // get the list of remediators
      Loop* loop = getSelectedLLVMLoop();
      auto remediators = getAvailableRemediators(loop);

      // optimize the PDG by marking removeable    // modify the PDG by annotating the remedies
      Criticisms allCriticisms = Critic::getAllCriticisms(*selectedPDG);
      for (auto &remediator : remediators) {
        Remedies remedies = remediator->satisfy(*selectedPDG, loop, allCriticisms);
        // for each remedy
        for (Remedy_ptr r : remedies) {
          unsigned long tcost = 0;
          Remedies_ptr remedSet = std::make_shared<Remedies>();

          // expand remedy if there's subremedies
          // TODO: is this even necessary? MemSpecAARemed uses subRemedies, but if
          // no other ones are using it, shound we remove this concept to reduce
          // the complexity
          std::function<void(Remedy_ptr)> expandSubRemedies =
            [remedSet, &tcost, &expandSubRemedies](Remedy_ptr r) {
              if (r->hasSubRemedies()) {
                for (Remedy_ptr subR : *r->getSubRemedies()) {
                  remedSet->insert(subR);
                  tcost += subR->cost;
                  expandSubRemedies(subR);
                }
              } else {
                remedSet->insert(r);
                tcost += r->cost;
              }
            };

          expandSubRemedies(r);

          // for each criticism that is satisfied by the remedy
          // might be multiple ones because one remedy can solve multiple criticisms
          for (Criticism *c : r->resolvedC) {
            // remedies are added to the edges.
            c->setRemovable(true);
            c->addRemedies(remedSet);
          }
        }
      }
    }

    static double getWeight(SCC *scc, PerformanceEstimator *perf) {
      double sumWeight = 0.0;

      for (auto instPair : scc->internalNodePairs()) {
        Instruction *inst = dyn_cast<Instruction>(instPair.first);
        assert(inst);

        sumWeight += perf->estimate_weight(inst);
      }

      return sumWeight;
    }

    static bool isParallel(const SCC &scc) {
      for (auto edge : make_range(scc.begin_edges(), scc.end_edges())) {
        if (!scc.isInternal(edge->getIncomingT()) ||
            !scc.isInternal(edge->getOutgoingT()))
          continue;

        if (edge->isLoopCarriedDependence()) {
          return false;
        }
      }
      return true;
    }

    struct CoverageStats {
      double TotalWeight;
      double LargestSeqWeight;
      double ParallelWeight;
      double SequentialWeight;

      std::string dumpPercentage() {
        if (TotalWeight == 0) {
          return "Coverage incomplete: loop weight is 0\n";
        }

        double ParallelPercentage = 100 * ParallelWeight / TotalWeight;
        double SequentialPercentage = 100 * SequentialWeight / TotalWeight;
        double CriticalPathPercentage = 100 * LargestSeqWeight / TotalWeight;
        double ParallelismCoverage = 100 - CriticalPathPercentage;

        std::stringstream ss;
        ss << "Largest Seq SCC (%): " << std::fixed << std::setw(2)
          << CriticalPathPercentage << "\n"
          << "Parallel SCC (%): " << ParallelPercentage << "\n"
          << "Sequential SCC (%): " << SequentialPercentage << "\n"
          << "Paralleism (%): " << ParallelismCoverage << "\n";

        return ss.str();
      }
    };

    CoverageStats getCoverageStats(ProfilePerformanceEstimator *perf) {
      std::vector<Value *> loopInternals;
      for (auto internalNode : selectedPDG->internalNodePairs()) {
        loopInternals.push_back(internalNode.first);
      }

      std::unordered_set<DGEdge<Value> *> edgesToIgnore;

      // go through the PDG and add all removable edges to this set
      for (auto edge : selectedPDG->getEdges()) {
        if (edge->isRemovableDependence()) {
          edgesToIgnore.insert(edge);
        }
      }

      auto optimisticPDG =
        selectedPDG->createSubgraphFromValues(loopInternals, false, edgesToIgnore);

      auto optimisticSCCDAG = new SCCDAG(optimisticPDG);

      double TotalWeight = 0;
      double LargestSeqWeight = 0;
      double ParallelWeight = 0;
      double SequentialWeight = 0;

      // get total weight
      for (auto *scc : optimisticSCCDAG->getSCCs()) {
        auto curWeight = getWeight(scc, perf);
        TotalWeight += curWeight;
        if (isParallel(*scc)) {
          ParallelWeight += curWeight;
        } else {
          SequentialWeight += curWeight;
          LargestSeqWeight = std::max(LargestSeqWeight, curWeight);
        }
      }

      CoverageStats stats = {TotalWeight, LargestSeqWeight, ParallelWeight,
                             SequentialWeight};
      return stats;
    }

    void parallelizeFn() override {
      speculativelyOptimizeFn();
      LoopProfLoad *lpl = &pass.getAnalysis<LoopProfLoad>();
      auto perf = &pass.getAnalysis<ProfilePerformanceEstimator>();

      auto stats = getCoverageStats(perf);
      outs() << stats.dumpPercentage();

      int threadBudget = parser.getActionId();
      if (threadBudget == -1) {
        threadBudget = 28;
      }

      // initialize performance estimator
      auto psdswp = std::make_shared<PSDSWPCritic>(perf, threadBudget, lpl);
      auto doall = std::make_shared<DOALLCritic>(perf, threadBudget, lpl);

      auto check = [](Critic_ptr critic, string name, PDG &pdg, Loop *loop) {
        CriticRes res = critic->getCriticisms(pdg, loop);
        Criticisms &criticisms = res.criticisms;
        unsigned long expSaving = res.expSpeedup;

        if (!expSaving) {
          outs() << name << " not applicable/profitable\n";
        } else {
          outs() << name << " applicable, estimated savings: " << expSaving
                 << "\n";
        }
      };

      auto loop = getSelectedLLVMLoop();

      check(doall, "DOALL", *selectedPDG, loop);
      check(psdswp, "PSDSWPCritic", *selectedPDG, loop);
    }


    void modrefFn() override {

      int fromId = parser.getFromId();
      int toId = parser.getToId();

      unsigned numLoopAAs = loopAAs.size();

      if (fromId == -1) {
        outs() << "From InstId not set\n";
        return;
      }
      else {
        if (instIdMap->find(fromId) == instIdMap->end()) {
          outs() << "From InstId " << fromId<< " not found\n";
          return;
        }
      }

      if (toId == -1) {
        outs() << "To InstId not set\n";
        return;
      }
      else {
        if (instIdMap->find(toId) == instIdMap->end()) {
          outs() << "To InstId " << toId<< " not found\n";
          return;
        }
      }
      auto fromInst = dyn_cast<Instruction>(instIdMap->at(fromId)->getT());
      auto toInst = dyn_cast<Instruction>(instIdMap->at(toId)->getT());

      // TODO: one of the instruction can be a value
      if (!fromInst || !toInst) {
        outs() << "Instructions not found\n";
        return;
      }

      LoopAA *aa = loopAA;
      Remedies remeds;
      Loop *loop = getSelectedLLVMLoop();

      if (parser.isVerbose()) {
        // TODO: try all combination of analysis and find a setting that the result is different

        // try all loopAA, from only the first one, to all of them, the last one is always NoLoopAA
        liberty::LoopAA::ModRefResult lastRet[3] = {liberty::LoopAA::ModRef, liberty::LoopAA::ModRef, liberty::LoopAA::ModRef};
        for (auto i = 1; i <= numLoopAAs - 1; i++) {
          // set the first i loopAA to be enabled(loopAAEnabled[i] = true)
          // and the rest to be disabled (loopAAEnabled[i] = false)
          // [0~i-1]
          for (auto j = 0; j < i; j++) {
            loopAAEnabled[j] = true;
          }

          // [i~numLoopAAs-2]
          for (auto j = i; j < numLoopAAs - 1; j++) {
            loopAAEnabled[j] = false;
          }
          // the last one is always NoLoopAA and enabled
          loopAAEnabled[numLoopAAs - 1] = true;

          // configure the loop AAs prev and next based on the enabled/disabled setting
          LoopAA *prev, *cur, *next;
          prev = nullptr;
          cur = nullptr;
          next = nullptr;
          // cur is the first enabled, next is the second enabled
          for (auto j = 0; j < numLoopAAs; j++) {
            if (loopAAEnabled[j]) {
              // the first one
              if (!cur) {
                cur = loopAAs[j];
                continue;
              } else {
                next = loopAAs[j];
                cur->configure(prev, next);
                prev = cur;
                cur = next;
              }
            }
          }

          // NoLoopAA is always enabled
          assert(cur->getLoopAAName() == "NoLoopAA");
          cur->configure(prev, nullptr);

          // aa->dump();
          auto ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Same, toInst, loop, remeds);
          auto red = "\033[1;31m";
          auto green = "\033[1;32m";
          auto reset = "\033[0m";
          if (ret != lastRet[0]) {
            outs() << "Modref (same) refine from " << red << lastRet[0] << reset
                   << " to " << red << ret << reset << " with " << green
                   << loopAAs[i - 1]->getLoopAAName() << reset << "\n";
          }
          lastRet[0] = ret;

          ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Before, toInst, loop, remeds);
          if (ret != lastRet[1]) {
            outs() << "Modref (before) refine from " << red << lastRet[1] << reset
                   << " to " << red << ret << reset << " with " << green
                   << loopAAs[i - 1]->getLoopAAName() << reset << "\n";
          }
          lastRet[1] = ret;

          ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::After, toInst, loop, remeds);
          if (ret != lastRet[2]) {
            outs() << "Modref (after) refine from " << red << lastRet[2] << reset
                   << " to " << red << ret << reset << " with " << green
                   << loopAAs[i - 1]->getLoopAAName() << reset << "\n";
          }
          lastRet[2] = ret;

          // outs() << *fromInst << "->" << *toInst << ": (Same)" << ret << " with " << remeds.size() <<  " remedies\n";
          // ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Before, toInst, loop, remeds);
          // outs() << *fromInst << "->" << *toInst << ": (Before)" << ret << " with " << remeds.size() <<  " remedies\n";
          // ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::After, toInst, loop, remeds);
          // outs() << *fromInst << "->" << *toInst << ": (After)" << ret << " with " << remeds.size() <<  " remedies\n";
        }
      }
      else {
        auto ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Same, toInst, loop, remeds);
        outs() << *fromInst << "->" << *toInst << ": (Same)" << ret << " with " << remeds.size() <<  " remedies\n";
        ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::Before, toInst, loop, remeds);
        outs() << *fromInst << "->" << *toInst << ": (Before)" << ret << " with " << remeds.size() <<  " remedies\n";
        ret = aa->modref(fromInst, liberty::LoopAA::TemporalRelation::After, toInst, loop, remeds);
        outs() << *fromInst << "->" << *toInst << ": (After)" << ret << " with " << remeds.size() <<  " remedies\n";
      }
    }
};

// a simple autocompletion generator
char* completion_generator(const char* text, int state) {
  // This function is called with state=0 the first time; subsequent calls are
  // with a nonzero state. state=0 can be used to perform one-time
  // initialization for this completion session.
  static std::vector<std::string> matches;
  static size_t match_index = 0;

  if (state == 0) {
    // During initialization, compute the actual matches for 'text' and keep
    // them in a static vector.
    matches.clear();
    match_index = 0;

    // Collect a vector of matches: vocabulary words that begin with text.
    std::string textstr = std::string(text);
    for (auto word : Repl::ReplVocab) {
      if (word.size() >= textstr.size() &&
          word.compare(0, textstr.size(), textstr) == 0) {
        matches.push_back(word);
      }
    }
  }

  if (match_index >= matches.size()) {
    // We return nullptr to notify the caller no more matches are available.
    return nullptr;
  } else {
    // Return a malloc'd char* for the match. The caller frees it.
    return strdup(matches[match_index++].c_str());
  }
}

char** completer(const char* text, int start, int end) {
  // Don't do filename completion even if our generator finds no matches.
  rl_attempted_completion_over = 1;

  // Note: returning nullptr here will make readline use the default filename
  // completer.
  return rl_completion_matches(text, completion_generator);
}


bool CpfRepl::runOnModule(Module &M) {
  bool modified = false;

  auto &noelle = getAnalysis<Noelle>();

  ModuleLoops &mloops = getAnalysis<ModuleLoops>();
  const Targets &targets = getAnalysis<Targets>();

  // have a vector of all the loop aas
  LoopAA* loopAA = nullptr;
  auto aaEngines = noelle.getAliasAnalysisEngines();
  for (auto &aa : aaEngines) {
    if (aa->getName() == "SCAF") {
      loopAA = reinterpret_cast<LoopAA *>(aa->getRawPointer());
      break;
    }
  }
  if (!loopAA) {
    errs() << "No SCAF analysis engine found. Exiting.\n";
    return false;
  }


  CpfReplDriver driver(noelle, M, loopAA, targets, mloops, *this);
  driver.createLoopMap();

  rl_attempted_completion_function = completer;
  // execute command history file if specified
  string historyFileName = HistoryFileName;
  if (historyFileName != "") {
    read_history(historyFileName.c_str());
    // DISCUSSION: the last command won't get executed if using 'i <
    // history_length'
    for (int i = history_base; i <= history_length; i++) {
      char *buf = history_get(i)->line;
      string query = (const char *)(buf);
      driver.run(query);
    }
    clear_history();
  }

  // the main repl while loop
  while (true) {
    if (driver.hasTerminated()) {
      break;
    }

    char *buf = readline(driver.prompt().c_str());
    if (!buf) {
      outs() << "Quit\n";
      break;
    }
    string query = (const char *)(buf);
    if (query.size() > 0) {
      add_history(buf);
      free(buf); // free the buf readline created
    }
    driver.run(query);
  }

  return modified;
}
