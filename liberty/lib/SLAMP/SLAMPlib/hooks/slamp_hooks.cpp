#include <bits/stdint-uintn.h>
#include <cassert>
#include <cerrno>
#include <clocale>
#include <cstdint>
#include <tuple>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include "malloc.h"

#include "json.hpp"
#include "slamp_timestamp.h"
#include "slamp_logger.h"
#include "slamp_hooks.h"
#include "slamp_shadow_mem.h"
#include "slamp_bound_malloc.h"
#include "slamp_debug.h"

#include "slamp_timer.h"
#include "context.h"


#include <set>
#include <map>
#include <list>
#include <utility>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

using namespace SLAMPLib;

#define TURN_OFF_CUSTOM_MALLOC do {\
  __malloc_hook = old_malloc_hook; \
  __free_hook = old_free_hook; \
  __memalign_hook = old_memalign_hook; \
} while (false);

#define TURN_ON_CUSTOM_MALLOC do { \
  __malloc_hook = SLAMP_malloc_hook; \
  __free_hook = SLAMP_free_hook; \
  __memalign_hook = SLAMP_memalign_hook; \
} while (false);

// shadow memory parameters

#define HEAP_BOUND_LOWER 0x010000000000L
#define HEAP_BOUND_HIGHER (0xFFFFFFFFFFFFL-BOUND_LOWER+1)
// 8MB max stack size for a typical host machine `ulimit -s`
#define SIZE_8M  0x800000

#define  FORMAT_INST_ARG(fcnId, argId) (fcnId << 5 | (0x1f & (argId  << 4) | 0x1))
#define FORMAT_INST_INST(instId) (instId << 1 | 0x0)
// #define LOCALWRITE(addr) if (true)

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

extern bool LOCALWRITE_MODULE;

#ifndef ITO_ENABLE
bool DEPENDENCE_MODULE; // = true;
bool POINTS_TO_MODULE; // = false;
bool DISTANCE_MODULE = false;
bool CONSTANT_ADDRESS_MODULE; // = false;
bool LINEAR_ADDRESS_MODULE; // = false;
bool CONSTANT_VALUE_MODULE; // = false;
bool LINEAR_VALUE_MODULE; // = false;
bool REASON_MODULE; // = false;
bool TRACE_MODULE; // = false;
// bool LOCALWRITE_MODULE=false;
bool ASSUME_ONE_ADDR = false;
#else
extern bool DEPENDENCE_MODULE; // = true;
extern bool POINTS_TO_MODULE; // = false;
bool DISTANCE_MODULE = false;
extern bool CONSTANT_ADDRESS_MODULE; // = false;
extern bool LINEAR_ADDRESS_MODULE; // = false;
extern bool CONSTANT_VALUE_MODULE; // = false;
extern bool LINEAR_VALUE_MODULE; // = false;
extern bool REASON_MODULE; // = false;
extern bool TRACE_MODULE; // = false;
extern bool ASSUME_ONE_ADDR;
#endif

// #ifdef ITO_ENABLE
extern size_t LOCALWRITE_MASK;
extern size_t LOCALWRITE_PATTERN;
// #else
// size_t LOCALWRITE_MASK = 0;
// size_t LOCALWRITE_PATTERN = 0;
// #endif

#define LOCALWRITE(addr)  ((!LOCALWRITE_MODULE || ((size_t)addr & LOCALWRITE_MASK) == LOCALWRITE_PATTERN))

static void *(*old_malloc_hook)(size_t, const void *);
static void (*old_free_hook)(void *, const void *);
static void *(*old_memalign_hook)(size_t, size_t, const void *);

uint64_t __slamp_iteration = 0;
uint64_t __slamp_invocation = 0;

uint64_t __slamp_load_count = 0;
uint64_t __slamp_store_count = 0;
uint64_t __slamp_malloc_count = 0;
uint64_t __slamp_free_count = 0;
SpecPrivLib::SpecPrivContextManager *contextManager;
std::unordered_set<unsigned long> *shortLivedObjects, *longLivedObjects;
std::unordered_set<ContextHash> *targetLoopContexts;

// Type of the access callback function
// instr, bare_instr, address, value, size
using AccessCallbackTy = void (*)(bool, uint32_t, uint32_t, uint64_t, uint64_t, uint8_t);

// Callback functions
void slamp_access_callback_constant_value(bool, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size);
void slamp_access_callback_constant_address(bool, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size);
void slamp_access_callback_linear_value(bool isLoad, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size);
void slamp_access_callback_linear_address(bool isLoad, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size);

// FIXME: implement the callback
void slamp_global_callback(const char* name, uint64_t addr, uint64_t size) {}

// TODO: activate shadow memory (SLAMP_malloc)
void SLAMP_callback_stack_alloca(uint64_t array_size, uint64_t type_size, uint32_t instr, uint64_t addr) {
  TURN_OFF_CUSTOM_MALLOC;
  uint64_t size = array_size*type_size;

  if (DEPENDENCE_MODULE || POINTS_TO_MODULE) {
    // Get pre-allocated shadow memory
    void* shadow = (void*)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
    if (shadow && POINTS_TO_MODULE) {
      TS *s = (TS *)shadow;
      auto hash = contextManager->encodeActiveContext();
      TS ts = CREATE_TS(instr, hash, __slamp_invocation);
      for (auto i = 0; i < size; i++)
        s[i] = ts;
    }
  }

  TURN_ON_CUSTOM_MALLOC;
  return;
}


void SLAMP_callback_stack_free(void) {
  // do not need to free memory or shadow memory, it gets reused implicitly

  // // Need to record short-lived objects
  // // need to check if the object is short-lived
  // if (POINTS_TO_MODULE) {
  //   // if we are still in the loop and the iteration is the same, mark it as local
  //   // otherwise mark it as not local
  //   TS* s = (TS*)GET_SHADOW(ptr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  //   auto instr = GET_INSTR(s[0]);
  //   auto iteration  = GET_ITER(s[0]);
  //   auto invocation = GET_INVOC(s[0]);

  //   // if invokedepth is 0, it means we are not in a loop
  //   if (iteration == (0xffffffffff & __slamp_iteration) 
  //       && invocation == (0xf & __slamp_invocation)
  //       && invokedepth > 0) {
  //     // is short-lived, put in the set
  //     shortLivedObjects.insert(instr);
  //   }
  //   else {
  //     // is not short-lived
  //     longLivedObjects.insert(instr);
  //   }

  // }
}

// Callback function pointers
std::list<AccessCallbackTy> *access_callbacks;
  // &slamp_access_callback_constant_addr,
  // &slamp_access_callback_linear_address,
  // &slamp_access_callback_constant_value,
  // &slamp_access_callback_linear_value,
  // &slamp_access_callback_reason,
  // &slamp_access_callback_trace
// };

struct PairHash
{
    std::size_t operator () (std::pair<uint32_t, uint32_t> const &v) const
    {
      static_assert(sizeof(size_t) == sizeof(uint64_t), "Should be 64bit address");
      // std::hash<uint32_t> hash_fn;
      // need to make sure if the pair are the same, this doesn't generate 0
      return ((uint64_t)v.first << 32) | v.second;
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

static std::unordered_map<AccessKey, Constant *, PairHash> *constmap_value;
static std::unordered_map<AccessKey, Constant *, PairHash> *constmap_addr;
static std::unordered_map<AccessKey, LinearPredictor *, PairHash > *lpmap_value;
static std::unordered_map<AccessKey, LinearPredictor *, PairHash > *lpmap_addr;

void slamp_access_callback_constant_value(bool isLoad, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  if (!isLoad) {
    return;
  }

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
    auto cp = new Constant(true, true, size, addr, value);
    constmap_value->insert(std::make_pair(key, cp));
  }
}

void slamp_access_callback_constant_address(bool isLoad, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  if (!isLoad) {
    return;
  }

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


void slamp_access_callback_linear_value(bool isLoad, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  if (!isLoad) {
    return;
  }
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

void slamp_access_callback_linear_address(bool isLoad, uint32_t instr, uint32_t bare_instr, uint64_t addr, uint64_t value, uint8_t size) {
  if (!isLoad) {
    return;
  }
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

static uint32_t          StaticInstIdOfLOI = 0;
static uint32_t          ext_context = 0;
slamp::MemoryMap* smmap = nullptr;

struct InstructionRecord {
  uint64_t last_addr;
  uint64_t last_iter;
  static const uint64_t INVALID = UINT64_MAX;
};

std::unordered_map<uint32_t, InstructionRecord*> instructionMap;

void updateInstruction(uint32_t instr, uint64_t addr) {
  if (instructionMap.find(instr) != instructionMap.end()) {
    auto &record = instructionMap[instr];
    record->last_addr = addr;
    record->last_iter = __slamp_iteration;
  } else {
    auto &record = instructionMap[instr];
    record = new InstructionRecord{addr, __slamp_iteration};
  }
}

// Allocation Unit: the instruction, the invocation and the iteration
using SlampAllocationUnit = TS;

// map from load/store instruction to the allocation unit
std::unordered_map<uint64_t, std::unordered_set<SlampAllocationUnit>> *pointsToMap;
// std::unordered_map<uint32_t, std::unordered_set<SlampAllocationUnit>> *pointsToMap;

template <unsigned size>
void SLAMP_points_to_module_use(uint32_t instr, uint64_t addr) {
  TURN_OFF_CUSTOM_MALLOC;

  auto contextHash = contextManager->encodeActiveContext();
  uint64_t instrAndHash = ((uint64_t)instr << 32) | contextHash;
  if (addr == 0) {
    (*pointsToMap)[instrAndHash].insert(0);
    return;
  }
  TS* s = (TS*)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS tss[8]; // HACK: avoid using malloc
  for (auto i = 0; i < size; i++) {
    tss[i] = s[i];
  }

  for (auto i = 0; i < size; i++) {
    bool cond = true;
    for (auto j = 0; j < i; j++) {
      cond = cond && (tss[i] != tss[j]);
    }

    if (cond && tss[i] != 0) {
      // mask off the iteration count
      TS ts = tss[i];
      ts = ts & 0xffffffffffffff00;
      // ts = ts & 0xfffffffffffffff0;
      // ts = ts & 0xfffff0000000000f;
      //create set of objects for each load/store
      (*pointsToMap)[instrAndHash].insert(ts);
    }
  }
  TURN_ON_CUSTOM_MALLOC;
}

void SLAMP_points_to_module_use(uint32_t instr, uint64_t addr, unsigned size) {
  TURN_OFF_CUSTOM_MALLOC;

  auto contextHash = contextManager->encodeActiveContext();
  uint64_t instrAndHash = ((uint64_t)instr << 32) | contextHash;
  if (addr == 0) {
      (*pointsToMap)[instrAndHash].insert(0);
      return;
  }
  std::unordered_set<TS> m;
  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);

  // FIXME: ASSUME_ONE_ADDR should be considered here, however, memcpy and stuff might rely on this
  bool noDep = true;
  for (unsigned i = 0; i < size; i++) {

    TS ts;
    ts = s[i];

    // mask off the iteration count
      // ts = ts & 0xfffff00000000000;
      ts = ts & 0xfffffffffffffe00;
    // ts = ts & 0xfffffffffffffff0;
    // ts = ts & 0xfffff0000000000f;
    if (m.count(ts) == 0) {
      (*pointsToMap)[instrAndHash].insert(ts);
      m.insert(ts);
    }
  }
  TURN_ON_CUSTOM_MALLOC;
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

static std::unordered_map<DepPair, ReasonCounter, PairHash> *depReasonMap;
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
static void accessModuleDump(std::string jname) {
  using json = nlohmann::json;
  std::ofstream jfile(jname, std::ios::app);
  //std::ofstream of(fname, std::ios::app);

  json outfile;

  //auto printCp = [&of](AccessKey key, Constant *cp) {
  //  of << "(" << key.first << ":" << key.second << ")" << " ["
  //    << cp->valid << " " << (unsigned)(cp->valid ? cp->size : 0) << " " << (cp->valid ? cp->value: 0) << "]";
  //};

  //auto printLp = [&of](AccessKey key, LinearPredictor *lp) {
  //  bool lp_int_valid = (lp->stable && lp->valid_as_int);
  //  bool lp_double_valid = (lp->stable && lp->valid_as_double);
  //  of << "(" << key.first << ":" << key.second << ")" <<  " ["
  //    << lp_int_valid << " " << (lp_int_valid ? lp->ia : 0) << " "
  //    << (lp_int_valid ? lp->ib : 0) << " " << lp_double_valid << " "
  //    << (lp_double_valid ? lp->da : 0) << " "
  //    << (lp_double_valid ? lp->db : 0) << "]";
  //};

  if (CONSTANT_VALUE_MODULE) {
    json constval;
    // dump constant_value map
    //of << "constant_value_map:\n";
    for (auto &[key, cp] : *constmap_value) {
      if (cp->valid) {
        constval["inst"] = key.first;
        constval["bare inst"] = key.second;
        constval["valid"] = cp->valid;
        constval["size"] = cp->size;;
        constval["value"] = cp->value;

        //printCp(key, cp);
        //of << "\n";
      }
    }
    outfile["constVal"] = constval;
  }
  if (CONSTANT_ADDRESS_MODULE) {
    json constaddr;
    // dump constant_address map
    //of << "constant_address_map:\n";
    for (auto &[key, cp] : *constmap_addr) {
      if (cp->valid) {
        constaddr["inst"] = key.first;
        constaddr["bare inst"] = key.second;
        constaddr["valid"] = cp->valid;
        constaddr["size"] = cp->size;;
        constaddr["value"] = cp->value;

        //printCp(key, cp);
        //of << "\n";
      }
    }
    outfile["constAddr"] = constaddr;
  }

  if (LINEAR_VALUE_MODULE) {
    json linval;
    // dump linear_value map
    //of << "linear_value_map:\n";
    for (auto &[key, lp] : *lpmap_value) {
      if (lp->valid_as_int || lp->valid_as_double) {
        bool lp_int_valid = lp->stable && lp->valid_as_int;
        bool lp_double_valid = lp->stable && lp->valid_as_double;
        linval["inst"] = key.first;
        linval["bare inst"] = key.second;
        linval["validInt"] = lp_int_valid;
        linval["aInt"] = lp_int_valid ? lp->ia : 0;
        linval["bInt"] = lp_int_valid ? lp->ib : 0;
        linval["validDouble"] = lp_double_valid;
        linval["aDouble"] = lp_double_valid ? lp->da : 0;
        linval["bDouble"] = lp_double_valid ? lp->db : 0;

        //printLp(key, lp);
        //of << "\n";
      }
    }
    outfile["linVal"] = linval;
  }

  if (LINEAR_ADDRESS_MODULE) {
    json linaddr;
    // dump linear_address map
    //of << "linear_address_map:\n";
    for (auto &[key, lp] : *lpmap_addr) {
      if (lp->valid_as_int || lp->valid_as_double) {
        bool lp_int_valid = lp->stable && lp->valid_as_int;
        bool lp_double_valid = lp->stable && lp->valid_as_double;
        linaddr["inst"] = key.first;
        linaddr["bare inst"] = key.second;
        linaddr["validInt"] = lp_int_valid;
        linaddr["aInt"] = lp_int_valid ? lp->ia : 0;
        linaddr["bInt"] = lp_int_valid ? lp->ib : 0;
        linaddr["validDouble"] = lp_double_valid;
        linaddr["aDouble"] = lp_double_valid ? lp->da : 0;
        linaddr["bDouble"] = lp_double_valid ? lp->db : 0;

        //printLp(key, lp);
        //of << "\n";
      }
    }
    outfile["linAddr"] = linaddr;
  }
  jfile << outfile.dump(4);
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


static void* SLAMP_malloc_hook(size_t size, const void * /*caller*/) {
  auto ptr = SLAMP_malloc(size, ext_context, 16);

  __slamp_malloc_count++;
  return ptr;
}

static void SLAMP_free_hook(void *ptr, const void * /*caller*/) {
  SLAMP_free(ptr);
  __slamp_free_count++;
}

static void* SLAMP_memalign_hook(size_t alignment, size_t size, const void *caller) {
  auto ptr = SLAMP_malloc(size, ext_context, alignment);
  __slamp_malloc_count++;
  return ptr;
}


void SLAMP_init(uint32_t fn_id, uint32_t loop_id)
{

  if (POINTS_TO_MODULE) {
    contextManager = new SpecPrivLib::SpecPrivContextManager();
    shortLivedObjects = new std::unordered_set<uint64_t>();
    longLivedObjects = new std::unordered_set<uint64_t>();
    targetLoopContexts = new std::unordered_set<uint64_t>();
  }
  // auto heapStart = sbrk(0);
  // fprintf(stderr, "heap start: %lx\n", (unsigned long)heapStart);

  constmap_value = new std::unordered_map<AccessKey, Constant *, PairHash>();
  constmap_addr = new std::unordered_map<AccessKey, Constant *, PairHash>();
  lpmap_value  = new std::unordered_map<AccessKey, LinearPredictor *, PairHash>();
  lpmap_addr = new std::unordered_map<AccessKey, LinearPredictor *, PairHash>();

  // per instruction map


  auto setModule = [](bool &var, const char *name, bool setV=true) {
    auto *mod = getenv(name);
    if (mod && strcmp(mod, "1") == 0) {
      var = setV;
    } else {
      var = !setV;
    }
  };

  auto setLocalWriteValue = [](size_t &var, const char *name) {
    auto *env = getenv(name);
    if (env) {
      // from hex to size_t
      var = strtoul(env, NULL, 16);
    }
  };

  // check if the modules are turned on in the environment variable
  setModule(DISTANCE_MODULE, "DISTANCE_MODULE");

#ifndef ITO_ENABLE
  setModule(CONSTANT_ADDRESS_MODULE, "CONSTANT_ADDRESS_MODULE");
  setModule(CONSTANT_VALUE_MODULE, "CONSTANT_VALUE_MODULE");
  setModule(LINEAR_ADDRESS_MODULE, "LINEAR_ADDRESS_MODULE");
  setModule(LINEAR_VALUE_MODULE, "LINEAR_VALUE_MODULE");
  setModule(REASON_MODULE, "REASON_MODULE");
  setModule(TRACE_MODULE, "TRACE_MODULE");
  // setModule(LOCALWRITE_MODULE, "LOCALWRITE_MODULE");

  setModule(DEPENDENCE_MODULE, "NO_DEPENDENCE_MODULE", false);
  setModule(POINTS_TO_MODULE, "POINTS_TO_MODULE");
#endif


  // dependence module and points to module cannot be turned on at the same time
  if (DEPENDENCE_MODULE && POINTS_TO_MODULE) {
    fprintf(stderr, "Dependence module and points-to module cannot be turned on at the same time\n");
    exit(1);
  }

  // initialize pointsToMap
  pointsToMap = new std::unordered_map<uint64_t, std::unordered_set<SlampAllocationUnit>>();

// #ifndef ITO_ENABLE
// // FIXME: LOCALWRITE stuff should also be converted to constant if possible
  // setLocalWriteValue(LOCALWRITE_MASK, "LOCALWRITE_MASK");
  // setLocalWriteValue(LOCALWRITE_PATTERN, "LOCALWRITE_PATTERN");
// #endif

  // print localwrite mask and pattern
  fprintf(stderr, "LOCALWRITE_MASK: %zx\n", LOCALWRITE_MASK);
  fprintf(stderr, "LOCALWRITE_PATTERN: %zx\n", LOCALWRITE_PATTERN);

  access_callbacks = new std::list<AccessCallbackTy> ();

  if (CONSTANT_VALUE_MODULE) {
    access_callbacks->push_back(&slamp_access_callback_constant_value);
  }

  if (CONSTANT_ADDRESS_MODULE) {
    access_callbacks->push_back(&slamp_access_callback_constant_address);
  }

  if (LINEAR_VALUE_MODULE) {
    access_callbacks->push_back(&slamp_access_callback_linear_value);
  }

  if (LINEAR_ADDRESS_MODULE) {
    access_callbacks->push_back(&slamp_access_callback_linear_address);
  }

  if (REASON_MODULE) {
    auto *store = getenv("STORE_INST");
    if (store) {
      STORE_INST = atoi(store);
    }
    depReasonMap = new std::unordered_map<DepPair, ReasonCounter, PairHash>();
  }

  uint64_t START;
  TIME(START);
  // initializing customized malloc should be done very first

  slamp::init_bound_malloc((void*)(HEAP_BOUND_LOWER));

  smmap = new slamp::MemoryMap(TIMESTAMP_SIZE_IN_BYTES);
  // smmap->init_heap(heapStart);

  smmap->init_stack(SIZE_8M);

  slamp::init_logger(fn_id, loop_id);
  TADD(overhead_init_fini, START);


  TIME(START);
  // allocate shadow for linux standard base, etc

  smmap->allocate((void*)&errno, sizeof(errno));
  smmap->allocate((void*)&stdin, sizeof(stdin));
  smmap->allocate((void*)&stdout, sizeof(stdout));
  smmap->allocate((void*)&stderr, sizeof(stderr));
  smmap->allocate((void*)&sys_nerr, sizeof(sys_nerr));
  if (POINTS_TO_MODULE) {
    auto size = sizeof(*stdout);
    auto shadow = (TS*)smmap->allocate((void*)stdout, size);
    for (auto i = 0; i < size; i+= size) {
      TS ts = ~(TS)0;
      shadow[i] = ts;
    }
  }

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

  std::string pt_fname = "points_to.profile";
  // append the filename with localwrite pattern
  if (LOCALWRITE_MODULE) {
    // convert the LOCALWRITE_PATTERN into a string
    std::stringstream ss;
    ss << filename << "_" << LOCALWRITE_PATTERN;
    filename = ss.str().c_str();

    std::stringstream ss2;
    ss2 << pt_fname << "_" << LOCALWRITE_PATTERN;
    pt_fname = ss2.str().c_str();
  }

  if (POINTS_TO_MODULE) {
    // dump out the points-to map
    std::ofstream ofs(pt_fname);
    if (ofs.is_open()) {
      ofs << "Points-to map\n";
      for (auto &it : *pointsToMap) {
        ofs << it.first << ": "; // instruction ID
        for (auto &it2 : it.second) { // the set of allocation units
          ofs << "instr - "<< GET_INSTR(it2) << " iter - " << GET_ITER(it2) << " invoc - " << GET_INVOC(it2) << "\n";
        }
        ofs << "\n";
      }
      ofs << "Short-lived object:\n";
      ofs << shortLivedObjects->size() << " " << longLivedObjects->size() << "\n";
      for (auto &obj: *shortLivedObjects) {
        // if short-lived
        ofs << obj << "\n";
      }

      for (auto &obj: *longLivedObjects) {
        // if long-lived
        ofs << obj << "\n";
      }

      ofs.close();
    }
  }

  // Dump compatible one as SpecPriv output
  std::ofstream specprivfs("specpriv-profile.out");
  specprivfs << "BEGIN SPEC PRIV PROFILE\n";
  specprivfs << "COMPLETE ALLOCATION INFO ; \n";

  auto printContext = [&specprivfs](const std::vector<SpecPrivLib::ContextId> &ctx) {
    for (auto &c : ctx) {
      specprivfs << "(" << c.type << "," << c.metaId << ")";
    }
  };

  if (POINTS_TO_MODULE) {
    // print all loop contexts
    specprivfs << "LOOP CONTEXTS: " << targetLoopContexts->size() << "\n";
    for (auto contextHash : *targetLoopContexts) {
      auto context = contextManager->decodeContext(contextHash);
      printContext(context);
      specprivfs << "\n";
    }

    // local objects
    for (auto &obj: *shortLivedObjects) {
      // LOCAL OBJECT AU HEAP main if.else.i call.i4.i FROM  CONTEXT { LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  IS LOCAL TO  CONTEXT { LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  COUNT 300 ;
      auto instr = GET_INSTR(obj);
      auto hash = GET_HASH(obj);
      auto context = contextManager->decodeContext(hash);
      specprivfs << "LOCAL OBJECT " << instr << " at context ";
      printContext(context);
      specprivfs << ";\n";

    }

  }

  // predict int and predict ptr (only NULL)
  // Note that the int prediction is in place, however SpecPriv rematerializes the load to the preheader of the loop

  // PRED INT main enqueue.exit23.i $0 AT  CONTEXT { LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  AS PREDICTABLE 301 SAMPLES OVER 1 VALUES {  ( INT 0 COUNT 301 )  } ;
  // PRED PTR main if.then29.i $17 AT  CONTEXT { LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  AS PREDICTABLE 301 SAMPLES OVER 1 VALUES {  ( OFFSET 0 BASE AU NULL  COUNT 301 )  } ;

  if (CONSTANT_VALUE_MODULE) {
    for (auto &[key, cp] : *constmap_value) {
      if (cp->valid) {
        // instr and value
        auto instr = key.first;
        auto value = cp->value;
        // later, we need to parse the instruction to see if it's a pointer or a regular integer
        specprivfs << "PRED VAL " << instr << " " << value << " ; \n";
      }
    }
  }
  // predict OBJ
  //  PRED OBJ main if.else.i $0 AT  CONTEXT { LOOP main for.cond15 1 WITHIN FUNCTION main WITHIN TOP }  AS PREDICTABLE 300 SAMPLES OVER 1 VALUES {  ( OFFSET 0 BASE AU HEAP allocate_matrices for.end call7 FROM  CONTEXT { FUNCTION allocate_matrices WITHIN FUNCTION main WITHIN TOP }  COUNT 300 )  } ;
  if (POINTS_TO_MODULE) {
    for (auto &it : *pointsToMap) {
      auto instr = it.first >> 32;
      auto instrHash = it.first & 0xFFFFFFFF;
      std::vector<SpecPrivLib::ContextId> instrContext = contextManager->decodeContext(instrHash);
      specprivfs << "PRED OBJ " << instr << " at ";
      printContext(instrContext);
      specprivfs << ": " << it.second.size() << "\n"; // instruction ID
      for (auto &it2 : it.second) { // the set of allocation units
        auto hash = GET_HASH(it2);

        specprivfs << "AU "; 
        if (it2 == 0xffffffffffffff00) {
          specprivfs << " UNMANAGED";
        }
        else if (it2 == 0) {
          specprivfs << " NULL";
        } else {
          std::vector<SpecPrivLib::ContextId> context = contextManager->decodeContext(hash);

          specprivfs <<GET_INSTR(it2);
          specprivfs << " FROM CONTEXT " ;
          printContext(context);
        }
        specprivfs << ";\n";
      }
    }
  }


  specprivfs << " END SPEC PRIV PROFILE\n";


  if (DEPENDENCE_MODULE) {
    slamp::fini_logger(filename);
    // delete smmap;
  }

  // slamp::fini_bound_malloc();
  TADD(overhead_init_fini, START);
  slamp_time_dump("slamp_overhead.dump");

  // create str= "slamp_access_module_" + LOCALWRITE_PATTERN + ".dump"
  std::string fname = "slamp_access_module.json";

  if (LOCALWRITE_MODULE) {
    std::stringstream ss;
    ss << "slamp_access_module_" << LOCALWRITE_PATTERN << ".json";
    fname = ss.str();
  } 

  accessModuleDump(fname);

  // dump 
  dumpReason();
}

void SLAMP_allocated(uint64_t addr)
{
  std::cout << std::hex << " allocated? " << smmap->is_allocated((void*)addr) << std::dec << std::endl;
}

/// Allocate the shadow memory on the "heap" for global variables
void SLAMP_init_global_vars(const char *name, uint64_t addr, size_t size)
{
  // report a global 
  slamp_global_callback(name, addr, size);
  smmap->allocate((void*)addr, size);
}

void SLAMP_main_entry(uint32_t argc, char** argv, char** env)
{
#if DEBUG
  __slamp_begin_trace = 1;

  std::cout << "[main_entry] begin: " << std::hex << begin << std::dec << std::endl;
#endif


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
  old_memalign_hook = __memalign_hook;

  TURN_ON_CUSTOM_MALLOC;
}

/// Keep track of the context of the function
void SLAMP_enter_fcn(uint32_t fcnId) {
  if (POINTS_TO_MODULE) {
    TURN_OFF_CUSTOM_MALLOC;

    // std::cerr << "entering function " << fcnId << std::endl;
    auto contextId = SpecPrivLib::ContextId(SpecPrivLib::FunctionContext, fcnId);

    // update the current context
    contextManager->updateContext(contextId);
    TURN_ON_CUSTOM_MALLOC;
  }
}

void SLAMP_enter_loop(uint32_t bbId) {
  if (POINTS_TO_MODULE) {
    TURN_OFF_CUSTOM_MALLOC;

    // std::cerr << "entering function " << fcnId << std::endl;
    auto contextId = SpecPrivLib::ContextId(SpecPrivLib::LoopContext, bbId);

    // update the current context
    contextManager->updateContext(contextId);
    TURN_ON_CUSTOM_MALLOC;
  }
}

void SLAMP_exit_loop(uint32_t bbId) {
  if (POINTS_TO_MODULE) {
    TURN_OFF_CUSTOM_MALLOC;
    auto contextId = SpecPrivLib::ContextId(SpecPrivLib::LoopContext, bbId);

    // std::cerr << "exiting function " << fcnId << std::endl;
    // update the current context
    contextManager->popContext(contextId);
    TURN_ON_CUSTOM_MALLOC;
  }
}

/// Keep track of the context of the function
void SLAMP_exit_fcn(uint32_t fcnId) {
  if (POINTS_TO_MODULE) {
    TURN_OFF_CUSTOM_MALLOC;
    auto contextId = SpecPrivLib::ContextId(SpecPrivLib::FunctionContext, fcnId);

    // std::cerr << "exiting function " << fcnId << std::endl;
    // update the current context
    contextManager->popContext(contextId);
    TURN_ON_CUSTOM_MALLOC;
  }
}

void SLAMP_loop_iter_ctx(uint32_t id) {}

/// update the invocation count
void SLAMP_loop_invocation() {
  TURN_OFF_CUSTOM_MALLOC;
  // fprintf(stderr, "SLAMP_loop_invocation, depth: %u\n", invokedepth);
  invokedepth++;

  if (POINTS_TO_MODULE)
    targetLoopContexts->insert(contextManager->encodeActiveContext());

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
  
  TURN_ON_CUSTOM_MALLOC;
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

// FIXME: a temporary patch for out of handling program original heap
bool SLAMP_isBadAlloc(uint64_t addr) ATTRIBUTE(always_inline) {
  const uint64_t  lower = 0x100000000L;
  const uint64_t higher =  0x010000000000L;
  const uint64_t heapStart = smmap->heapStart;

  if (addr < lower && addr > heapStart) {
    return true;
  }

  return false;
}

void SLAMP_report_base_pointer_arg(uint32_t fcnId, uint32_t argId, void *ptr){
  if (invokedepth == 0)
    return;
  if (SLAMP_isBadAlloc((uint64_t)ptr)) {
    return;
  }
  SLAMP_points_to_module_use<1>(FORMAT_INST_ARG(fcnId, argId), (uint64_t)ptr);
}

void SLAMP_report_base_pointer_inst(uint32_t instId, void *ptr){
  if (invokedepth == 0)
    return;
  if (SLAMP_isBadAlloc((uint64_t)ptr)) {
    return;
  }
  SLAMP_points_to_module_use<1>(FORMAT_INST_INST(instId), (uint64_t)ptr);
}

/// set the context of the call inside a loop
void SLAMP_ext_push(const uint32_t instr) ATTRIBUTE(always_inline) {
  assert(ext_context == 0);
  ext_context = instr;
}

/// unset the context of the call inside a loop
void SLAMP_ext_pop() ATTRIBUTE(always_inline) {
  ext_context = 0;
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

  assert(StaticInstIdOfLOI == 0);
  StaticInstIdOfLOI = instr;
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

  StaticInstIdOfLOI = 0;
}

template <unsigned size>
void SLAMP_dependence_module_load_log(const uint32_t instr, const uint32_t bare_instr, const uint64_t value, const uint64_t addr) ATTRIBUTE(noinline) {
  uint64_t START;
  TIME(START);

  TS* s = (TS*)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS tss[8]; // HACK: avoid using malloc

  if (ASSUME_ONE_ADDR) {
    tss[0] = s[0];
  } else {
    for (auto i = 0; i < size; i++) {
      tss[i] = s[i];
    }
  }

  TADD(overhead_shadow_read, START);

  TIME(START);

  // assume loads and stores are always one unit;
  if (ASSUME_ONE_ADDR) {
    uint32_t src_inst =
      slamp::log(tss[0], instr, s, bare_instr, addr, value, size);
  } else {
    for (auto i = 0; i < size; i++) {
      bool cond = true;
      for (auto j = 0; j < i; j++) {
        cond = cond && (tss[i] != tss[j]);
      }

      if (cond && tss[i] != 0) {
        uint32_t src_inst =
            slamp::log(tss[i], instr, s, bare_instr, addr, value, size);
        // // FIXME: no dependence, not consider other branches
        // if (src_inst != STORE_INST) {
        //   updateReasonMap(instr, bare_instr, addr, value, size);
        // }
      }
    }
  }

  TADD(overhead_log_total, START);
}

// FIXME: duplication with SLAMP_dependence_module_load_log template
void SLAMP_dependence_module_load_log(const uint32_t instr, const uint32_t bare_instr, const uint64_t value, const uint64_t addr, unsigned size) ATTRIBUTE(noinline) {
  uint64_t START;

  // FIXME: beware of the malloc hook being changed at this point, any
  // allocation is super costly
  std::unordered_set<TS> m;
  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);

  // FIXME: ASSUME_ONE_ADDR should be considered here, however, memcpy and stuff might rely on this
  bool noDep = true;
  for (unsigned i = 0; i < size; i++) {
    TIME(START);

    TS ts;
    ts = s[i];
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

  /*
   * if (noDep) {
   *   updateReasonMap(instr, bare_instr, addr, 0, size);
   * }
   */
}

template <unsigned size>
void SLAMP_load(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value) ATTRIBUTE(always_inline) {
  if (invokedepth > 1)
    instr = StaticInstIdOfLOI;
  if (SLAMP_isBadAlloc(addr))
    return;

  if (TRACE_MODULE) {
    __slamp_load_count++;
  }
#if DEBUG
  if (__slamp_begin_trace) std::cout << "    load"<< size << " " << instr << "," << bare_instr << " iteration " << __slamp_iteration << " addr " << std::hex << addr << " value " << value << std::dec << "\n" << std::flush;
  if (!smmap->is_allocated(reinterpret_cast<void*>(addr))) {
    std::cout << "Error: shadow memory not allocated" << std::flush;
    assert(false);
  }
#endif

  // only need to check once
  if (LOCALWRITE(addr)) {
#ifndef ITO_ENABLE
    TURN_OFF_CUSTOM_MALLOC;
#endif
    for (auto *f : *access_callbacks) {
      f(true, instr, bare_instr, addr, value, size);
    }

    if (DEPENDENCE_MODULE) {
      SLAMP_dependence_module_load_log<size>(instr, bare_instr, value, addr);
    }

    // if (POINTS_TO_MODULE) {
    //   SLAMP_points_to_module_use<size>(instr, addr);
    // }
#ifndef ITO_ENABLE
    TURN_ON_CUSTOM_MALLOC;
#endif
  }

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
    instr = StaticInstIdOfLOI;

  if (TRACE_MODULE) {
    __slamp_load_count++;
  }
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

  // only need to check once
  if (LOCALWRITE(addr)) {
    TURN_OFF_CUSTOM_MALLOC;
    if (DEPENDENCE_MODULE) {
      SLAMP_dependence_module_load_log(instr, bare_instr, 0, addr, n);
    }

    // if (POINTS_TO_MODULE) {
    //   // only need to check once
    //   SLAMP_points_to_module_use(instr, addr, n);
    // }

    TURN_ON_CUSTOM_MALLOC;
  }
}

template <unsigned size>
void SLAMP_load_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value) ATTRIBUTE(always_inline) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    load" << size << "_ext " << context << "," << bare_instr
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif
  if (StaticInstIdOfLOI)
    SLAMP_load<size>(StaticInstIdOfLOI, addr, bare_instr, value);
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

  if (StaticInstIdOfLOI)
    SLAMP_loadn(StaticInstIdOfLOI, addr, bare_instr, n);
}

template <unsigned size>
void SLAMP_dependence_module_store_log(const uint32_t instr, const uint64_t addr) ATTRIBUTE(noinline){
  uint64_t START;
  TIME(START);

  TS *s = (TS *)GET_SHADOW(addr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
  TS ts = CREATE_TS(instr, __slamp_iteration, __slamp_invocation);

  // TODO: handle output dependence. ignore it as of now.
  if (ASSUME_ONE_ADDR) {
    s[0] = ts;
  } else {
    for (auto i = 0; i < size; i++)
      s[i] = ts;
  }

  TADD(overhead_shadow_write, START);
  slamp::capturestorecallstack(s);
}

// FIXME: duplication with SLAMP_dependence_module_store_log template
void SLAMP_dependence_module_store_log(const uint32_t instr, const uint64_t addr, unsigned size) ATTRIBUTE(noinline){
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

template <unsigned size>
void SLAMP_store(uint32_t instr, uint32_t bare_instr, const uint64_t addr) ATTRIBUTE(always_inline) {
  if (SLAMP_isBadAlloc(addr))
    return;

  // TODO: do we care about recursive calls?
  if (invokedepth > 1)
    instr = StaticInstIdOfLOI;
  
  if (TRACE_MODULE) {
    __slamp_store_count++;
  }

  if (REASON_MODULE)
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

  // Store access
  // only need to check once
  if (LOCALWRITE(addr)) {
#ifndef ITO_ENABLE
  TURN_OFF_CUSTOM_MALLOC;
#endif
    for (auto *f : *access_callbacks) {
      // FIXME: value is empty for now
      f(false, instr, bare_instr, addr, 0, size);
    }

    if (DEPENDENCE_MODULE) {
      SLAMP_dependence_module_store_log<size>(instr, addr);
    }

    // if (POINTS_TO_MODULE) {
    //   SLAMP_points_to_module_use<size>(instr, addr);
    // }
#ifndef ITO_ENABLE
  TURN_ON_CUSTOM_MALLOC;
#endif
  }
}

void SLAMP_store1(uint32_t instr, const uint64_t addr) {
  SLAMP_store<1>(instr, instr, addr);
}

void SLAMP_store2(uint32_t instr, const uint64_t addr) {
  SLAMP_store<2>(instr, instr, addr);
}

void SLAMP_store4(uint32_t instr, const uint64_t addr) {
  SLAMP_store<4>(instr, instr, addr);
}

void SLAMP_store8(uint32_t instr, const uint64_t addr) {
  SLAMP_store<8>(instr, instr, addr);
}

void SLAMP_storen(uint32_t instr, const uint64_t addr, size_t n) {
  if (SLAMP_isBadAlloc(addr))
    return;
  if (invokedepth > 1)
    instr = StaticInstIdOfLOI;

  if (TRACE_MODULE) {
    __slamp_store_count++;
  }

  if (REASON_MODULE)
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

  if (LOCALWRITE(addr)) {
    TURN_OFF_CUSTOM_MALLOC;
    if (DEPENDENCE_MODULE) {
      // only need to check once
      SLAMP_dependence_module_store_log(instr, addr, n);
    }

    // if (POINTS_TO_MODULE) {
    //   // only need to check once
    //   SLAMP_points_to_module_use(instr, addr, n);
    // }
    TURN_ON_CUSTOM_MALLOC;
  }
}

template <unsigned size>
void SLAMP_store_ext(const uint64_t addr, const uint32_t bare_inst) ATTRIBUTE(always_inline) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    store" << size << "_ext " << context << "," << bare_inst
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif

  if (StaticInstIdOfLOI)
    SLAMP_store<size>(StaticInstIdOfLOI, bare_inst, addr);
}


void SLAMP_store1_ext(const uint64_t addr, const uint32_t bare_inst) {
  SLAMP_store_ext<1>(addr, bare_inst);
}

void SLAMP_store2_ext(const uint64_t addr, const uint32_t bare_inst) {
  SLAMP_store_ext<2>(addr, bare_inst);
}

void SLAMP_store4_ext(const uint64_t addr, const uint32_t bare_inst) {
  SLAMP_store_ext<4>(addr, bare_inst);
}

void SLAMP_store8_ext(const uint64_t addr, const uint32_t bare_inst) {
  SLAMP_store_ext<8>(addr, bare_inst);
}

void SLAMP_storen_ext(const uint64_t addr, const uint32_t bare_inst, size_t n) {
#if DEBUG
  if (__slamp_begin_trace)
    std::cout << "    storen_ext " << context << "," << bare_inst
              << " iteration " << __slamp_iteration << " addr " << std::hex
              << addr << std::dec << "\n"
              << std::flush;
#endif

  if (StaticInstIdOfLOI)
    SLAMP_storen(StaticInstIdOfLOI, addr, n);
}

/*
 * External library wrappers
 */

void* SLAMP_malloc(size_t size, uint32_t instr, size_t alignment)
{
  TURN_OFF_CUSTOM_MALLOC;

  uint64_t START;
  TIME(START);
  //fprintf(stderr, "SLAMP_malloc, size: %lu\n", size);
  void* result = (void*)slamp::bound_malloc(size, alignment);
  unsigned count = 0;

  while( true )
  {
    if ( !result ) {

      TADD(overhead_shadow_allocate, START);
      TURN_ON_CUSTOM_MALLOC;
      return nullptr;
    }

    // if dependence modules is turned on
    if (DEPENDENCE_MODULE || POINTS_TO_MODULE) {
      //create shadoow mem space
      void* shadow = smmap->allocate(result, size);
      if (shadow)
      {
        //std::cout << "SLAMP_malloc result is " << std::hex << result << "\n";
        TADD(overhead_shadow_allocate, START);

        if (POINTS_TO_MODULE) {
          // initialize all shadow memory to context
          // cast as timestamp
          TS *s = (TS *)shadow;
          // log all data into sigle TS
          // FIXME: static instruction and the dynamic context?
          // context: static instr + function + loop
          auto hash = contextManager->encodeActiveContext();
          // print active context
          // contextManager->activeContext->print(std::cerr);

          // currentContext->print(std::cerr);
          // std::cerr << "malloc hash: " << hash << "\n";

          // TS ts = CREATE_TS(instr, hash, __slamp_invocation);
          TS ts = CREATE_TS_HASH(instr, hash, __slamp_iteration, __slamp_iteration);
          // std::cerr << "TS: " << std::hex << ts << std::dec << "\n";
          // TS ts = CREATE_TS(instr, __slamp_iteration, __slamp_invocation);
          if (instr == 0) {
            std::cerr << "Ext context: " << ext_context << "\n";
          }

          //8 bytes per byte TODO: can we reduce this?
          for (auto i = 0; i < size; i++)
            s[i] = ts;
        }
        TURN_ON_CUSTOM_MALLOC;
        return result;
      }
      else
      {
        uint64_t starting_page;
        unsigned purge_cnt;
        slamp::bound_free(result, starting_page, purge_cnt);
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
    else {
      TURN_ON_CUSTOM_MALLOC;
      return result;
    }
  }
}

void  SLAMP_free(void* ptr)
{
  if (ptr == nullptr)
    return;

  if (SLAMP_isBadAlloc((uint64_t)ptr))
    return;

  TURN_OFF_CUSTOM_MALLOC;
  
  uint64_t starting_page;
  unsigned purge_cnt;
  bool purge = slamp::bound_free(ptr, starting_page, purge_cnt);

  // need to check if the object is short-lived
  if (POINTS_TO_MODULE) {
    // if we are still in the loop and the iteration is the same, mark it as local
    // otherwise mark it as not local
    TS* s = (TS*)GET_SHADOW(ptr, TIMESTAMP_SIZE_IN_POWER_OF_TWO);
    auto instr = GET_INSTR(s[0]);
    auto hash = GET_HASH(s[0]);
    auto iteration  = GET_ITER(s[0]) & 0xF;
    auto invocation = GET_INVOC(s[0]);

    auto instrAndHash = s[0] & 0xFFFFFFFFFFFFFF00;
    
    if (instr == 0) {
      std::cerr << "TS: " << s[0] << " ptr: " << ptr << "\n";
    }

    // if invokedepth is 0, it means we are not in a loop
    if (iteration == (0xf & __slamp_iteration) 
        && invocation == (0xf & __slamp_invocation)
        && invokedepth > 0) {
      // is short-lived, put in the set
      shortLivedObjects->insert(instrAndHash);
      for (auto &obj: *shortLivedObjects) {
        std::cerr << "short lived object: " << obj << "\n";
      }
    }
    else {
      // is not short-lived
      longLivedObjects->insert(instrAndHash);
    }

  }

  if (DEPENDENCE_MODULE || POINTS_TO_MODULE) {
    if (purge)
      smmap->deallocate_pages(starting_page, purge_cnt);
  }

  TURN_ON_CUSTOM_MALLOC;

}
