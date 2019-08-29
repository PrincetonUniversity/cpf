#ifndef LLVM_LIBERTY_FAST_DAG_SCC_EXP_H
#define LLVM_LIBERTY_FAST_DAG_SCC_EXP_H

#include "llvm/Support/CommandLine.h"

namespace liberty
{
namespace SpecPriv
{
namespace FastDagSccExperiment
{
using namespace llvm;

extern cl::opt<std::string> BenchName;
extern cl::opt<std::string> OptLevel;
extern cl::opt<std::string> AADesc;


uint64_t rdtsc(void);

extern cl::opt<unsigned> Exp_Timeout;
extern cl::opt<bool> UseOracle;
extern cl::opt<bool> HideContext;
extern cl::opt<bool> UseCntrSpec;
extern cl::opt<bool> UseValuePred;
extern cl::opt<bool> UseTXIO;
extern cl::opt<bool> UseCommLibs;
extern cl::opt<bool> UseCommGuess;
extern cl::opt<bool> UsePureFun;
extern cl::opt<bool> UseRedux;
extern cl::opt<bool> UseRO;
extern cl::opt<bool> UseLocal;
extern cl::opt<bool> UsePointsTo;
extern cl::opt<bool> UseOracle;
extern cl::opt<bool> UsePtrResidue;
extern cl::opt<bool> UseCAF;

uint64_t countCyclesPerSecond();

}
}
}
#endif

