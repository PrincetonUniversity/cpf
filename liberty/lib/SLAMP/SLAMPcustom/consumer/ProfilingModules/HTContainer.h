/*
 * HT (High Throughput) Containers
 * Author: Ziyang Xu
 * 
 * Use vector as buffer and use parallelism to improve performance
 * This can replace set and map in STL
 *
 */
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <fstream>

#define PB
#ifdef PB
#include "parallel_hashmap/phmap.h"
#else
#include <unordered_set>
#endif

#define ADAPTIVE_HT

#ifdef PB
#define hash_set phmap::flat_hash_set
#else
#define hash_set std::unordered_set
#endif


template <typename T, typename Hash = std::hash<T>,
          typename KeyEqual = std::equal_to<T>,
          uint32_t MAX_THREAD = 56,
          uint32_t BUFFER_SIZE = 1'000'000>
class HTSet {
private:
  std::vector<T> buffer;
  std::mutex m;
  using hash_set_t = hash_set<T, Hash, KeyEqual>;

public:
  hash_set_t set;
  HTSet() { buffer.reserve(BUFFER_SIZE); }

  void emplace_back(T &&t) {
    buffer.emplace_back(std::move(t));

    checkBuffer();
  }

  void emplace_back(const T &t) {
    buffer.emplace_back(t);

    checkBuffer();
  }

  // iterator begin
  auto begin() {
    convertVectorToSet();
    return set.begin();
  }

  // iterator end
  auto end() {
    convertVectorToSet();
    return set.end();
  }

  void merge_set(HTSet &other) {
    merge_set(other.begin(), other.end());
  }

  // insert (begin, end)
  void merge_set(typename hash_set_t::iterator begin, typename hash_set_t::iterator end) {
    for (auto it = begin; it != end; ++it) {
      set.insert(*it);
    }
  }


private:
  const uint32_t getThreadCount() {
#ifdef ADAPTIVE_HT
    // TODO: adaptive thread count, measure the performance benefit of this
    // get current active number of threads from /proc/loadavg
    std::ifstream loadavg("/proc/loadavg");

    // get active threads count
    // ignore the first three fp numbers, find the 4th int number (before "/")
    std::string load;
    for (int i = 0; i < 3; i++) {
      loadavg >> load;
    }
    loadavg >> load;
    loadavg.close();
    load = load.substr(0, load.find('/'));
    int active_threads = std::stoi(load);

    uint32_t MAX_CORES = 56;

    int running_threads = MAX_CORES - active_threads;
    // max(1, running_threads)
    running_threads = running_threads > 0 ? running_threads : 1;
    // min(MAX_THREAD, running_threads)
    running_threads = running_threads < MAX_THREAD ? running_threads : MAX_THREAD;


    // std::cout << "active threads: " << running_threads << std::endl;

    return running_threads;
#else
    return MAX_THREAD;
#endif
  }

  inline void checkBuffer() {
    if (buffer.size() == BUFFER_SIZE) {
      convertVectorToSet();
      buffer.resize(0);
      buffer.reserve(BUFFER_SIZE);
    }
  }

  void convertVectorToSet() {
    const uint32_t thread_count = getThreadCount();
    const auto set_size = buffer.size() / thread_count;
    const auto buffer_size = buffer.size();

    if (buffer_size == 0) {
      return;
    }

    if (thread_count == 1) {
      set.insert(buffer.begin(), buffer.end());
      return;
    }

    // launch N threads to convert the vector to set independently, chunking
    std::thread t[thread_count];
    for (unsigned long i = 0; i < thread_count; i++) {
      t[i] = std::thread(
          [&](int id) {
            // take the chunk and convert to a set and return
            auto *set_chunk = new hash_set<T, Hash, KeyEqual>();
            set_chunk->reserve(set_size);

            auto begin = id * (buffer_size / thread_count);
            auto end = (id + 1) * (buffer_size / thread_count);

            set_chunk->insert(buffer.begin() + begin, buffer.begin() + end);

            m.lock();
            // lock the global set and insert the chunk
            set.insert(set_chunk->begin(), set_chunk->end());
            m.unlock();
            delete set_chunk;
          },
          i);
    }
    // join the threads
    for (auto &i : t) {
      i.join();
    }
  }
};
