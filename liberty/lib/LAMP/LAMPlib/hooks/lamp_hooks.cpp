#include "lamp_hooks.hxx"
#include "../utils/MemoryMap.hxx"
#include "../utils/LoopHierarchy.hxx"
#include "../utils/MemoryProfile.hxx"

#define LOAD LAMP_external_load
#define STORE LAMP_external_store
#define ALLOCATE LAMP_external_allocate
#define DEALLOCATE LAMP_external_deallocate

#include "../utils/profile_function_wrappers.h"

#undef ALLOCATE
#undef DEALLOCATE
#undef LOAD
#undef STORE

#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <vector>
#include <stdio.h>

// Generality-is-slow implies:
//  - do not collect stats on total number of dynamic loads, stores
//  - do not track output dependences
//  - do not track silent stores
//  - always track flow dependences
#ifndef GENERALITY_IS_SLOW
#define GENERALITY_IS_SLOW	(0)
#endif

#define MAX_NUM_LOOPS 5000
uint32_t giNumLoops;



#define LOOP_ITER_PROFILE
#define SHADOW_MEM_PROFILE
#undef LOOP_ITER_PROFILE
#undef SHADOW_MEM_PROFILE

#define INFO_GATHER 0

using namespace std;
using namespace Memory;
using namespace Loop;
using namespace Profiling;


#if defined(LOOP_ITER_PROFILE) || defined(SHADOW_MEM_PROFILE)
static unsigned long rdtsc(void) {
  unsigned long high;
  unsigned long low;
  __asm__ volatile ("rdtsc" : "=a" (low), "=d" (high));
  return (high << 32) | low;
}

// keep track of the loads, stores, allocs, and deallocs.
// Instead of using a map, just keep variables around for profiling
unsigned long cyclesBegin;
unsigned long cyclesEnd;

unsigned long numLoads;
unsigned long cyclesLoads;

unsigned long numStores;
unsigned long cyclesStores;

unsigned long numAllocs;
unsigned long cyclesAllocs;

unsigned long numDeallocs;
unsigned long cyclesDeallocs;

unsigned long numIterationsBegin;
unsigned long cyclesIterationsBegin;

unsigned long numIterationsEnd;
unsigned long cyclesIterationsEnd;

unsigned long numInvocations;
unsigned long cyclesInvocations;

unsigned long numLoopExits;
unsigned long cyclesLoopExits;


void printLAMPProfilingInfo();
#endif


/**** globals ****/
#if INFO_GATHER
long loopInvoc[MAX_NUM_LOOPS];
long loopIter[MAX_NUM_LOOPS];
long loopTotalIter[MAX_NUM_LOOPS];
long loopDeps[MAX_NUM_LOOPS];
long totalDeps[MAX_NUM_LOOPS];
bool change;

FILE *fp;

// 17 Oct 2013: Made these into pointers to avoid global contructor - NPJ
vector<int> *loopStack;
ofstream *SampleInfo;
#endif

uint64_t LAMP_param1;
uint64_t LAMP_param2;
uint64_t LAMP_param3;
uint64_t LAMP_param4;

static uint32_t external_call_id;

static uint64_t time_stamp;

typedef struct timestamp_s {
  uint32_t instr:20;
  uint64_t timestamp:44;
} __attribute__((__packed__)) timestamp_t;

static const uint64_t TIME_STAMP_MAX = ((1ULL << 44) - 1);
static const uint64_t INSTR_MAX = ((1ULL << 20) - 1);

bool operator==(const timestamp_t &t1, const timestamp_t &t2) {
  return *((uint64_t *) &t1) == *((uint64_t *) &t2);
}

static const uint64_t MAX_DEP_DIST = 2;

typedef MemoryProfiler<MAX_DEP_DIST> MemoryProfilerType;

static MemoryProfilerType *memoryProfiler;

typedef MemoryNodeMap<timestamp_t> MemoryStamp;


typedef vector<DependenceSet> DependenceSets;

typedef LoopHierarchy<DependenceSets, Loop::DEFAULT_LOOP_DEPTH, MAX_DEP_DIST> Loops;

typedef Loops::LoopInfoType LoopInfoType;

class Pages {
  private:
    MemoryPage<timestamp_t> *stampPage;

  public:
    Pages() : stampPage(NULL) {}

    Pages(MemoryStamp &stampMemory)
      : stampPage(stampMemory.get_or_create_node((void *) NULL)) {}

    void setStampPage(MemoryPage<timestamp_t> *page) {
      if (page == NULL)
        abort();
      this->stampPage = page;
    }

    MemoryPage<timestamp_t> *getStampPage() {
      return this->stampPage;
    }
};

typedef vector<Pages> PageCache;

/***** struct defs *****/
typedef struct _lamp_params_t {
  ofstream * lamp_out;
  uint64_t mem_gran;
  uint64_t mem_gran_shift;
  uint64_t mem_gran_mask;
  bool silent_stores;
  bool measure_iterations;
  bool profile_flow;
  bool profile_output;
} lamp_params_t;

typedef struct _lamp_stats_t {
  clock_t start_time;
  int64_t dyn_stores, dyn_loads, num_sync_arcs;
  int64_t calls_to_qhash;
  uint32_t nest_depth;
} lamp_stats_t;


static lamp_params_t lamp_params;
static lamp_stats_t lamp_stats;

struct nullstream: std::ostream {
  struct nullbuf: std::streambuf {
    int overflow(int c) { return traits_type::not_eof(c); }
  } m_sbuf;
  nullstream(): std::ios(&m_sbuf), std::ostream(&m_sbuf) {}
};

static const bool debug_output = true;

// 17 Oct 2013: Made these into pointers to avoid global contructor - NPJ
static MemoryStamp *memory_stamp;
static Loops *loop_hierarchy;
static PageCache *pageCache;
static nullstream *null_stream;

/*
 static ostream &debug() {
   if (debug_output) {
     return *(lamp_params.lamp_out);
   } else {
     return null_stream;
   }
 }
*/
//uint64_t iterationcount[MAX_NUM_LOOPS];  // TRM
uint64_t *iterationcount;  // Track iterations
void LAMP_print_stats(ofstream &stream) {
  stream<<setprecision(3);
  stream<<"run_time: "<<1.0*(clock()-lamp_stats.start_time)/CLOCKS_PER_SEC<<endl;
  stream<<"Num dynamic stores: "<<lamp_stats.dyn_stores<<endl;
  stream<<"Num dynamic loads: "<<lamp_stats.dyn_loads<<endl;
  stream<<"Max loop nest depth: "<<loop_hierarchy->max_depth<<endl;
#if defined(LOOP_ITER_PROFILE) || defined(SHADOW_MEM_PROFILE)
  printLAMPProfilingInfo();
  unsigned long cyclesShadow = cyclesLoads + cyclesStores + cyclesAllocs + cyclesDeallocs;
  unsigned long cyclesLoopIterProf = cyclesIterationsBegin + cyclesIterationsEnd + cyclesInvocations + cyclesLoopExits;
  unsigned long totalTime  = cyclesEnd - cyclesBegin;
  stream << "RDTSC total time: " << totalTime<< endl;
  stream << "RDTSC overhead shadow memory: " << cyclesShadow <<  " overhead ratio of " << (double)cyclesShadow/(double)totalTime << endl;
  stream << "RDTSC overhead loop iteration tracking: " << cyclesLoopIterProf << " overhead ratio of " << (double)cyclesLoopIterProf/(double)totalTime << endl;
#endif
}

/***** functions *****/

#if defined(LOOP_ITER_PROFILE) || defined(SHADOW_MEM_PROFILE)
//helper function to print
void printStat(FILE *fp, unsigned long cycles, unsigned long number, const char *name)
{
  long double avg = (long double)cycles / (long double)number;

  fprintf(fp,"Number of %s %ld\n",name,number);
  fprintf(fp,"Cycles for %s %ld\n",name,cycles);
  if(number)
    fprintf(fp, "Average per %s is %Lf\n",name, avg);
  else
    fprintf(fp, "Average per %s is not defined\n",name);

  fprintf(fp,"\n");
}


void printLAMPProfilingInfo()
{
  FILE *fp = fopen("/tmp/LAMPProfInfo.txt", "w");
  #ifdef SHADOW_MEM_PROFILE
  fprintf(fp,"Shadow Memory Profiling Overheads\n");
  unsigned long cyclesShadow = cyclesLoads + cyclesStores + cyclesAllocs + cyclesDeallocs;
  unsigned long numShadow = numLoads + numStores + numAllocs + numDeallocs;
  printStat(fp,cyclesLoads,numLoads,"loads");
  printStat(fp,cyclesStores,numStores,"stores");
  printStat(fp,cyclesAllocs,numAllocs,"allocs");
  printStat(fp,cyclesDeallocs,numDeallocs,"deallocs");
  printStat(fp,cyclesShadow,numShadow,"TOTAL SHADOW MEMORY");
  #endif

  #ifdef LOOP_ITER_PROFILE
  fprintf(fp, "\n\n\n");
  fprintf(fp,"Loop Iteration Profiling Overheads\n");
  unsigned long cyclesLoopIterProf = cyclesIterationsBegin + cyclesIterationsEnd + cyclesInvocations + cyclesLoopExits;
  unsigned long numLoopIterProf = numIterationsBegin + numIterationsEnd + numInvocations + numLoopExits;
  printStat(fp,cyclesIterationsBegin,numIterationsBegin,"iterations begin");
  printStat(fp,cyclesIterationsEnd,numIterationsEnd,"iterations end");
  printStat(fp,cyclesInvocations, numInvocations,"num invocations");
  printStat(fp,cyclesLoopExits, numLoopExits, "num loop exits");
  printStat(fp,cyclesLoopIterProf,numLoopIterProf,"TOTAL LOOP ITER PROFILING");
  #endif
  fclose(fp);


}
#endif

static void LAMP_SIGINT(int sig, siginfo_t *siginfo, void *dummy)
{
  const char msg[] = "\n\n*** Received CTRL-C. "
    "LAMP will shut down cleanly. "
    "Please wait. ***\n\n";
  int x = write(2,msg,sizeof(msg));
  if( x == -1)
    exit(x);
  exit(0);
}


void LAMP_init(uint32_t num_instrs, uint32_t num_loops, uint64_t mem_gran, uint64_t flags) {
  // 17 Oct 2013: Made these into pointers to avoid global contructor - NPJ
  memory_stamp = new MemoryStamp();
  loop_hierarchy = new Loops();
  pageCache = new PageCache();
  null_stream = new nullstream();
#if INFO_GATHER
  loopStack = new vector<int>();
  SampleInfo = new ofstream();
#endif

#if defined(LOOP_ITER_PROFILE) || defined(SHADOW_MEM_PROFILE)
  cyclesBegin = rdtsc();
  #endif
  lamp_params.lamp_out = new ofstream("result.lamp.profile");
  giNumLoops = num_loops + 1;
  iterationcount = (uint64_t *)calloc(giNumLoops,sizeof(uint64_t));


#if defined(LOOP_ITER_PROFILE) || defined(SHADOW_MEM_PROFILE)
  numDeallocs = 0;
  cyclesDeallocs= 0;

  numAllocs = 0;
  cyclesAllocs = 0;

  numStores =0;
  cyclesStores = 0;

  numLoads = 0;
  cyclesLoads = 0;

  numIterationsBegin = 0;
  cyclesIterationsBegin = 0;

  numIterationsEnd = 0;
  cyclesIterationsEnd = 0;

  numInvocations = 0;
  cyclesInvocations = 0;

  numLoopExits = 0;
  cyclesLoopExits = 0;



#endif


#if INFO_GATHER
  fp = fopen("deps.info","w");

  loopStack->push_back(0);
  SampleInfo->open("sample.info");

  *SampleInfo << "Loop ID | Invoc # | Iter # | Deps So Far\n";

  for(int i=0; i<MAX_NUM_LOOPS; ++i)
  {
    loopInvoc[i] = 0;
    loopIter[i] = 0;
    loopDeps[i] = 0;
    totalDeps[i] = 0;
    loopTotalIter[i] = 0;
  }
#endif

  if (sizeof(timestamp_t) != sizeof(uint64_t)) {
    fprintf(stderr, "sizeof(timestamp_t) != sizeof(uint64_t) (%lu != %lu)\n", sizeof(timestamp_t), sizeof(uint64_t));
    abort();
  }

  if (num_instrs >= INSTR_MAX) {
    cerr<<"Number of instructions too high"<<num_instrs<<" >= "<<INSTR_MAX<<endl;
    abort();
  }

  lamp_params.mem_gran = mem_gran;
  lamp_params.mem_gran_mask = ~0x0;
  lamp_params.mem_gran_shift = 0;
  for (uint32_t i = 0x1; i < lamp_params.mem_gran; i <<= 1) {
    lamp_params.mem_gran_mask ^= i;
    lamp_params.mem_gran_shift+=1;
  }

  lamp_params.silent_stores = false;
  if (((flags & 0x1) != 0) || (getenv("LAMP_PROFILE_SILENT_STORES") != NULL)) {
    lamp_params.silent_stores = true;
  }

  lamp_params.measure_iterations = true;
/*  if (((flags & 0x2) != 0) || (strcmp(getenv("LAMP_PROFILE_MEASURE_ITERATIONS"),"OFF"))) {
    lamp_params.measure_iterations = false;
  } */

  lamp_params.profile_flow = true;

  /*
  lamp_params.profile_output = false;
  if (((flags & 0x4) != 0) || (getenv("LAMP_PROFILE_PROFILE_OUTPUT") != NULL)) {
    lamp_params.profile_output = true;
    // lamp_params.profile_flow = false;
  }
  */
  lamp_params.profile_output = false;

  lamp_stats.start_time = clock();
  lamp_stats.dyn_stores= 0;
  lamp_stats.dyn_loads= 0;
  lamp_stats.nest_depth = 0;
  lamp_stats.num_sync_arcs = 0;

  external_call_id = 0;

  memoryProfiler = new MemoryProfilerType(num_instrs);

  // timestamp 0 is the special first "iteration" of main
  loop_hierarchy->loopIteration(0, iterationcount);

  LoopInfoType &loopInfo = loop_hierarchy->getCurrentLoop();
  DependenceSets dependenceSets(MemoryProfilerType::MAX_TRACKED_DISTANCE);
  loopInfo.setItem(dependenceSets);

  time_stamp = 1;

  pageCache = new PageCache(num_instrs);
  Pages pages(*memory_stamp);
  for (uint32_t i = 0; i < num_instrs; i++) {
    (*pageCache)[i] = pages;
  }

  atexit(LAMP_finish);

  // This process may take a really long time
  // to complete.  We set up a CTRL-C
  // handler which will exit using
  // exit(), ensuring that LAMP_finish
  // is called.
  struct sigaction original, replacement;
  replacement.sa_flags = SA_SIGINFO;
  sigemptyset( &replacement.sa_mask );
  replacement.sa_sigaction = &LAMP_SIGINT;
  sigaction( SIGINT, &replacement, &original );
}

void LAMP_init_st() {
  LAMP_init(LAMP_param1, LAMP_param2, LAMP_param3, LAMP_param4);
}

void LAMP_finish() {
#if defined(LOOP_ITER_PROFILE) || defined(SHADOW_MEM_PROFILE)
  cyclesEnd = rdtsc();
  #endif
#if INFO_GATHER
  SampleInfo->close();
  fclose(fp);
#endif

  // Print out Loop Iteration Counts
  for (unsigned i= 0; i < giNumLoops; i++)
  {
    *(lamp_params.lamp_out)<<i<<" "<<iterationcount[i] <<"\n";
  }

  // Print out all dependence information
  *(lamp_params.lamp_out)<<*memoryProfiler;

  // Print final stats
  LAMP_print_stats(*(lamp_params.lamp_out));
}

static LoopInfoType &fillInDependence(const timestamp_t value, Dependence &dep) {
  const uint64_t store_time_stamp = value.timestamp;
  dep.store = value.instr;
  LoopInfoType &loop = loop_hierarchy->findLoop(store_time_stamp);
  dep.loop = loop.loop_id;
  dep.dist = loop_hierarchy->calculateDistance(loop, store_time_stamp);

  return loop;
}

template <class T>
static void memory_profile(const uint32_t destId, const uint64_t addr) {
  Pages &pages = pageCache->at(destId);
  const timestamp_t *last_store = NULL;

  //debug()<<"ML "<<destId<<" "<<(void *) addr<<" "<<sizeof(T)<<" :: ";

  uint8_t i = 0;
  do {
    /* Get the last store */
    const timestamp_t *store_value = pages.getStampPage()->getItem((void *) (addr + i));

    /* There has been no store so far */
    if (store_value == NULL) {
      //debug()<<"U ";
      continue;
    }

    /* if it is still the same store as previous loop iteration, continue */
    if ((last_store != NULL) && (*store_value == *last_store)) {
      //debug()<<"E ";
      continue;
    }

    //debug()<<"N ";

    last_store = store_value;

    Dependence dep(destId);
    LoopInfoType &loopInfo = fillInDependence(*store_value, dep);
    dep.dist = MemoryProfilerType::trackedDistance(dep.dist);
    MemoryProfile &profile = memoryProfiler->increment(dep);


    if (lamp_params.measure_iterations)
    {
      DependenceSets &dependenceSets = loopInfo.getItem();
      pair<DependenceSet::iterator, bool> result = dependenceSets[dep.dist].insert(dep);
      if (result.second)
      {
        profile.incrementLoop();

#if INFO_GATHER
        uint32_t mloop = dep.loop;
        uint64_t cnt = profile.getLoopCount();
    /*
        fwrite(&mloop,sizeof(int),1,fp);
        fwrite(&loopInvoc[mloop],sizeof(long),1,fp);
        fwrite(&loopIter[mloop],sizeof(long),1,fp);
        fwrite(&dep.load,sizeof(int),1,fp);
        fwrite(&dep.store,sizeof(int),1,fp);
        fwrite(&dep.dist,sizeof(int),1,fp);
        fwrite(&cnt,sizeof(long),1,fp);

    */
        if(loopIter[mloop] < 10000)
        {
          fprintf(fp, "%d %ld %ld %ld %d %d %d %ld\n",
              mloop, loopInvoc[mloop], loopIter[mloop],
              loopTotalIter[mloop],
              dep.load, dep.store, dep.dist, cnt);
        }

        totalDeps[dep.loop] += 1;
        if(profile.getLoopCount() == 1)
        {
          change = true;
          loopDeps[dep.loop] += 1;
        }
#endif



      }
    }
  } while ((++i) < sizeof(T));

  //debug()<<endl;
}

template <class T>
static void LAMP_aligned_load(const uint32_t instr, const uint64_t addr) {
  Pages &pages = pageCache->at(instr);

  if (!pages.getStampPage()->inPage((void *) addr)) {
    pages.setStampPage(memory_stamp->get_or_create_node((void *) addr));
  }

  if ( GENERALITY_IS_SLOW || lamp_params.profile_flow) {
    memory_profile<T>(instr, addr);
  }
}

template <class T>
static void LAMP_unaligned_load(const uint32_t instr, const uint64_t addr) {
//zouf todo check with SRB. Do these need to be separately handled? I think its
//OK they're getting lumped in with aligned loads, but maybe not
  for (uint8_t i = 0; i < sizeof(T); i++) {
    LAMP_aligned_load<uint8_t>(instr, addr + i);
  }
}

template <class T>
void LAMP_load(const uint32_t instr, const uint64_t addr) {
#ifdef SHADOW_MEM_PROFILE
  unsigned long before = rdtsc();
#endif

  if( !GENERALITY_IS_SLOW )
  {
    lamp_stats.dyn_loads++;
  }

  if (!Memory::is_aligned<T>(addr)) {
    LAMP_unaligned_load<T>(instr, addr);
  } else {
    LAMP_aligned_load<T>(instr, addr);
  }
#ifdef SHADOW_MEM_PROFILE
  unsigned long after = rdtsc();
  numLoads++;
  cyclesLoads += after - before;
#endif

}

void LAMP_load1(const uint32_t instr, const uint64_t addr) {
  LAMP_load<uint8_t>(instr, addr);
}

void LAMP_load2(const uint32_t instr, const uint64_t addr) {
  LAMP_load<uint16_t>(instr, addr);
}

void LAMP_load4(const uint32_t instr, const uint64_t addr) {
  LAMP_load<uint32_t>(instr, addr);
}

void LAMP_load8(const uint32_t instr, const uint64_t addr) {
  LAMP_load<uint64_t>(instr, addr);
}

void LAMP_llvm_memcpy_p0i8_p0i8_i32(const uint32_t instr, const uint8_t * dstAddr, const uint8_t * srcAddr, const uint32_t sizeBytes)
{
  LAMP_llvm_memmove_p0i8_p0i8_i64(instr, dstAddr, srcAddr, (uint64_t)sizeBytes);
}

void LAMP_llvm_memcpy_p0i8_p0i8_i64(const uint32_t instr, const uint8_t * dstAddr, const uint8_t * srcAddr, const uint64_t sizeBytes)
{
  LAMP_llvm_memmove_p0i8_p0i8_i64(instr, dstAddr, srcAddr, sizeBytes);
}

void LAMP_llvm_memmove_p0i8_p0i8_i32(const uint32_t instr, const uint8_t * dstAddr, const uint8_t * srcAddr, const uint32_t sizeBytes)
{
  LAMP_llvm_memmove_p0i8_p0i8_i64(instr, dstAddr, srcAddr, (uint64_t)sizeBytes);
}

void LAMP_llvm_memmove_p0i8_p0i8_i64(const uint32_t instr, const uint8_t * dstAddr, const uint8_t * srcAddr, const uint64_t sizeBytes)
{
  uint64_t i;

  if (srcAddr <= dstAddr && srcAddr + sizeBytes > dstAddr)
  {
    // overlap, copy backwards
    for(i=0; i<sizeBytes; ++i)
    {
      const uint64_t k = sizeBytes - 1 - i;
      LAMP_load1(instr, (uint64_t) &srcAddr[k]);
      LAMP_store1(instr, (uint64_t) &dstAddr[k], srcAddr[k]);
    }
  }
  else
  {
    // copy forward.
    for(i=0; i<sizeBytes; ++i)
    {
      LAMP_load1(instr, (uint64_t) &srcAddr[i]);
      LAMP_store1(instr, (uint64_t) &dstAddr[i], srcAddr[i] );
    }
  }
}

void LAMP_llvm_memset_p0i8_i32(const uint32_t instr, const uint8_t * dstAddr, const uint8_t value, const uint32_t sizeBytes)
{
  LAMP_llvm_memset_p0i8_i64(instr, dstAddr, value, (uint64_t)sizeBytes);
}

void LAMP_llvm_memset_p0i8_i64(const uint32_t instr, const uint8_t * dstAddr, const uint8_t value, const uint32_t sizeBytes)
{
  uint64_t i;

  for(i=0; i<sizeBytes; ++i)
    LAMP_store1(instr, (uint64_t) &dstAddr[i], value);
}



void LAMP_external_load(const void * src, const uint64_t size) {
  const uint64_t cptr = (uint64_t) (intptr_t) src;
  for (uint64_t i = 0; i < size; i++) {
    const uint64_t addr = cptr + i;
    // MJB: It is important that src not be dereferenced, as it may not longer be valid
    // (ex. if realloc freed the src pointer)

    LAMP_load1(external_call_id, addr);
  }
}

static timestamp_t form_timestamp(uint32_t instr, uint64_t timestamp) {
  if (timestamp > TIME_STAMP_MAX) {
    fprintf(stderr, "TIME STAMP too large\n");
    abort();
  }

  if (instr > INSTR_MAX) {
    fprintf(stderr, "INSTR too large\n");
    abort();
  }

  timestamp_t ts;
  ts.timestamp = timestamp;
  ts.instr = instr;
  return ts;
}

template<class T>
static void LAMP_aligned_store(uint32_t instrId, uint64_t addr) {
  #ifdef SHADOW_MEM_PROFILE
    //zouf todo profile shadow mem store.
    // todo categorize different types of acccesses?
    // todo categorize the components of shadow mem?

  #endif

  Pages &pages = pageCache->at(instrId);
  if (!pages.getStampPage()->inPage((void *) addr)) {
    pages.setStampPage(memory_stamp->get_or_create_node((void *) addr));
  }

	if( !GENERALITY_IS_SLOW )
	{
		if (lamp_params.profile_output) {
			memory_profile<T>(instrId, addr);
		}
	}

  const timestamp_t val = form_timestamp(instrId, time_stamp);
  //debug()<<"S "<<instrId<<" "<<(void *) addr<<" "<<sizeof(T)<<" "<<val<<" ";

  for (uint8_t i = 0; i < sizeof(T); i++) {
    //debug()<<"S ";
    pages.getStampPage()->setItem((void *) (addr + i), val);
  }

  //debug()<<endl;
}

template<class T>
static void LAMP_unaligned_store(uint32_t instrId, uint64_t addr) {
  for (uint8_t i = 0; i < sizeof(T); i++) {
    LAMP_aligned_store<uint8_t>(instrId, addr + i);
  }
}

template<class T>
  static bool is_silent_store(const uint32_t instr, const uint64_t addr, const uint64_t value) {
    if (!lamp_params.silent_stores)
      return false;

    return (*((const T *) addr) == ((T) value));
  }

template<class T>
void LAMP_store(uint32_t instrID, uint64_t addr, uint64_t value) {
#ifdef SHADOW_MEM_PROFILE
  unsigned long before = rdtsc();
#endif
	if( !GENERALITY_IS_SLOW)
	{
    lamp_stats.dyn_stores++;

		if (is_silent_store<T>(instrID, addr, value))
			return;
	}

  if (!Memory::is_aligned<T>(addr)) {
    LAMP_unaligned_store<T>(instrID, addr);
  } else {
    LAMP_aligned_store<T>(instrID, addr);
  }
#ifdef SHADOW_MEM_PROFILE
  unsigned long after = rdtsc();
  numStores ++;
  cyclesStores += after - before;
#endif

}

void LAMP_store1(uint32_t instr, uint64_t addr, uint64_t value) {
  LAMP_store<uint8_t>(instr, addr, value);
}

void LAMP_store2(uint32_t instr, uint64_t addr, uint64_t value) {
  LAMP_store<uint16_t>(instr, addr, value);
}

void LAMP_store4(uint32_t instr, uint64_t addr, uint64_t value) {
  LAMP_store<uint32_t>(instr, addr, value);
}

void LAMP_store8(uint32_t instr, uint64_t addr, uint64_t value) {
  LAMP_store<uint64_t>(instr, addr, value);
}

void LAMP_external_store(const void * dest, const uint64_t size) {
#ifdef SHADOW_MEM_PROFILE
//zouf xxx why is this diff from LAMP_store?
  unsigned long before = rdtsc();
#endif
  const uint64_t cptr = (uint64_t) (intptr_t) dest;
  for (uint64_t i = 0; i < size; i++) {
    const uint64_t addr = cptr + i;

    LAMP_aligned_store<uint8_t>(external_call_id, addr);
  }
#ifdef SHADOW_MEM_PROFILE
  unsigned long after = rdtsc();
  numStores ++;
  cyclesStores += after - before;
#endif


}

static void invalidate_region(const void *memory, size_t size) {
  for (uint64_t i = 0; i < size; i++) {
    memory_stamp->set_invalid<uint8_t>((uint8_t*)memory+i);
  }
}

// sot: remove Heejin's changes for malloc/free
// Handle malloc/free
// Malloc'ed address to size map, to let matching free know its size
// This is necessary to call LAMP_deallocate
//std::map<const void*, size_t> allocAddrToSize;

void LAMP_allocate(uint32_t lampId, const void *memory, size_t size) {

#ifdef SHADOW_MEM_PROFILE
  unsigned long before = rdtsc();
#endif
  //allocAddrToSize[memory] = size;
  invalidate_region(memory, size);
#ifdef SHADOW_MEM_PROFILE
  unsigned long after = rdtsc();
  numAllocs++;
  cyclesAllocs+= after - before;
#endif
}

void LAMP_deallocate(uint32_t lampId, const void *memory, size_t size) {
//void LAMP_deallocate(uint32_t lampId, const void *memory) {

#ifdef SHADOW_MEM_PROFILE
  unsigned long before = rdtsc();
#endif
  //size_t size = allocAddrToSize.at(memory);

  for (uint64_t i = 0; i < size; i++) {
    LAMP_aligned_store<uint8_t>(lampId, ((uint64_t)memory+i));
  }
  invalidate_region(memory, size);
#ifdef SHADOW_MEM_PROFILE
  unsigned long after = rdtsc();
  numAllocs++;
  cyclesAllocs+= after - before;
#endif

}

void LAMP_external_allocate(const void *memory, size_t size) {
  LAMP_allocate(external_call_id, memory, size);
}

void LAMP_external_deallocate(const void *memory, size_t size) {
  LAMP_deallocate(external_call_id, memory, size);
  //LAMP_deallocate(external_call_id, memory);
}

void LAMP_allocate_st(void) {
  LAMP_allocate((uint32_t) LAMP_param1, (void *) LAMP_param2, (size_t) LAMP_param3);
}


static void initializeSets() {
  //zouf todo where is this counted
  if (!lamp_params.measure_iterations)
    return;

  LoopInfoType &loopInfo = loop_hierarchy->getCurrentLoop();
  DependenceSets &dependenceSets = loopInfo.getItem();

  if (dependenceSets.size() != MemoryProfilerType::MAX_TRACKED_DISTANCE) {
    for (uint32_t i = dependenceSets.size(); i < MemoryProfilerType::MAX_TRACKED_DISTANCE; i++) {
      DependenceSet seta;
      dependenceSets.push_back(seta);
    }
  }

  DependenceSets::iterator viter = dependenceSets.begin();
  for (; viter != dependenceSets.end(); viter++) {
    viter->clear();
  }
}

void LAMP_loop_iteration_begin(void) {
#ifdef LOOP_ITER_PROFILE
  unsigned long before = rdtsc();
#endif

#if INFO_GATHER
  loopIter[loopStack->back()] += 1;
  loopTotalIter[loopStack->back()] += 1;
  change = false;
#endif

  time_stamp++;
  loop_hierarchy->loopIteration(time_stamp, iterationcount);
  initializeSets();
#ifdef LOOP_ITER_PROFILE
  unsigned long after = rdtsc();
  numIterationsBegin++;
  cyclesIterationsBegin+= after - before;
#endif

}

void LAMP_loop_iteration_end(void) {
#ifdef LOOP_ITER_PROFILE
  unsigned long before = rdtsc();
#endif

#if INFO_GATHER
  // Print out the number of deps found for this loop thus far
  // loop ID | Invocation # | Iter # | # deps found so far
  if(change) // Might need to add something here to print ever x iterations
    // to catch when the totalDeps change but no new dep is found
  {
    int loop = loopStack->back();
    *SampleInfo << loop << " " << right
      << setw(10) << loopInvoc[loop] << " "
      << setw(10) << loopIter[loop] << " "
      << setw(10) << loopDeps[loop] << " "
      << setw(10) << loopTotalIter[loop] << " "
      << setw(10) << totalDeps[loop] << "\n";
  }
#endif
#ifdef LOOP_ITER_PROFILE
  unsigned long after = rdtsc();
  numIterationsEnd++;
  cyclesIterationsEnd+= after - before;
#endif

  return;
}

void LAMP_loop_iteration_begin_st(void) {
  LAMP_loop_iteration_begin();
}

void LAMP_loop_iteration_end_st(void) {
  LAMP_loop_iteration_end();
}

void LAMP_loop_exit2(const uint16_t loop) {
#ifdef LOOP_ITER_PROFILE
  unsigned long before = rdtsc();
#endif


#if INFO_GATHER
  loopIter[loopStack->back()] = 0;
  loopStack->pop_back();
#endif

  loop_hierarchy->exitLoop(loop);
#ifdef LOOP_ITER_PROFILE
  unsigned long after = rdtsc();
  numLoopExits++;
  cyclesLoopExits += after - before;
#endif



}

void LAMP_loop_exit(const uint16_t loop) {
#ifdef LOOP_ITER_PROFILE
  unsigned long before = rdtsc();
#endif



#if INFO_GATHER
  loopIter[loopStack->back()] = 0;
  loopStack->pop_back();
#endif

   loop_hierarchy->exitLoop(loop);
#ifdef LOOP_ITER_PROFILE
  unsigned long after = rdtsc();
  numLoopExits++;
  cyclesLoopExits += after - before;
#endif

}

void LAMP_loop_exit_st(void) {
  LAMP_loop_exit(0);
}

void LAMP_loop_invocation(const uint16_t loop) {
  #ifdef LOOP_ITER_PROFILE
    unsigned long before = rdtsc();
  #endif




#if INFO_GATHER
  loopStack->push_back(loop);
  loopInvoc[loop] += 1;
#endif


 loop_hierarchy->enterLoop(loop, time_stamp);
  initializeSets();

#ifdef LOOP_ITER_PROFILE
    unsigned long after = rdtsc();
    numInvocations ++;
    cyclesInvocations += after - before;
  #endif






}

void LAMP_loop_invocation_st(void) {
 #ifdef LOOP_ITER_PROFILE
    //zouf profile loop
  #endif

  uint16_t loop_id = (uint16_t) LAMP_param1;
  LAMP_loop_invocation(loop_id);
}

void LAMP_register(uint32_t id) {
  //zouf todo where is this overhead counted
  external_call_id = id;
  LAMP_param1 = id;
}

void LAMP_register_st(void) {
  LAMP_register((uint32_t) LAMP_param1);
}
