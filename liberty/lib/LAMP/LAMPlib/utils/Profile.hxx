#ifndef PROFILING_H
#define PROFILING_H

#include <inttypes.h>
#include <iostream>
#include <ostream>

#include <map>
#include <vector>
#include <iterator>

#include <tr1/unordered_set>

using namespace std;
using namespace std::tr1;

namespace Profiling {

  typedef struct ls_key_s {
    uint32_t store:22;
    uint32_t loop:10;
  }  __attribute__((__packed__)) ls_key_t;

  bool operator<(const ls_key_t &ls1, const ls_key_t&ls2) {
    return *((uint32_t *) &ls1) < *((uint32_t *) &ls2);
  }

  static const uint64_t PROFILE_INSTR_MAX = ((1ULL << 22) - 1);
  static const uint64_t PROFILE_LOOP_MAX = ((1ULL << 10) - 1);

  static const uint64_t DEFAULT_TRACKED_DISTANCE = 2;

  class Dependence {
    public:
      uint32_t store;
      uint32_t loop;
      uint32_t dist;
      uint32_t load;

      Dependence(const Dependence &dep)
      {
        this->store = dep.store;
        this->loop = dep.loop;
        this->dist = dep.dist;
        this->load = dep.load;
      }

      Dependence(uint32_t ld) : store(~(0U)), loop(~(0U)), dist(~(0U)), load(ld) {}

      Dependence(uint32_t st, uint32_t lp, uint32_t di, uint32_t ld)
        : store(st), loop(lp), dist(di), load(ld) {}

      bool operator==(const Dependence &dep) const {
        return (this->store == dep.store) && (this->loop == dep.loop)
          && (this->dist == dep.dist) && (this->load == dep.load);
      }
  };

  struct DependenceEquals {
    bool operator()(const Dependence &page1, const Dependence page2) const {
      return page1 == page2;
    }
  };

  struct DependenceHash {
    size_t operator()(const Dependence &dep) const {
      return dep.store ^ dep.load ^ dep.loop;
    }
  };

  ostream &operator<<(ostream &stream, const Dependence &dep) {
    stream<<dep.store<<" "<<dep.loop<<" "<<dep.dist<<" "<<dep.load;
    return stream;
  }

  typedef unordered_set<Dependence, DependenceHash, DependenceEquals> DependenceSet;

  template <class T, int maxTrackedDistance = DEFAULT_TRACKED_DISTANCE>
    class KeyDistanceProfiler {
      public:

        static const uint64_t MAX_TRACKED_DISTANCE = maxTrackedDistance;

        typedef map<ls_key_t, T> KeyProfilerMap;

        typedef vector<KeyProfilerMap> DistanceMaps;

        typedef vector<DistanceMaps> InstructionMaps;

      private:
        InstructionMaps instructionInfo;

      public:
        KeyDistanceProfiler(const uint32_t num_instrs) : instructionInfo(num_instrs){
          if (sizeof(ls_key_t) != sizeof(uint32_t)) {
            cerr<<"sizeof(ls_key_t) != sizeof(uint32_t) ("<<sizeof(ls_key_t)<<" != "<<sizeof(uint32_t)<<")"<<endl;
            abort();
          }

          if (num_instrs > PROFILE_INSTR_MAX) {
            cerr<<"Number of instructions must be less than "<<PROFILE_INSTR_MAX<<" "<<num_instrs<<" given"<<endl;
            abort();
          }
        }

        static uint32_t trackedDistance(const uint32_t dist) {
          const uint32_t tracked_distance = (dist >= maxTrackedDistance) ? (maxTrackedDistance - 1) : dist;
          return tracked_distance;
        }

        T & getProfile(const Dependence &dep) {
          DistanceMaps &distanceMap = instructionInfo.at(dep.load);

          if (distanceMap.empty()) {
            for (uint32_t i = 0; i < maxTrackedDistance; i++) {
              distanceMap.push_back(KeyProfilerMap());
            }
          }

          const uint32_t tracked_distance = trackedDistance(dep.dist);

          KeyProfilerMap &valueMap = distanceMap.at(tracked_distance);

          const ls_key_t key = {dep.store, dep.loop};
          if (valueMap.find(key) == valueMap.end()) {
            T value_count;
            valueMap[key] = value_count;
          }
          T &profiler = valueMap[key];
          return profiler;
        }

        template<class S, int D>
          friend ostream &operator<<(ostream &stream, const KeyDistanceProfiler<S, D> &vp);
    };

  template<class T, int D>
    ostream &operator<<(ostream &stream, const KeyDistanceProfiler<T, D> &vp){
      typename KeyDistanceProfiler<T, D>::InstructionMaps::const_iterator instrIter = vp.instructionInfo.begin();
      for (; instrIter != vp.instructionInfo.end(); instrIter++) {
        const uint32_t load = distance(vp.instructionInfo.begin(), instrIter);
        typename KeyDistanceProfiler<T, D>::DistanceMaps::const_iterator distIter = instrIter->begin();
        for (; distIter != instrIter->end(); distIter++) {
          const uint32_t dist = distance(instrIter->begin(), distIter);
          typename KeyDistanceProfiler<T, D>::KeyProfilerMap::const_iterator keyIter = distIter->begin();
          for(; keyIter != distIter->end(); keyIter++) {
            const ls_key_t key = keyIter->first;
            const uint32_t loop = key.loop;
            const uint32_t store = key.store;
            const T &profile = (keyIter->second);

            stream<<"("<<load<<" "<<dist<<" "<<loop<<" "<<store<<" ("<<profile<<") )"<<endl;
          }
        }
      }
      return stream;
    }

}

#endif
