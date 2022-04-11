#include <bits/stdint-uintn.h>
#include <cassert>
#include <cerrno>
#include <clocale>
#include <cstdint>
#include <tuple>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include "malloc.h"

#include "slamp_timestamp.h"
#include "slamp_logger.h"
#include "slamp_hooks.h"
#include "slamp_shadow_mem.h"
#include "slamp_bound_malloc.h"
#include "slamp_debug.h"

#include "slamp_timer.h"


#include <set>
#include <map>
#include <utility>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#define TURN_OFF_CUSTOM_MALLOC do {\
  __malloc_hook = old_malloc_hook; \
  __free_hook = old_free_hook; \
} while (false);

#define TURN_ON_CUSTOM_MALLOC do { \
  __malloc_hook = SLAMP_malloc_hook; \
  __free_hook = SLAMP_free_hook; \
} while (false);

// shadow memory parameters

#define HEAP_BOUND_LOWER 0x010000000000L
#define HEAP_BOUND_HIGHER (0xFFFFFFFFFFFFL-BOUND_LOWER+1)
// 8MB max stack size for a typical host machine `ulimit -s`
#define SIZE_8M  0x800000

// debugging tools

#if 0
static char** dbggv = NULL;
void SLAMP_dbggv(int id) {
  if (dbggv) {
    fprintf(stderr, "%u, dbggv %p\n", id, *dbggv);
  }
}

void SLAMP_dbggvstr(char* str) {
  if (dbggv) {
    fprintf(stderr, "%s, dbggv %p\n", str, *dbggv);
  }
}
#endif

#if DEBUG
uint8_t __slamp_begin_trace = 0;
#endif

bool DISTANCE_MODULE = false;
bool CONSTANT_ADDRESS_MODULE = false;
bool LINEAR_ADDRESS_MODULE = false;
bool CONSTANT_VALUE_MODULE = false;
bool LINEAR_VALUE_MODULE = false;
bool REASON_MODULE = false;
bool TRACE_MODULE = false;

uint64_t __slamp_iteration = 0;
uint64_t __slamp_invocation = 0;

uint64_t __slamp_load_count = 0;
uint64_t __slamp_store_count = 0;
uint64_t __slamp_malloc_count = 0;
uint64_t __slamp_free_count = 0;

std::map<void*, size_t>* alloc_in_the_loop;

// Type of the access callback function
// instr, bare_instr, address, value, size
using AccessCallbackTy = void (*)(uint32_t, uint32_t, uint64_t, uint64_t, uint8_t);

// Callback functions
void slamp_access_callback_constant_value(uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size);
void slamp_access_callback_constant_addr(uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size);

// Callback function pointers
AccessCallbackTy access_callbacks[8] = {
  // &slamp_access_callback_constant_addr,
  // &slamp_access_callback_linear_address,
  // &slamp_access_callback_constant_value,
  // &slamp_access_callback_linear_value,
  // &slamp_access_callback_reason,
  // &slamp_access_callback_trace
};

template <typename T1, typename T2>
struct PairHash
{
    std::size_t operator () (std::pair<T1, T2> const &v) const
    {
      std::hash<uint32_t> hash_fn;

      return hash_fn(v.first) ^ hash_fn(v.second);
    }
};

// instr, bare_instr
using AccessKey = std::pair<uint32_t, uint32_t>;
struct Constant {
  bool valid;
  bool valueinit;
  uint8_t size;
  uint64_t addr;
  uint64_t value;
  char pad[64 - sizeof(uint64_t) - sizeof(uint64_t) - sizeof(uint8_t) -
           sizeof(bool) - sizeof(bool)];

  Constant(bool va, bool vi, uint8_t s, uint64_t a, uint64_t v)
      : valid(va), valueinit(vi), size(s), addr(a), value(v) {}
};

struct LinearPredictor {
  using value = union {
    int64_t ival;
    double dval;
  };

  uint64_t addr;
  int64_t ia;
  int64_t ib;
  double da;
  double db;
  int64_t x;
  value y;
  bool init;
  bool ready;
  bool stable;
  bool valid_as_int;
  bool valid_as_double;

  LinearPredictor(int64_t x1, int64_t y1, uint64_t addr)
      : addr(addr), init(false), ready(false), stable(false),
        valid_as_int(true), valid_as_double(true) {
    ia = ib = 0;
    da = db = 0.0;
    x = x1;
    y.ival = y1;
  }

  void add_sample(int64_t x1, int64_t y1, uint64_t sample_addr) {
    if (!valid_as_int && !valid_as_double)
      return;


    // // Remove check for constant need to have the same address
    // if (addr != sample_addr) {
    //   valid_as_int = valid_as_double = false;
    //   return;
    // }

    if (!init) {
      x = x1;
      y.ival = y1;
      init = true;
    } else if (!ready) {
      if ((x == x1 && y.ival != y1) || (x != x1 && y.ival == y1)) {
        valid_as_int = valid_as_double = false;
        return;
      }

      if (x == x1 && y.ival == y1) {
        // Nothing to do but not ready yet
        return;
      }

      // for int
      {
        int64_t y_diff = y1 - y.ival;
        int64_t x_diff = x1 - x;

        ia = y_diff / x_diff;
        ib = y.ival - (ia * x);
      }

      // for double
      {
        value vy;
        vy.ival = y1;

        double y_diff = vy.dval - y.dval;
        double x_diff = (double)x1 - (double)x;

        da = y_diff / x_diff;
        db = y.dval - (da * x);
      }

      ready = true;
    } else {
      if (valid_as_int) {
        if ((ia * x1 + ib) != y1)
          valid_as_int = false;
      }

      if (valid_as_double) {
        value vy;
        vy.ival = y1;

        if ((da * (double)x1 + db) != vy.dval)
          valid_as_double = false;
      }

      stable = true;
    }
  }
};

static std::unordered_map<AccessKey, Constant *, PairHash<uint32_t, uint32_t>> *constmap_value;
static std::unordered_map<AccessKey, Constant *, PairHash<uint32_t, uint32_t>> *constmap_addr;
static std::unordered_map<AccessKey, LinearPredictor *, PairHash<uint32_t, uint32_t> > *lpmap_value;
static std::unordered_map<AccessKey, LinearPredictor *, PairHash<uint32_t, uint32_t> > *lpmap_addr;

void slamp_access_callback_constant_value(uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  AccessKey key(instr, bare_instr);
  if (constmap_value->count(key) != 0) {
    auto cp = (*constmap_value)[key];

    // // Remove check for constant need to have the same address
    // if (cp->valueinit && cp->addr != addr)
    //   cp->valid = false;
    if (cp->valid) {
      if (cp->valueinit && cp->value != value) {
        cp->valid = false;
      }
      else {
        cp->valueinit = true;
        cp->value = value;
        cp->addr = addr;
      }
    }
  } else {
    auto cp = new Constant(1, 1, size, addr, value);
    constmap_value->insert(std::make_pair(key, cp));
  }
}

void slamp_access_callback_constant_addr(uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  AccessKey key(instr, bare_instr);
  if (constmap_addr->count(key) != 0) {
    auto cp = (*constmap_addr)[key];

    // // Remove check for constant need to have the same address
    // if (cp->valueinit && cp->addr != addr)
    //   cp->valid = false;
    if (cp->valid) {
      if (cp->valueinit && cp->value != addr) {
        cp->valid = false;
      }
      else {
        cp->valueinit = true;
        cp->value = addr;
        cp->addr = addr;
      }
    }
  } else {
    auto cp = new Constant(1, 1, size, addr, addr);
    constmap_addr->insert(std::make_pair(key, cp));
  }
}


void slamp_access_callback_linear_value(uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  // check if linear predictable. constkey can be reused here.
  auto key = AccessKey(instr, bare_instr);
  if (LINEAR_VALUE_MODULE) {
    if (lpmap_value->count(key)) {
      auto lp = (*lpmap_value)[key];
      lp->add_sample(__slamp_iteration, value, addr);
    } else {
      auto lp = new LinearPredictor(__slamp_iteration, value, addr);
      lpmap_value->insert(std::make_pair(key, lp));
    }
  }
}

void slamp_access_callback_linear_addr(uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  // check if linear predictable.
  auto key = AccessKey(instr, bare_instr);
  if (LINEAR_ADDRESS_MODULE) {
    if (lpmap_addr->count(key)) {
      auto lp_addr = (*lpmap_addr)[key];
      // this one checks for if addr is linear
      lp_addr->add_sample(__slamp_iteration, addr, addr);
    } else {
      auto lp_addr = new LinearPredictor(__slamp_iteration, addr, addr);
      lp_addr->valid_as_double = false;
      lpmap_addr->insert(std::make_pair(key, lp_addr));
    }
  }
}

static uint32_t          context = 0;
slamp::MemoryMap* smmap = nullptr;

struct InstructionRecord {
  uint64_t last_addr;
  uint64_t last_iter;
  static const uint64_t INVALID = UINT64_MAX;
};

std::unordered_map<uint32_t, InstructionRecord*> instructionMap;

static void updateInstruction(uint32_t instr, uint64_t addr) {
  if (!REASON_MODULE) {
    return;
  }

  if (instructionMap.find(instr) != instructionMap.end()) {
    auto &record = instructionMap[instr];
    record->last_addr = addr;
    record->last_iter = __slamp_iteration;
  } else {
    auto &record = instructionMap[instr];
    record = new InstructionRecord{addr, __slamp_iteration};
  }
}

// -1->inconclusive
// 0->killed
// 1->no-alias
// 2->no-path
enum NoDepReason {
  INCONCLUSIVE=-1,
  KILLED=0,
  NO_ALIAS=1,
  NO_PATH=2,
};

static NoDepReason reasonLoad(uint32_t store_instr, uint64_t addr) {
  if (!REASON_MODULE) {
    return NoDepReason::INCONCLUSIVE;
  }

  // no path
  if (!instructionMap.count(store_instr)) {
    return NoDepReason::NO_PATH;
  } 

  auto record = instructionMap[store_instr];

  if (record->last_iter == InstructionRecord::INVALID){
    return NoDepReason::NO_PATH;
  }

  // no alias
  if (record->last_addr != addr) {
    return NoDepReason::NO_ALIAS;
  }

  // killed
  return NoDepReason::KILLED;
}

using DepPair = std::pair<uint32_t, uint32_t>;
using ReasonCounter = std::array<unsigned, 4>;

static std::unordered_map<DepPair, ReasonCounter, PairHash<uint32_t, uint32_t>> *depReasonMap;
static  uint32_t STORE_INST = 33;

// update depReasonMap
static void updateReasonMap(uint32_t inst, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  if (!REASON_MODULE) {
    return;
  }

  // uint32_t store_inst = 18603;
  NoDepReason reason = reasonLoad(STORE_INST, addr);
  unsigned reasonIdx = reason + 1;

  DepPair dep = std::make_pair(STORE_INST, inst);
  (*depReasonMap)[dep][reasonIdx]++;
}

// dump all access event modules
static void accessModuleDump(std::string fname) {
  std::ofstream of(fname, std::ios::app);

  auto printCp = [&of](AccessKey key, Constant *cp) {
    of << "(" << key.first << ":" << key.second << ")" << " ["
      << cp->valid << " " << (unsigned)(cp->valid ? cp->size : 0) << " " << (cp->valid ? cp->value: 0) << "]";
  };

  auto printLp = [&of](AccessKey key, LinearPredictor *lp) {
    bool lp_int_valid = (lp->stable && lp->valid_as_int);
    bool lp_double_valid = (lp->stable && lp->valid_as_double);
    of << "(" << key.first << ":" << key.second << ")" <<  " ["
      << lp_int_valid << " " << (lp_int_valid ? lp->ia : 0) << " "
      << (lp_int_valid ? lp->ib : 0) << " " << lp_double_valid << " "
      << (lp_double_valid ? lp->da : 0) << " "
      << (lp_double_valid ? lp->db : 0) << "]";
  };

  if (CONSTANT_VALUE_MODULE) {
    // dump constant_value map
    of << "constant_value_map:\n";
    for (auto &[key, cp] : *constmap_value) {
      if (cp->valid) {
        printCp(key, cp);
        of << "\n";
      }
    }
  }
  if (CONSTANT_ADDRESS_MODULE) {
    // dump constant_address map
    of << "constant_address_map:\n";
    for (auto &[key, cp] : *constmap_addr) {
      if (cp->valid) {
        printCp(key, cp);
        of << "\n";
      }
    }
  }

  if (LINEAR_VALUE_MODULE) {
    // dump linear_value map
    of << "linear_value_map:\n";
    for (auto &[key, lp] : *lpmap_value) {
      if (lp->valid_as_int || lp->valid_as_double) {
        printLp(key, lp);
        of << "\n";
      }
    }
  }

  if (LINEAR_ADDRESS_MODULE) {
    // dump linear_address map
    of << "linear_address_map:\n";
    for (auto &[key, lp] : *lpmap_addr) {
      if (lp->valid_as_int || lp->valid_as_double) {
        printLp(key, lp);
        of << "\n";
      }
    }
  }
}

static void dumpReason() {
  if (REASON_MODULE) {
    std::ofstream of("slamp_reason.dump", std::ios::app);
    of << "From: " << STORE_INST << "; " << depReasonMap->size() << " deps \n";

    for (auto &[dep, counter]: *depReasonMap) {
      of << dep.first << "->" << dep.second << ": [";
      for (int i = 0; i < 4; i++) {
        of << counter[i] << ",";
      }
      of << "]\n";
    }

    of.close();
  }
}

static uint32_t invokedepth = 0; // recursive function

static void dumpstack()  {
  unw_cursor_t cursor;
  unw_context_t uc;
  unw_word_t ip;

  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);

  // move up once because current stack frame (within handler) is meaningless

  assert(unw_step(&cursor) > 0);
  unw_get_reg(&cursor, UNW_REG_IP, &ip);

  while (unw_step(&cursor) > 0)
  {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    fprintf(stderr, "pid: %u, %p\n", getpid(), (void*)ip);
  }
}

static void __slamp_sigsegv_handler(int sig, siginfo_t *siginfo, void *dummy)
{
  fprintf(stderr, "sigsegv handler!!\n");

  // Block signals during the stack trace.
  sigset_t block_all, old_sigs;
  sigfillset(&block_all);
  sigprocmask(SIG_BLOCK, &block_all, &old_sigs);

  dumpstack();
#if 0
  char   buf[256];
  char*  line = NULL;
  size_t len = 0;
  sprintf(buf, "/proc/%u/maps", getpid());

  FILE* fp = fopen(buf, "r");
  while (getline(&line, &len, fp) != -1) {
    fprintf(stderr, "%s", line);
  }
#endif
  sigprocmask(SIG_SETMASK, &old_sigs, 0);

  _exit(1);
}


static void *(*old_malloc_hook)(unsigned long, const void *);
static void (*old_free_hook)(void *, const void *);

static void* SLAMP_malloc_hook(size_t size, const void * /*caller*/) {
  auto ptr = SLAMP_malloc(size);

  __slamp_malloc_count++;
  return ptr;
}

static void SLAMP_free_hook(void *ptr, const void * /*caller*/) {
  SLAMP_free(ptr);
  __slamp_free_count++;
}


void SLAMP_init(uint32_t fn_id, uint32_t loop_id)
{
  constmap_value = new std::unordered_map<AccessKey, Constant *, PairHash<uint32_t, uint32_t>>();
  constmap_addr = new std::unordered_map<AccessKey, Constant *, PairHash<uint32_t, uint32_t>>();
  lpmap_value  = new std::unordered_map<AccessKey, LinearPredictor *, PairHash<uint32_t, uint32_t>>();
  lpmap_addr = new std::unordered_map<AccessKey, LinearPredictor *, PairHash<uint32_t, uint32_t>>();

  // per instruction map


  auto setModule = [](bool &var, const char *name) {
    auto *module = getenv(name);
    if (module && strcmp(module, "1") == 0) {
      var= true;
    }
  };

  // check if the modules are turned on in the environment variable
  setModule(DISTANCE_MODULE, "DISTANCE_MODULE");
  setModule(CONSTANT_VALUE_MODULE, "CONSTANT_VALUE_MODULE");
  setModule(LINEAR_VALUE_MODULE, "LINEAR_VALUE_MODULE");
  setModule(CONSTANT_ADDRESS_MODULE, "CONSTANT_ADDRESS_MODULE");
  setModule(LINEAR_ADDRESS_MODULE, "LINEAR_ADDRESS_MODULE");
  setModule(REASON_MODULE, "REASON_MODULE");
  setModule(TRACE_MODULE, "TRACE_MODULE");

  if (CONSTANT_VALUE_MODULE) {
    access_callbacks[0] = &slamp_access_callback_constant_value;
  }

  if (CONSTANT_ADDRESS_MODULE) {
    access_callbacks[1] = &slamp_access_callback_constant_addr;
  }

  if (LINEAR_VALUE_MODULE) {
    access_callbacks[2] = &slamp_access_callback_linear_value;
  }

  if (LINEAR_ADDRESS_MODULE) {
    access_callbacks[3] = &slamp_access_callback_linear_addr;
  }

  if (REASON_MODULE) {
    auto *store = getenv("STORE_INST");
    if (store) {
      STORE_INST = atoi(store);
    }
    depReasonMap = new std::unordered_map<DepPair, ReasonCounter, PairHash<uint32_t, uint32_t>>();
  }

  uint64_t START;
  TIME(START);
  // initializing customized malloc should be done very first

  slamp::init_bound_malloc((void*)(HEAP_BOUND_LOWER));

  smmap = new slamp::MemoryMap(TIMESTAMP_SIZE_IN_BYTES);

  slamp::init_logger(fn_id, loop_id);
  TADD(overhead_init_fini, START);


  TIME(START);
  // allocate shadow for linux standard base, etc

  smmap->allocate((void*)&errno, sizeof(errno));
  smmap->allocate((void*)&stdin, sizeof(stdin));
  smmap->allocate((void*)&stdout, sizeof(stdout));
  smmap->allocate((void*)&stderr, sizeof(stderr));
  smmap->allocate((void*)&sys_nerr, sizeof(sys_nerr));

  {
    const unsigned short int* ctype_ptr = (*__ctype_b_loc()) - 128;
    smmap->allocate((void*)ctype_ptr, 384 * sizeof(*ctype_ptr));
  }
  {
    const int32_t* itype_ptr = (*__ctype_tolower_loc()) - 128;
    smmap->allocate((void*)itype_ptr, 384 * sizeof(*itype_ptr));
  }
  {
    const int32_t* itype_ptr = (*__ctype_toupper_loc()) - 128;
    smmap->allocate((void*)itype_ptr, 384 * sizeof(*itype_ptr));
  }

  alloc_in_the_loop = new std::map<void*, size_t>();
  TADD(overhead_shadow_allocate, START);

  TIME(START);
  // install sigsegv handler for debuggin

  struct sigaction replacement;
  replacement.sa_flags = SA_SIGINFO;
  sigemptyset(&replacement.sa_mask );
  replacement.sa_sigaction = &__slamp_sigsegv_handler;
  if ( sigaction(SIGSEGV, &replacement, nullptr) < 0 ) {
    assert(false && "SIGSEGV handler install failed");
  }
  TADD(overhead_init_fini, START);
}

void SLAMP_fini(const char* filename)
{
  uint64_t START;
  TIME(START);
  delete alloc_in_the_loop;

  slamp::fini_logger(filename);

  // delete smmap;

  // slamp::fini_bound_malloc();
  TADD(overhead_init_fini, START);
  slamp_time_dump("slamp_overhead.dump");

  accessModuleDump("slamp_access_module.dump");

  // dump 
  dumpReason();
}

void SLAMP_allocated(uint64_t addr)
{
  std::cout << std::hex << " allocated? " << smmap->is_allocated((void*)addr) << std::dec << std::endl;
}

/// Allocate the shadow memory on the "heap" for global variables
void SLAMP_init_global_vars(uint64_t addr, size_t size)
{
  smmap->allocate((void*)addr, size);
}

void SLAMP_main_entry(uint32_t argc, char** argv, char** env)
{
#if DEBUG
  __slamp_begin_trace = 1;

  std::cout << "[main_entry] begin: " << std::hex << begin << std::dec << std::endl;
#endif

  // preallocate the stack
  smmap->init_stack(SIZE_8M);

  if (argc != 0)
  {
    smmap->allocate(argv, sizeof(void*) * argc);

    for (unsigned i = 0 ; i < argc ; i++)
    {
      size_t len = strlen( argv[i] );
      smmap->allocate(argv[i], len+1);
    }
  }

  if (env)
  {
    // env is terminated by a NULL entry
    char** p = env;

    unsigned env_len = 0;
    while (*p != nullptr)
    {
      char* entry = *p;
      size_t len = strlen(entry);

      smmap->allocate(entry, len+1);
      p++;
      env_len++;
    }
    smmap->allocate( env, sizeof(char*) * (env_len+1) );
  }


  // replace hooks
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  __malloc_hook = SLAMP_malloc_hook;
  __free_hook = SLAMP_free_hook;
}

/// update the invocation count
void SLAMP_loop_invocation() {
  // fprintf(stderr, "SLAMP_loop_invocation, depth: %u\n", invokedepth);
  invokedepth++;

  if (invokedepth > 1)
    return;

  for (auto &[k, v]: instructionMap) {
    v->last_iter = InstructionRecord::INVALID;
  }

  ++__slamp_invocation;
  ++__slamp_iteration;

#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "[invoke] " << (__slamp_invocation) << "\n" << std::flush;
#endif
}

void SLAMP_loop_iteration()
{
  //fprintf(stderr, "SLAMP_loop_iteration, depth: %u\n", invokedepth);
  if (invokedepth > 1) return;

  __slamp_iteration++;

#if DEBUG
  if (__slamp_begin_trace) std::cout << "[iter] " << (__slamp_iteration) << "\n" << std::flush;
#endif
}

void SLAMP_loop_exit() {
  // fprintf(stderr, "SLAMP_loop_exit, depth: %u\n", invokedepth);
  if (invokedepth == 0)
    return;

  invokedepth--;
}

/// set the context of the call inside a loop
void SLAMP_push(const uint32_t instr) {
  if (invokedepth > 1)
    return;

#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "  %%push%% " << instr << " iteration " << __slamp_iteration
              << "\n"
              << std::flush;
#endif

  assert(context == 0);
  context = instr;
}

/// unset the context of the call inside a loop
void SLAMP_pop() {
  if (invokedepth > 1)
    return;

#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "  %%pop%% " << context << " iteration " << __slamp_iteration
              << "\n"
              << std::flush;
#endif

  context = 0;
}

// FIXME: a temporary patch for out of handling program original heap
bool SLAMP_isBadAlloc(uint64_t addr) {
  const uint64_t  lower = 0x100000000L;
  const uint64_t higher =  0x010000000000L;
  const uint64_t heapStart = smmap->heapStart;

  if (addr < lower && addr > heapStart) {
    return true;
  }

  return false;
}

template <unsigned size>
void SLAMP_load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value) {
  if (invokedepth > 1)
    instr = context;
  if (SLAMP_isBadAlloc(addr))
    return;

  __slamp_load_count++;
#if DEBUG
  if (__slamp_begin_trace) std::cout << "    load"<< size << " " << instr << "," << bare_instr << " iteration " << __slamp_iteration << " addr " << std::hex << addr << " value " << value << std::dec << "\n" << std::flush;
  if (!smmap->is_allocated(reinterpret_cast<void*>(addr))) {
    std::cout << "Error: shadow memory not allocated" << std::flush;
    assert(false);
  }
#endif

  uint64_t START;
  TIME(START);

  TS* s = (TS*)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS tss[8]; // HACK: avoid using malloc
  for (auto i = 0; i < size; i++) {
    tss[i] = s[i];
  }

  TADD(overhead_shadow_read, START);

  TIME(START);

  for (auto i = 0; i < size; i++) {
    bool cond = true;
    for (auto j = 0; j < i; j++) {
      cond = cond && (tss[i] != tss[j]);
    }

    if (cond) {
      uint32_t src_inst = slamp::log(tss[i], instr, s, bare_instr, addr, value, size);
      // FIXME: no dependence, not consider other branches
      if (src_inst != STORE_INST) {
        updateReasonMap(instr, bare_instr, addr, value, size);
      }
    }
  }

  for (auto *f: access_callbacks) {
    TURN_OFF_CUSTOM_MALLOC;
    if (f) {
      f(instr, bare_instr, addr, value, size);
    }
    TURN_ON_CUSTOM_MALLOC;
  }

  TADD(overhead_log_total, START);
}

void SLAMP_load1(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value)
{
  SLAMP_load<1>(instr, addr, bare_instr, value);
}

void SLAMP_load2(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value)
{
  SLAMP_load<2>(instr, addr, bare_instr, value);
}

void SLAMP_load4(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value)
{
  SLAMP_load<4>(instr, addr, bare_instr, value);
}

void SLAMP_load8(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value)
{
  SLAMP_load<8>(instr, addr, bare_instr, value);
}

void SLAMP_loadn(uint32_t instr, const uint64_t addr, const uint32_t bare_instr,
                 size_t n) {
  if (SLAMP_isBadAlloc(addr))
    return;
  if (invokedepth > 1)
    instr = context;

#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    loadn " << instr << "," << bare_instr << " iteration "
              << __slamp_iteration << " addr " << std::hex << addr << std::dec
              << "\n"
              << std::flush;
  if (!smmap->is_allocated(reinterpret_cast<void *>(addr))) {
    std::cout << "Error: shadow memory not allocated" << std::flush;
    assert(false);
  }
#endif

  uint64_t START;

  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);

  // FIXME: beware of the malloc hook being changed at this point, any allocation is super costly
  std::unordered_set<TS> m;

  bool noDep = true;
  for (unsigned i = 0; i < n; i++) {
    TIME(START);
    TS ts = s[i];
    TADD(overhead_shadow_read, START);


    TIME(START);
    if (m.count(ts) == 0) {
      uint32_t src_inst = slamp::log(ts, instr, s + i, 0, addr + i, 0, 0);
      if (src_inst != STORE_INST) {
        noDep = false;
      }
      m.insert(ts);
    }
    TADD(overhead_log_total, START);
  }

  if (noDep) {
    updateReasonMap(instr, bare_instr, addr, 0, n);
  }
}

template <unsigned size>
void SLAMP_load_ext(const uint64_t addr, const uint32_t bare_instr,
                     uint64_t value) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    load" << size << "_ext " << context << "," << bare_instr
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif
  if (context)
    SLAMP_load<size>(context, addr, bare_instr, value);
}


void SLAMP_load1_ext(const uint64_t addr, const uint32_t bare_instr,
                     uint64_t value) {
  SLAMP_load_ext<1>(addr, bare_instr, value);
}

void SLAMP_load2_ext(const uint64_t addr, const uint32_t bare_instr,
                     uint64_t value) {
  SLAMP_load_ext<2>(addr, bare_instr, value);
}

void SLAMP_load4_ext(const uint64_t addr, const uint32_t bare_instr,
                     uint64_t value) {
  SLAMP_load_ext<4>(addr, bare_instr, value);
}

void SLAMP_load8_ext(const uint64_t addr, const uint32_t bare_instr,
                     uint64_t value) {
  SLAMP_load_ext<8>(addr, bare_instr, value);
}

void SLAMP_loadn_ext(const uint64_t addr, const uint32_t bare_instr, size_t n) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    loadn_ext " << context << "," << bare_instr
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif

  if (context)
    SLAMP_loadn(context, addr, bare_instr, n);
}

template <unsigned size>
void SLAMP_store(uint32_t instr, const uint64_t addr) {
  if (SLAMP_isBadAlloc(addr))
    return;
  if (invokedepth > 1)
    instr = context;

  __slamp_store_count++;

  updateInstruction(instr, addr);
#if DEBUG
  uint64_t *ptr = (uint64_t *)addr;
  if (__slamp_begin_trace)
    std::cout << "    store" << size << " " << instr << " iteration " << __slamp_iteration
              << " addr " << std::hex << addr << " value " << *ptr << std::dec
              << "\n"
              << std::flush;
  if (!smmap->is_allocated(reinterpret_cast<void *>(addr))) {
    std::cout << "Error: shadow memory not allocated" << std::flush;
    assert(false);
  }
#endif
  uint64_t START;
  TIME(START);

  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS ts = CREATE_TS(instr, __slamp_iteration, __slamp_invocation);

  // TODO: handle output dependence. ignore it as of now.
  for (auto i = 0; i < size; i++)
    s[i] = ts;

  TADD(overhead_shadow_write, START);
  slamp::capturestorecallstack(s);
}


void SLAMP_store1(uint32_t instr, const uint64_t addr) {
  SLAMP_store<1>(instr, addr);
}

void SLAMP_store2(uint32_t instr, const uint64_t addr) {
  SLAMP_store<2>(instr, addr);
}

void SLAMP_store4(uint32_t instr, const uint64_t addr) {
  SLAMP_store<4>(instr, addr);
}

void SLAMP_store8(uint32_t instr, const uint64_t addr) {
  SLAMP_store<8>(instr, addr);
}

void SLAMP_storen(uint32_t instr, const uint64_t addr, size_t n) {
  if (SLAMP_isBadAlloc(addr))
    return;
  if (invokedepth > 1)
    instr = context;

  updateInstruction(instr, addr);
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    storen " << instr << " iteration " << __slamp_iteration
              << " addr " << std::hex << addr << std::dec << "\n"
              << std::flush;
  if (!smmap->is_allocated(reinterpret_cast<void *>(addr))) {
    std::cout << "Error: shadow memory not allocated" << std::flush;
    assert(false);
  }
#endif

  uint64_t START;
  TIME(START);

  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS ts = CREATE_TS(instr, __slamp_iteration, __slamp_invocation);

  // TODO: handle output dependence. ignore it as of now.

  for (unsigned i = 0; i < n; i++)
    s[i] = ts;

  TADD(overhead_shadow_write, START);
  slamp::capturestorecallstack(s);
}

template <unsigned size>
void SLAMP_store_ext(const uint64_t addr, const uint64_t bare_inst) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    store" << size << "_ext " << context << "," << bare_inst
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif

  if (context)
    SLAMP_store<size>(context, addr);
}


void SLAMP_store1_ext(const uint64_t addr, const uint64_t bare_inst) {
  SLAMP_store_ext<1>(addr, bare_inst);
}

void SLAMP_store2_ext(const uint64_t addr, const uint64_t bare_inst) {
  SLAMP_store_ext<2>(addr, bare_inst);
}

void SLAMP_store4_ext(const uint64_t addr, const uint64_t bare_inst) {
  SLAMP_store_ext<4>(addr, bare_inst);
}

void SLAMP_store8_ext(const uint64_t addr, const uint64_t bare_inst) {
  SLAMP_store_ext<8>(addr, bare_inst);
}

void SLAMP_storen_ext(const uint64_t addr, const uint64_t bare_inst, size_t n) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    storen_ext " << context << "," << bare_inst
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif

  if (context)
    SLAMP_storen(context, addr, n);
}

/*
 * External library wrappers
 */

void* SLAMP_malloc(size_t size)
{
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;

  uint64_t START;
  TIME(START);
  //fprintf(stderr, "SLAMP_malloc, size: %lu\n", size);
  void* result = (void*)slamp::bound_malloc(size);
  unsigned count = 0;
  while( true )
  {
    if ( !result ) {

      TADD(overhead_shadow_allocate, START);
      __malloc_hook = SLAMP_malloc_hook;
      __free_hook = SLAMP_free_hook;
      return nullptr;
    }

    void* shadow = smmap->allocate(result, size);
    if (shadow)
    {
      if (context)
      {
        (*alloc_in_the_loop)[result] = size;
      }
      //std::cout << "SLAMP_malloc result is " << std::hex << result << "\n";
      TADD(overhead_shadow_allocate, START);
      __malloc_hook = SLAMP_malloc_hook;
      __free_hook = SLAMP_free_hook;
      return result;
    }
    else
    {
      slamp::bound_free(result);
      count++;

      if (count == 1024)
      {
        perror("Error: shadow_alloc failed\n");
        exit(0);
      }

      slamp::bound_discard_page();
      result = (void*)slamp::bound_malloc(size);
    }
  }
}

void  SLAMP_free(void* ptr)
{
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  slamp::bound_free(ptr);
  __malloc_hook = SLAMP_malloc_hook;
  __free_hook = SLAMP_free_hook;
}
