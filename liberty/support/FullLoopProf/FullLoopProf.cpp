#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>

#include <cstring>

#include <vector>
#include <iostream>
#include <fstream>

#define Period 1000

#define DEBUG
#undef DEBUG

#ifdef __cplusplus
extern "C" {
#endif

  void loopProf_invocation(int loop);
  void loop_exit(int loop);
  void loopProfFinish(void);
  void loopProfInit(int numLoops);
  int dumpLoopStack(void);

#ifdef __cplusplus
}
#endif

using namespace std;

bool withinRuntime;
unsigned long *loopCounts; // Track number of times we were in each loop
int gnumLoops;

vector<int> *loopStack; // Track what the current loop stack looks like
vector<int> *loopAccounted; // Track if this loop has been counted yet

/* Function to dump the current loop stack, really only useful for debugging
 */
int dumpLoopStack(void)
{
  vector<int>::iterator it;

  for(it = loopStack->begin(); it < loopStack->end(); ++it)
  {
    cout << *it << "\n";
  }

  return 1;
}

/* Begin an invocation of a function/loop/callsite
 * TODO: Split this into different functions so that debugging the bitcode
 * files becomes easier
 */
void loopProf_invocation(int loop)
{
  if( withinRuntime )
    return;
  withinRuntime = true;

#ifdef DEBUG
  printf("Entering function/calsite/loop %d\n", loop);
#endif
  loopStack->push_back(loop);

  withinRuntime = false;
}

/* End an invocation of a function/loop/callsite
 * TODO: Split this into different functions so that debugging the bitcode
 * files becomes easier
 */
void loop_exit(int loop)
{
  if( withinRuntime )
    return;
  withinRuntime = true;

#ifdef DEBUG
  printf("Exiting function/calsite/loop %d\n", loop);
#endif

  for(;;)
  {
    if( loopStack->empty() )
    {
      std::cerr << "Warning: exiting from empty stack\n";
      withinRuntime = false;
      return;
    }

    else if( loop == loopStack->back() )
      break;

    if( loopStack->back() == 0 && loop != 0 )
    {
      std::cerr << "Warning: don't pop past 'top' context\n";
      withinRuntime = false;
      return;
    }

    std::cerr << "Warning: expected " << loopStack->back() << " but got " << loop << "\n";
    // Can't assert b/c benchmarks like 471.omnetpp which enjoy longjmp
    loopStack->pop_back();
  }

  loopStack->pop_back();
  withinRuntime = false;
}

/* Finish up the profiler, print profile
 */
void loopProfFinish(void)
{
  ofstream f;
  f.open("loopProf.out");

  for(int i = 0; i < gnumLoops; ++i)
  {
    f << i << " " << loopCounts[i] << endl;
  }

  f.close();


  free(loopCounts);
}


// signal handler

void timer_handler(int sig, siginfo_t *siginfo, void *dummy)
{
  const bool oldWithin = withinRuntime;
  withinRuntime = true;

  // Block signals during the stack trace.
  sigset_t blockAll, oldSigs;
  sigfillset(&blockAll);
  sigprocmask(SIG_BLOCK, &blockAll, &oldSigs);

  // Caution: evil
  memset( &(*loopAccounted)[0], 0, loopAccounted->size() * sizeof(int));
  /*
  for( vector<int>::iterator it=loopAccounted->begin(), end=loopAccounted->end();
      it!=end;++it)
  {
    loopAccounted[i] = false;
    *it=false;
  }
  */


  for(unsigned i=0, N=loopStack->size(); i<N; ++i)
  {
    int loop = (*loopStack)[i];

    if(! (*loopAccounted) [ loop ])
    {
      loopCounts[ loop ]++;
      (*loopAccounted) [ loop ] = true;
    }
  }
  sigprocmask(SIG_SETMASK, &oldSigs, 0);
  withinRuntime = oldWithin;
}


//vector<int> *loopStack; // Track what the current loop stack looks like
//vector<int> *loopAccounted; // Track if this loop has been counted yet

void loopProfInit(int numLoops)
{
  withinRuntime = true;
  loopStack = new vector<int>();
  loopAccounted = new vector<int>();

  gnumLoops = numLoops + 1;
  loopCounts = (unsigned long*)calloc(gnumLoops, sizeof(unsigned long));
  loopAccounted->resize(gnumLoops);

  // Set up a signal handler for SIG VTALRM
  struct sigaction replacement;
  replacement.sa_flags = SA_SIGINFO;
  sigemptyset( &replacement.sa_mask );
  replacement.sa_sigaction = & timer_handler;
  sigaction(SIGVTALRM, &replacement, 0);

  // Set the itimer to give us interrupts
  // at a regular interval.
  struct itimerval interval;
  interval.it_interval.tv_sec = 0;
  interval.it_interval.tv_usec = Period;
  interval.it_value.tv_sec = 0;
  interval.it_value.tv_usec = Period;
  setitimer(ITIMER_VIRTUAL, &interval, 0);

  atexit(loopProfFinish);
  withinRuntime = false;

  loopProf_invocation(0);
}

