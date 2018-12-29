#ifndef LLVM_LIBERTY_LOOP_PROF_LOAD_H
#define LLVM_LIBERTY_LOOP_PROF_LOAD_H

//===----------------------------------------------------------------------===//
//
// Info...
//
//===----------------------------------------------------------------------===//

#include <map>
#include <string>
#include <fstream>

#include "llvm/Pass.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Function.h"

#define PROF_FILE "loopProf.out"

namespace llvm {
  class Loop;
}

namespace liberty {
using namespace llvm;

  class LoopProfLoad : public ModulePass {
    public:
      typedef std::map<std::string, unsigned long> Loop2Times;
      typedef Loop2Times::const_iterator iterator;

      static char ID;
      LoopProfLoad() : ModulePass(ID), valid(false) {}

      virtual bool runOnModule (Module &M);
      bool runOnLoop(Loop *Lp);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const;
      void profile_dump(void);


      unsigned long getTotTime(void) const { return totTime; }
      void setTotTime(unsigned long t) { totTime = t; }

      unsigned long getFunctionTime(const Function *f) const
      {
        return getLoopTime( f->getName() );
      }

      void setFunctionTime(const Function *f, unsigned long t)
      {
        setLoopTime(f->getName(), t);
      }

      unsigned long getLoopTime(const BasicBlock *loop_header) const
      {
        std::string name = getLoopName(loop_header);
        return getLoopTime(name);
      }

      void setLoopTime(const BasicBlock *loop_header, unsigned long t)
      {
        std::string name = getLoopName(loop_header);
        setLoopTime(name,t);
      }

      unsigned long getLoopTime(const Loop *l) const
      {
        std::string name = getLoopName(l);
        return getLoopTime(name);
      }

      unsigned long getLoopTime(const std::string &loopName) const
      {
        Loop2Times::const_iterator i = loopTimesMap.find(loopName);
        if( i == loopTimesMap.end() )
          return 0;
        else
          return i->second;
      }

      void setLoopTime(const std::string &loopName, unsigned long t)
      {
        Loop2Times::iterator i = loopTimesMap.find(loopName);
        assert( i != loopTimesMap.end()
        && "Loop name is not already in the map");
        i->second = t;
      }

      unsigned long getCallSiteTime(const Instruction *cs) const
      {
        std::string name = getCallSiteName(cs);
        return getLoopTime(name);
      }

      void setCallSiteTime(const Instruction *cs, unsigned long t)
      {
        std::string name = getCallSiteName(cs);
        setLoopTime(name,t);
      }

      double getLoopFraction(const Loop *l) const
      {
        std::string name = getLoopName(l);
        return getLoopFraction(name);
      }

      double getLoopFraction(const std::string &loopName) const
      {
        return getLoopTime(loopName)/(double)getTotTime();
      }

      bool isValid() const { return valid; }
      iterator begin() const { return loopTimesMap.begin(); }
      iterator end() const { return loopTimesMap.end(); }


      void addLoop(const BasicBlock *header)
      {
        std::string name = getLoopName(header);
        ++numLoops;
        numToLoopName[ numLoops ] = name;
        loopTimesMap[ name ] = 0;
      }

      void addFunction(const Function *fcn)
      {
        std::string name = fcn->getName();
        ++numLoops;
        numToLoopName[ numLoops ] = name;
        loopTimesMap[ name ] = 0;
      }

      void addCallSite(const Instruction *cs)
      {
        std::string name = getCallSiteName(cs);
        ++numLoops;
        numToLoopName[ numLoops ] = name;
        loopTimesMap[ name ] = 0;
      }

    private:

      std::string getLoopName(const BasicBlock *loop_header) const;
      std::string getLoopName(const Loop *loop) const;
      std::string getCallSiteName(const Instruction *inst) const;

      Loop2Times loopTimesMap;
      std::map<int, std::string> numToLoopName;
      std::ifstream inFile;

      unsigned long totTime;
      int numLoops;
      /** Is this profile valid? */
      bool valid;

  };
}

#endif // LLVM_LIBERTY_LAMP_LOAD_PROFILE_H
