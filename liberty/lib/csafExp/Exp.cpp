#include <sys/time.h>
#include <unistd.h>
#include "llvm/Support/raw_ostream.h"

#include "Exp.h"

namespace liberty
{
namespace SpecPriv
{
namespace FastDagSccExperiment
{
using namespace llvm;

cl::opt<std::string> BenchName(
  "bench-name", cl::init("benchmark"), cl::Hidden,
  cl::desc("Name of benchmark"));
cl::opt<std::string> OptLevel(
  "opt-level", cl::init("opti"), cl::Hidden,
  cl::desc("Optimization level"));
cl::opt<std::string> AADesc(
  "aa-desc", cl::init("aa"), cl::Hidden,
  cl::desc("Description of AA"));

cl::opt<bool> UseOracle(
  "use-oracle", cl::init(false), cl::Hidden,
  cl::desc("Use Oracle AA"));
cl::opt<bool> HideContext(
  "hide-context", cl::init(false), cl::Hidden,
  cl::desc("Hide context from AA stack to simulate a dumber interface"));
cl::opt<bool> UseCntrSpec(
  "use-cntr-spec", cl::init(false), cl::Hidden,
  cl::desc("Use Control Speculation Remed"));
cl::opt<bool> UseValuePred(
  "use-value-pred", cl::init(false), cl::Hidden,
  cl::desc("Use Value Prediction Remed"));
cl::opt<bool> UseTXIO(
  "use-txio", cl::init(false), cl::Hidden,
  cl::desc("Use TXIO Remed"));
cl::opt<bool> UseCommLibs(
  "use-comm-libs", cl::init(false), cl::Hidden,
  cl::desc("Use Commutative Libs Remed"));
cl::opt<bool> UseCommGuess(
  "use-comm-guess", cl::init(false), cl::Hidden,
  cl::desc("Use Commutative Guess Remed"));
cl::opt<bool> UsePureFun(
  "use-pure-fun", cl::init(false), cl::Hidden,
  cl::desc("Use Pure Fun Remed"));
cl::opt<bool> UseRedux(
  "use-redux", cl::init(false), cl::Hidden,
  cl::desc("Use scalar reductions"));

uint64_t rdtsc(void)
{
  uint32_t a, d;
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((uint64_t)a) | (((uint64_t)d) << 32);
}

// One hour
cl::opt<unsigned> Exp_Timeout(
  "experiment-timeout", cl::init(30*60), cl::Hidden,
    cl::desc("Watchdog timeout per loop"));


static uint64_t cycles_per_second = 0;
uint64_t countCyclesPerSecond()
{
  if( cycles_per_second > 0 )
    return cycles_per_second;

  errs() << "Measuring cycles/second...\n";

  struct timeval tv_start, tv_stop;
  gettimeofday(&tv_start,0);
  const uint64_t start = rdtsc();

  sleep(10);

  const uint64_t stop = rdtsc();
  gettimeofday(&tv_stop,0);

  double actual_duration =
    (tv_stop.tv_sec  - tv_start.tv_sec)
  + (tv_stop.tv_usec - tv_start.tv_usec)*1.0e-9;

  cycles_per_second = (stop-start)/(actual_duration);
  errs() << "==> There are " << cycles_per_second << " cycles/second\n";
  return cycles_per_second;
}

}
}
}

