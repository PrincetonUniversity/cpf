#if HAS_SMTX
#define __STDC_FORMAT_MACROS

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


extern "C" {
#include <time.h>
#include <pthread.h>
#include "sw_queue_astream.h"
}

#define MAX_NUM_LOOPS 1048

using namespace std;
using namespace Memory;
using namespace Loop;
using namespace Profiling;


/**** globals ****/
pthread_t master_thread; // tid of the master thread
SW_Queue q; // the queue

extern volatile uint64_t *addrArray;
extern uint64_t pos;

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

static MemoryStamp memory_stamp;

typedef vector<DependenceSet> DependenceSets;

typedef LoopHierarchy<DependenceSets, Loop::DEFAULT_LOOP_DEPTH, MAX_DEP_DIST> Loops;

static Loops loop_hierarchy;

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

static PageCache pageCache;

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
static nullstream null_stream;

// static ostream &debug() {
//   if (debug_output) {
//     return *(lamp_params.lamp_out);
//   } else {
//     return null_stream;
//   }
// }

uint64_t iterationcount[MAX_NUM_LOOPS];  // TRM
void LAMP_print_stats(ofstream &stream) {
  stream<<setprecision(3);
  stream<<"run_time: "<<1.0*(clock()-lamp_stats.start_time)/CLOCKS_PER_SEC<<endl;
  stream<<"Num dynamic stores: "<<lamp_stats.dyn_stores<<endl;
  stream<<"Num dynamic loads: "<<lamp_stats.dyn_loads<<endl;
  stream<<"Max loop nest depth: "<<loop_hierarchy.max_depth<<endl;
}

/***** Proto Types  *****/
template <class T> void LAMP_load(const uint32_t instr, const uint64_t addr);
template<class T> void LAMP_store(uint32_t instrID, uint64_t addr, uint64_t value);
void LAMP_loop_invocation_par(const uint16_t loop);
void LAMP_loop_iteration_begin_par(void);
void LAMP_loop_exit_par(const uint16_t loop);

Inline void lamp_streamWrite(volatile uint64_t *addr, uint64_t value)
{
//  *addr = value;

  __asm (
      "movntiq %1, (%0)\n"
      :
      : "r" (addr), "r" (value)
      );
}

/***** functions *****/
static void LAMP_SIGINT(int sig, siginfo_t *siginfo, void *dummy)
{
  const char msg[] = "\n\n*** Received CTRL-C. "
    "LAMP will shut down cleanly. "
    "Please wait. ***\n\n";
  int x = write(2,msg,sizeof(msg));
  if(x == -1)
    exit(x);
  exit(0);
}



/* Master function to handle all splitting of work,
   this could become a major bottle neck, can we split it up?
   SRB
   */
void* master(void *arg)
{
#if DEBUG
  printf("Starting Master Thread...\n");
  mstart = time(NULL);
#endif

#if 1

  //int i=0;
  uint32_t type, instr;
  uint64_t addr, value, tmp;

  uint64_t index = 0;

  while(addrArray[index] == 0);

  tmp = addrArray[index%MAX];
  lamp_streamWrite(&addrArray[index%MAX],0);
  index++;
  /*
     addr = addrArray[index];
     index++;
     */

  type = tmp >> 32;
  instr = tmp;
  value = 0;

  while ( type != FINISH )
  {

    switch(type)
    {
      case L1:
      case L2:
      case L4:
      case L8:
      case S1:
      case S2:
      case S4:
      case S8:
        addr = addrArray[index%MAX];
        lamp_streamWrite(&addrArray[index%MAX],0);
        index++;

        if(addr%13 == 0)
          switch(type)
          {
            case L1:
              LAMP_load<uint8_t>(instr, addr);
              break;
            case L2:
              LAMP_load<uint16_t>(instr, addr);
              break;
            case L4:
              LAMP_load<uint32_t>(instr, addr);
              break;
            case L8:
              LAMP_load<uint64_t>(instr, addr);
              break;

            case S1:
              LAMP_store<uint8_t>(instr, addr, value);
              break;
            case S2:
              LAMP_store<uint16_t>(instr, addr, value);
              break;
            case S4:
              LAMP_store<uint32_t>(instr, addr, value);
              break;
            case S8:
              LAMP_store<uint64_t>(instr, addr, value);
              break;
          }
        break;

      case LIB:
        LAMP_loop_iteration_begin_par();
        break;
      case LI:
        LAMP_loop_invocation_par(instr);
        break;
      case LE:
        LAMP_loop_exit_par(instr);
        break;
      case FINISH:
        pthread_exit(NULL);
    }




    while(addrArray[index%MAX] == 0);

    tmp = addrArray[index%MAX];
    lamp_streamWrite(&addrArray[index%MAX],0);
    index++;

    type = tmp >> 32;
    instr = tmp;
  }
#endif

#if DEBUG
  end = time(NULL);
  printf("Master thread exiting, took %f minutes\n", difftime(end,start)/60.0 );
#endif
  pthread_exit(NULL);
}


/**
 * Init function
 */
void LAMP_init(uint32_t num_instrs, uint32_t num_loops, uint64_t mem_gran, uint64_t flags) {
  lamp_params.lamp_out = new ofstream("result.lamp.profile");

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

  lamp_params.measure_iterations = false;
  if (((flags & 0x2) != 0) || (getenv("LAMP_PROFILE_MEASURE_ITERATIONS") != NULL)) {
    lamp_params.measure_iterations = true;
  }

  lamp_params.profile_flow = true;

  lamp_params.profile_output = false;
  if (((flags & 0x4) != 0) || (getenv("LAMP_PROFILE_PROFILE_OUTPUT") != NULL)) {
    lamp_params.profile_output = true;
    lamp_params.profile_flow = false;
  }

  lamp_stats.start_time = clock();
  lamp_stats.dyn_stores= 0;
  lamp_stats.dyn_loads= 0;
  lamp_stats.nest_depth = 0;
  lamp_stats.num_sync_arcs = 0;

  external_call_id = 0;

  memoryProfiler = new MemoryProfilerType(num_instrs);

  // timestamp 0 is the special first "iteration" of main
  loop_hierarchy.loopIteration(0, iterationcount);

  LoopInfoType &loopInfo = loop_hierarchy.getCurrentLoop();
  DependenceSets dependenceSets(MemoryProfilerType::MAX_TRACKED_DISTANCE);
  loopInfo.setItem(dependenceSets);

  time_stamp = 1;

  pageCache = PageCache(num_instrs);
  Pages pages(memory_stamp);
  for (uint32_t i = 0; i < num_instrs; i++) {
    pageCache[i] = pages;
  }



  addrArray = (uint64_t*)malloc(MAX_malloc);
  if(addrArray == NULL)
  {
    cerr << "Malloc failed\n";
    exit(-1);
  }
  memset((void*)addrArray, 0, MAX);
  pos = 0;


  /* Create watcher thread to enable parallel LAMP  SRB */
  pthread_create(&master_thread, NULL, master, NULL);
  q = sq_createQueue();


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
#if DEBUG
  start = time(NULL);
#endif
}

void LAMP_init_st() {
  LAMP_init(LAMP_param1, LAMP_param2, LAMP_param3, LAMP_param4);
}

void LAMP_finish()
{
#if DEBUG
  end = time(NULL);
  printf("Instrumented program took %f minutes\n",difftime(end,start)/60.0);
#endif

  void *status;
  uint64_t tmp = FINISH;
  tmp = tmp << 32;
  lamp_streamWrite(&addrArray[pos],tmp);
  /*
     sq_produce2(q,tmp,0);
     sq_flushQueue(q);
     */
  pthread_join(master_thread, &status);

  // Print out Loop Iteration Counts
  for (int i= 0; i < MAX_NUM_LOOPS; i++)
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
  LoopInfoType &loop = loop_hierarchy.findLoop(store_time_stamp);
  dep.loop = loop.loop_id;
  dep.dist = loop_hierarchy.calculateDistance(loop, store_time_stamp);

  return loop;
}

template <class T>
static void memory_profile(const uint32_t destId, const uint64_t addr) {
  Pages &pages = pageCache.at(destId);
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

    if (lamp_params.measure_iterations) {
      DependenceSets &dependenceSets = loopInfo.getItem();
      pair<DependenceSet::iterator, bool> result = dependenceSets[dep.dist].insert(dep);
      if (result.second) {
        profile.incrementLoop();
      }
    }
  } while ((++i) < sizeof(T));

  //debug()<<endl;
}

/***********************************************************************
 ******************Load Functions***************************************
 ***********************************************************************/
template <class T>
static void LAMP_aligned_load(const uint32_t instr, const uint64_t addr) {
  Pages &pages = pageCache.at(instr);

  if (!pages.getStampPage()->inPage((void *) addr)) {
    pages.setStampPage(memory_stamp.get_or_create_node((void *) addr));
  }

  if (lamp_params.profile_flow) {
    memory_profile<T>(instr, addr);
  }
}




template <class T>
static void LAMP_unaligned_load(const uint32_t instr, const uint64_t addr) {
  for (uint8_t i = 0; i < sizeof(T); i++) {
    LAMP_aligned_load<uint8_t>(instr, addr + i);
  }
}

template <class T>
void LAMP_load(const uint32_t instr, const uint64_t addr) {
  lamp_stats.dyn_loads++;

  if (!Memory::is_aligned<T>(addr)) {
    LAMP_unaligned_load<T>(instr, addr);
  } else {
    LAMP_aligned_load<T>(instr, addr);
  }
}



/* Unused ? */
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

/***********************************************************************
 ***************Store Functions*****************************************
 ***********************************************************************/
template<class T>
static void LAMP_aligned_store(uint32_t instrId, uint64_t addr) {
  Pages &pages = pageCache.at(instrId);
  if (!pages.getStampPage()->inPage((void *) addr)) {
    pages.setStampPage(memory_stamp.get_or_create_node((void *) addr));
  }

  if (lamp_params.profile_output) {
    memory_profile<T>(instrId, addr);
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
  lamp_stats.dyn_stores++;

  if (is_silent_store<T>(instrID, addr, value))
    return;

  if (!Memory::is_aligned<T>(addr)) {
    LAMP_unaligned_store<T>(instrID, addr);
  } else {
    LAMP_aligned_store<T>(instrID, addr);
  }
}



/* Unused ? */
void LAMP_external_store(const void * dest, const uint64_t size) {
  const uint64_t cptr = (uint64_t) (intptr_t) dest;
  for (uint64_t i = 0; i < size; i++) {
    const uint64_t addr = cptr + i;

    LAMP_aligned_store<uint8_t>(external_call_id, addr);
  }
}

/***********************************************************************
 ***********************************************************************
 ***********************************************************************/

static void invalidate_region(const void *memory, size_t size) {
  for (uint64_t i = 0; i < size; i++) {
    memory_stamp.set_invalid<uint8_t>((uint8_t*)memory+i);
  }
}

void LAMP_allocate(uint32_t lampId, const void *memory, size_t size) {
  invalidate_region(memory, size);
}

void LAMP_deallocate(uint32_t lampId, const void *memory, size_t size) {
  for (uint64_t i = 0; i < size; i++) {
    LAMP_aligned_store<uint8_t>(lampId, ((uint64_t)memory+i));
  }
  invalidate_region(memory, size);
}

void LAMP_external_allocate(const void *memory, size_t size) {
  LAMP_allocate(external_call_id, memory, size);
}

void LAMP_external_deallocate(const void *memory, size_t size) {
  LAMP_deallocate(external_call_id, memory, size);
}

void LAMP_allocate_st(void) {
  LAMP_allocate((uint32_t) LAMP_param1, (void *) LAMP_param2, (size_t) LAMP_param3);
}


  static void initializeSets() {
    if (!lamp_params.measure_iterations)
      return;

    LoopInfoType &loopInfo = loop_hierarchy.getCurrentLoop();
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

void LAMP_loop_iteration_begin_par(void) {
  time_stamp++;
  loop_hierarchy.loopIteration(time_stamp, iterationcount);
  initializeSets();
}




void LAMP_loop_exit_par(const uint16_t loop) {
  loop_hierarchy.exitLoop(loop);
}



void LAMP_loop_invocation_par(const uint16_t loop)
{
  //printf("LAMP_loop_invocation_par loop:%d\n",loop);
  loop_hierarchy.enterLoop(loop, time_stamp);
  initializeSets();
}



void LAMP_register(uint32_t id)
{
  external_call_id = id;
  LAMP_param1 = id;
}

void LAMP_register_st(void)
{
  LAMP_register((uint32_t) LAMP_param1);
}

#endif /* HAS_SMTX */
