#include "slamp_logger.h"

#define DISTANCE_MODULE true
#define CONSTANT_MODULE true
#define LINEAR_MODULE true
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>

#include <map>
#include <unordered_map>

#include "slamp_debug.h"
#include "slamp_timestamp.h"
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
extern std::map<void *, size_t> *alloc_in_the_loop;

#if DEBUG
#include <set>
#include <sstream>
#include <string>

extern uint8_t __slamp_begin_trace;
#endif

namespace slamp {

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

struct DistanceDistribution {
  bool constant;
  uint32_t distance;
  std::unordered_map<uint32_t, uint32_t> distribution;
  DistanceDistribution(uint32_t distance): constant(true), distance(distance) {}
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

struct Value {
  uint64_t count{0};
  Constant *c{nullptr};
  LinearPredictor *lp{nullptr};
  DistanceDistribution *d; 
  char pad[64 - sizeof(uint64_t) - sizeof(void *) - sizeof(void *)];

  // Value() : count(0), c(NULL), lp(NULL) { assert(false); }
  Value() = default;
  Value(Constant *c, LinearPredictor *lp) : c(c), lp(lp) {}
};

static std::unordered_map<KEY, Value, KEYHash, KEYEqual> *deplog;
static std::unordered_map<KEY, Constant *, KEYHash, KEYEqual> *constmap;
static std::unordered_map<KEY, LinearPredictor *, KEYHash, KEYEqual> *lpmap;
#if DEBUG
static std::set<std::string> *depset;
#endif

static uint32_t target_fn_id;
static uint32_t target_loop_id;

void init_logger(uint32_t fn_id, uint32_t loop_id) {
  deplog = new std::unordered_map<KEY, Value, KEYHash, KEYEqual>();
  constmap = new std::unordered_map<KEY, Constant *, KEYHash, KEYEqual>();
  lpmap = new std::unordered_map<KEY, LinearPredictor *, KEYHash, KEYEqual>();
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
  delete deplog;
}

void log(TS ts, const uint32_t dst_inst, TS *pts, const uint32_t bare_inst,
         uint64_t addr, uint64_t value, uint8_t size) {
  // ZY: check invocation counter, if not the same, just return; because don't
  // create new dependence between two invocations
  if (ts) {
    uint64_t src_invoc = GET_INVOC(ts);
    if (src_invoc != __slamp_invocation)
      return;
  }

   Constant* cp = nullptr;

   // check if constant
   if (CONSTANT_MODULE) {
     KEY constkey(0, dst_inst, bare_inst, 0);
     if (constmap->count(constkey)) {
       cp = (*constmap)[constkey];
       assert(cp->size == size);

       // // Remove check for constant need to have the same address
       // if (cp->valueinit && cp->addr != addr)
       //   cp->valid = false;
       if (cp->valueinit && cp->value != value)
         cp->valid = false;

       if (ts) {
         cp->valueinit = true;
         cp->value = value;
         cp->addr = addr;
       }
     } else {
       bool valueinit = ts ? true : false;
       cp = new Constant((size != 0), valueinit, size, addr, value);
       constmap->insert(std::make_pair(constkey, cp));
     }

     assert(cp);
   }

   // check if linear predictable. constkey can be reused here.
   LinearPredictor *lp = nullptr;

   if (LINEAR_MODULE) {
     KEY constkey(0, dst_inst, bare_inst, 0);
     if (lpmap->count(constkey)) {
       lp = (*lpmap)[constkey];
       lp->add_sample(__slamp_iteration, value, addr);
     } else {
       lp = new LinearPredictor(__slamp_iteration, value, addr);
       lpmap->insert(std::make_pair(constkey, lp));
     }

     assert(lp);
   }

   // update log
   if (ts) {
     uint32_t src_inst = GET_INSTR(ts);
     uint64_t src_iter = GET_ITER(ts);

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

     // 0 -> intra-iteration; >=1 loop-carried
     auto distance = __slamp_iteration - src_iter;

     if (deplog->count(key)) {
       Value &v = (*deplog)[key];
       v.count += 1;
       if (DISTANCE_MODULE) {
         if (v.d->constant && distance != v.d->distance) {
           v.d->constant = false;

           // add this back
           v.d->distribution[v.d->distance] = 1;
         }

         if (!v.d->constant) {
           if (v.d->distribution.count(distance)) {
             v.d->distribution[distance] += 1;
           } else {
             v.d->distribution[distance] = 1;
           }
         }
       }
     } else {
       // size == 0 means that value profiling is not possible
       Value v(cp, lp);
       v.count = 1;
       if (DISTANCE_MODULE) {
         v.d = new DistanceDistribution(distance);
       }
       deplog->insert(std::make_pair(key, v));
     }

#if CTXTDEBUG
     dumpdependencecallstack(pts, key);
#endif
   }
}

void print_log(const char *filename) {
  std::ofstream of(filename);

  // of << target_fn_id << "\n";
  // of << target_loop_id << "\n";

  std::map<KEY, Value, KEYComp> ordered(deplog->begin(), deplog->end());

  // Add a fake dependence so the loop is recognized
  of << target_loop_id << " " << 0 << " " << 0 << " "
       << 0 << " " << 0 << " " << 0 << "\n";

  for (auto &&mi : ordered) {
    KEY key = mi.first;
    Value &v = mi.second;


    of << target_loop_id << " " << key.src << " " << key.dst << " "
       << key.dst_bare << " " << key.cross << " " << v.count;

    if (DISTANCE_MODULE) {
      of << " ";
      if (v.d->constant) {
        of << v.d->distance;
      } else {
        of << "[";
        for (auto &[distance, count] : v.d->distribution) {
          of << "(" << distance << " " << count << "), ";
        }
        of << "]";
      }
    }
 

    if (CONSTANT_MODULE) {
       Constant*        cp = v.c;
       of << " "
          << cp->valid << " " << (unsigned)(cp->size) << " " << (cp->value);
    }

    if (LINEAR_MODULE) {
      LinearPredictor *lp = v.lp;

      bool lp_int_valid = (lp->stable && lp->valid_as_int);
      bool lp_double_valid = (lp->stable && lp->valid_as_double);
      of << lp_int_valid << " " << (lp_int_valid ? lp->ia : 0) << " "
         << (lp_int_valid ? lp->ib : 0) << " " << lp_double_valid << " "
         << (lp_double_valid ? lp->da : 0) << " "
         << (lp_double_valid ? lp->db : 0);
    }

    of << "\n";
  }

  of.close();
}

} // namespace slamp
