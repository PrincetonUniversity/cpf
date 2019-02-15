#ifndef LLVM_LIBERTY_HEADER_PHI_PROF_LOAD_H
#define LLVM_LIBERTY_HEADER_PHI_PROF_LOAD_H

#include "llvm/Pass.h"

#include <stdint.h>
#include <set>

namespace liberty
{

using namespace llvm;

class HeaderPhiLoadProfile: public ModulePass
{
public:
  static char ID;
  HeaderPhiLoadProfile();
  ~HeaderPhiLoadProfile();

  void getAnalysisUsage(AnalysisUsage& au) const;

  bool runOnModule(Module& m);

  bool isPredictable(uint64_t id);
private:

  std::set<uint64_t> predictable;
};

}

#endif
