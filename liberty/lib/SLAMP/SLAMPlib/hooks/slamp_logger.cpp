#include "slamp_logger.h"
#define ONLY_SET

#include <bits/stdint-uintn.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>

#include "slamp_debug.h"
#include "slamp_timestamp.h"

#include "slamp_timer.h"

// defined in slamp_hooks.cpp
extern bool DISTANCE_MODULE;
extern bool TRACE_MODULE;

/*
 * #define CREATE_KEY(src, dst, cross) ( ((KEY)(cross) << 40) | ((KEY)(src) <<
 * 20) | (dst) ) #define GET_SRC(key) ( ( (key) >> 20 ) & 0xfffff ) #define
 * GET_DST(key) ( (key) & 0xfffff ) #define GET_CROSS(key) ( ( (key) >> 40 ) &
 * 0x1 )
 *
 * #define CREATE_VALUEPROF_KEY(inst, bare_inst) ( ((uint64_t)(inst)<<32) |
 * (bare_inst) )
 */
extern uint64_t __slamp_iteration;
extern uint64_t __slamp_invocation;
extern uint64_t __slamp_load_count;
extern uint64_t __slamp_store_count;
extern uint64_t __slamp_malloc_count;
extern uint64_t __slamp_free_count;

#if DEBUG
#include <set>
#include <sstream>
#include <string>

extern uint8_t __slamp_begin_trace;
#endif

static uint64_t __slamp_dep_count = 0;

struct TraceRecord {
  uint32_t instrS;
  uint32_t instrL;
  uint64_t invocS;
  uint64_t invocL;
  uint64_t iterS;
  uint64_t iterL;
  uint64_t addr;
  uint64_t value;
  uint32_t size;
};

const uint64_t MAX_TRACE_SIZE = 10000;

static std::vector<TraceRecord> trace;

// (src_inst, dst_inst, bare_inst, src_invoc, __slamp_invocation, src_iter,
// __slamp_iteration, addr, value, size);
using DepCallbackTy = void (*)(uint32_t, uint32_t, uint32_t, uint64_t, uint64_t,
                               uint64_t, uint64_t, uint64_t, uint64_t,
                               uint32_t);

// Callback functions
void slamp_dep_callback_log(uint32_t src_inst, uint32_t dst_inst,
                            uint32_t bare_inst, uint64_t src_invoc,
                            uint64_t dst_invoc, uint64_t src_iter,
                            uint64_t dst_iter, uint64_t addr, uint64_t value,
                            uint32_t size);
void slamp_dep_callback_distance(uint32_t src_inst, uint32_t dst_inst,
                                 uint32_t bare_inst, uint64_t src_invoc,
                                 uint64_t dst_invoc, uint64_t src_iter,
                                 uint64_t dst_iter, uint64_t addr,
                                 uint64_t value, uint32_t size);

// Callback function pointers
DepCallbackTy dep_callbacks[2] = {};

// dump trace stats into a file called trace.txt
static void dumpTrace() {
  if (!TRACE_MODULE) {
    return;
  }

  std::ofstream of("trace.txt", std::ios::app);
  of << __slamp_load_count << " " << __slamp_store_count << " "
     << __slamp_dep_count << " "
     << __slamp_malloc_count << " " << __slamp_free_count << " "
     << __slamp_invocation << " " << __slamp_iteration
     << "\n";

  // of << "instrS, instrL, invocS, invocL, iterS, iterL, addr, value, size\n";
  // auto id = 0;
  // for (auto &it: trace) {
    // of << id++ << "," << it.instrS << "," << it.instrL << "," << it.invocS
       // << "," << it.invocL << "," << it.iterS << "," << it.iterL << ","
       // << it.addr << "," << it.value << "," << it.size << "\n";
  // }

  of.close();
}


static void recordTrace(uint32_t instrS, uint32_t instrL, uint64_t invocS,
                        uint64_t invocL, uint64_t iterS, uint64_t iterL,
                        uint64_t addr, uint64_t value, uint32_t size) {
  if (!TRACE_MODULE) {
    return;
  }

  // FIXME: turned off trace
  // if (trace.size() >= MAX_TRACE_SIZE) {
    // return;
  // }


  // TraceRecord record{instrS, instrL, invocS, invocL, iterS, iterL, addr, value,
                     // size};

  // trace.push_back(record);
}

namespace slamp {
struct DistanceDistribution {
  bool isConstant;
  uint32_t distance;
  std::unordered_map<uint32_t, uint32_t> distribution;
  DistanceDistribution(uint32_t distance): isConstant(true), distance(distance) {}
};


struct Value {
  uint64_t count{0};
  // Constant *c_value{nullptr};
  // Constant *c_addr{nullptr};
  // LinearPredictor *lp_value{nullptr};
  // LinearPredictor *lp_addr{nullptr};
  DistanceDistribution *d; 
  // char pad[64 - sizeof(void *) - sizeof(void *) - sizeof(void *)];
  // char pad[64 - sizeof(uint64_t) - sizeof(void *)];

  // Value() : count(0), c(NULL), lp(NULL) { assert(false); }
  Value() = default;
  // Value(LinearPredictor* lp, LinearPredictor *lp_addr) : lp_addr(lp_addr), lp_value(lp) {}
  // Value(Constant *c, LinearPredictor *lp) : c_value(c), lp_value(lp) {}
  // Value(Constant *c, LinearPredictor *lp, Constant *c_addr, LinearPredictor *lp_addr) : c_value(c), lp_value(lp), c_addr(c_addr), lp_addr(lp_addr) {}
};

#ifdef ONLY_SET
static std::unordered_set<KEY, KEYHash, KEYEqual> *deplog_set;
#else
static std::unordered_map<KEY, Value, KEYHash, KEYEqual> *deplog;
#endif

#if DEBUG
static std::set<std::string> *depset;
#endif

static uint32_t target_fn_id;
static uint32_t target_loop_id;

void init_logger(uint32_t fn_id, uint32_t loop_id) {

#ifdef ONLY_SET
  deplog_set = new std::unordered_set<KEY, KEYHash, KEYEqual>();
#else
  deplog = new std::unordered_map<KEY, Value, KEYHash, KEYEqual>();
#endif
  // constmap = new std::unordered_map<uint32_t, VALUE>();

#if DEBUG
  depset = new std::set<std::string>();
#endif

  target_fn_id = fn_id;
  target_loop_id = loop_id;
}

void fini_logger(const char *filename) {
  print_log(filename);

#if DEBUG
  std::cout << "printed log to the file\n";
  for (std::set<std::string>::iterator si = depset->begin();
       si != depset->end(); si++) {
    std::cout << (*si) << "\n";
  }

  delete depset;
#endif

  /*
   *   std::unordered_map<KEY, Constant *, KEYHash, KEYEqual>::iterator ci
   * = constmap->begin(); for (; ci != constmap->end(); ci++) delete ci->second;
   *
   *   std::unordered_map<KEY, LinearPredictor *, KEYHash,
   * KEYEqual>::iterator li = lpmap->begin(); for (; li != lpmap->end(); li++)
   *     delete li->second;
   *
   *   delete lpmap;
   *   delete constmap;
   */
#ifdef ONLY_SET
  delete deplog_set;
#else
  delete deplog;
#endif
}

uint32_t log(TS ts, const uint32_t dst_inst, TS *pts, const uint32_t bare_inst,
         uint64_t addr, uint64_t value, uint8_t size) {
  // FIXME: should turn off custom malloc here
  // ZY: check invocation counter, if not the same, just return; because don't
  // create new dependence between two invocations
  if (ts) {
    uint64_t src_invoc = GET_INVOC(ts);
    if (src_invoc != __slamp_invocation)
      return UINT32_MAX;
  }

  if (ts) {
    __slamp_dep_count++;
  }

// #if 0
  // update log
  // Found a dependence
  if (ts) {
    uint32_t src_inst = GET_INSTR(ts);
    uint64_t src_iter = GET_ITER(ts);

    uint64_t src_invoc = GET_INVOC(ts);

    // do dep callbacks
    for (auto *f: dep_callbacks) {
      if (f) {
        f(src_inst, dst_inst, bare_inst, src_invoc, __slamp_invocation,
          src_iter, __slamp_iteration, addr, value, size);
      }
    }
    

    // source is a Write
    KEY key(src_inst, dst_inst, bare_inst, src_iter != __slamp_iteration);

#if DEBUG
    std::cout << "    [log] (" << src_iter << ":" << src_inst << ")->("
              << __slamp_iteration << ":" << dst_inst << ")\n";
    std::cout << "    [key] " << key.src << " " << key.dst << " "
              << key.dst_bare << " " << key.cross << "\n";

    std::stringstream ss;
    ss << src_inst << " " << dst_inst << " " << bare_inst << " "
       << (src_iter != __slamp_iteration ? 1 : 0);
    depset->insert(ss.str());
#endif

    auto distance = __slamp_iteration - src_iter;

#ifdef ONLY_SET
    deplog_set->insert(key);
#else
    if (deplog->count(key)) {
      Value &v = (*deplog)[key];
      v.count += 1;
      if (DISTANCE_MODULE) {
        if (v.d->isConstant && distance != v.d->distance) {
          v.d->isConstant = false;

          // add this back
          v.d->distribution[v.d->distance] = 1;
        }

        if (!v.d->isConstant) {
          if (v.d->distribution.count(distance)) {
            v.d->distribution[distance] += 1;
          } else {
            v.d->distribution[distance] = 1;
          }
        }
      }
    } else {
      // size == 0 means that value profiling is not possible
      // Value v(lp, lp_addr);
      Value v;
      v.count = 1;
      v.d = nullptr;
      if (DISTANCE_MODULE) {
        v.d = new DistanceDistribution(distance);
      }
      deplog->insert(std::make_pair(key, v));
    }
#endif

#if CTXTDEBUG
    dumpdependencecallstack(pts, key);
#endif
    return src_inst;
  }
// #endif

  return UINT32_MAX;
}

void distance_module_callback(uint32_t src_inst, uint32_t dst_inst, uint32_t bare_inst, uint64_t src_iter) {
  KEY key(src_inst, dst_inst, bare_inst, src_iter != __slamp_iteration);

  // 0 -> intra-iteration; >=1 loop-carried
  auto distance = __slamp_iteration - src_iter;

}

void print_log(const char *filename) {
  std::ofstream of(filename);

  // of << target_fn_id << "\n";
  // of << target_loop_id << "\n";

  // Add a fake dependence so the loop is recognized
  of << target_loop_id << " " << 0 << " " << 0 << " "
       << 0 << " " << 0 << " " << 0 << "\n";

#ifdef ONLY_SET
  std::set<KEY, KEYComp> ordered(deplog_set->begin(), deplog_set->end());
  for (auto &k: ordered) {
    of << target_loop_id << " " << k.src << " " << k.dst << " " << k.dst_bare << " "
       << (k.cross ? 1 : 0) << " " << 1 << " ";
    of << "\n";
  }

#else
  std::map<KEY, Value, KEYComp> ordered(deplog->begin(), deplog->end());


  for (auto &&mi : ordered) {
    KEY key = mi.first;
    Value &v = mi.second;


    of << target_loop_id << " " << key.src << " " << key.dst << " "
       << key.dst_bare << " " << key.cross << " " << v.count;

    if (DISTANCE_MODULE) {
      of << " ";
      of << "[";
      if (v.d->isConstant) {
        of << v.d->distance;
      } else {
        for (auto &[distance, count] : v.d->distribution) {
          of << "(" << distance << " " << count << "), ";
        }
      }
      of << "]";
    }

    of << "\n";
  }
#endif

  of.close();

  if (TRACE_MODULE) {
    dumpTrace();
  }
}

} // namespace slamp
