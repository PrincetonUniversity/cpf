/*
 * HT (High Throughput) Containers
 * Author: Ziyang Xu
 * 
 * Use vector as buffer and use parallelism to improve performance
 * This can replace set and map in STL
 *
 */
#pragma once
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>
#include <fstream>

#define HT
// #define ADAPTIVE_HT
#define PB

#ifdef PB
#include "parallel_hashmap/phmap.h"
#else
#include <unordered_set>
#endif


#ifdef PB
#define hash_set phmap::flat_hash_set
#define hash_map phmap::flat_hash_map
#else
#define hash_set std::unordered_set
#define hash_map std::unordered_map
#endif

#define HT_THREAD_POOL
#define CACHELINE_SIZE 64


template <typename T, typename Hash = std::hash<T>,
          typename KeyEqual = std::equal_to<T>,
          uint32_t MAX_THREAD = 56,
          uint32_t BUFFER_SIZE = 1'000'000>
class HTSet {

    public:
      void Start() {
      }

    private:
#ifdef HT_THREAD_POOL
      bool should_terminate = false;           // Tells threads to stop looking for jobs
      std::mutex queue_mutex;                  // Prevents data races to the job queue
      std::condition_variable mutex_condition; // Allows threads to wait on new jobs or termination 
      std::vector<std::thread> threads;
      volatile int pending_jobs = 0;           // Number of jobs that have not been completed
      // avoid false sharing

      std::vector<bool> ready;

      void ThreadLoop(const int id) {
        auto set_chunk = std::make_unique<hash_set<T, Hash, KeyEqual>>();

        const auto thread_count = MAX_THREAD;
        auto job = [&]() {
          const auto set_size = buffer.size() / thread_count;
          const auto buffer_size = buffer.size();
          // take the chunk and convert to a set and return
          // set_chunk->reserve(set_size);

          auto begin = id * (buffer_size / thread_count);
          auto end = (id + 1) * (buffer_size / thread_count);

          set_chunk->insert(buffer.begin() + begin, buffer.begin() + end);

          m.lock();
          // lock the global set and insert the chunk
          set.insert(set_chunk->begin(), set_chunk->end());
          m.unlock();
          set_chunk->clear();
        };
        while (true) {
          {
            std::unique_lock<std::mutex> lock(queue_mutex);
            mutex_condition.wait(lock, [this, id] { return ready[id * CACHELINE_SIZE]  || should_terminate; });
            // std::cout << "Thread " << id << " is running" << std::endl;
            if (should_terminate) {
              return;
            }
          }
          job();
          ready[id * CACHELINE_SIZE] = false;
          {
            std::unique_lock<std::mutex> lock(queue_mutex);
            pending_jobs--;
          }
        }
      }
#endif


private:
  std::vector<T> buffer;
  std::mutex m;
  using hash_set_t = hash_set<T, Hash, KeyEqual>;

public:
  hash_set_t set;
  HTSet() {
    buffer.reserve(BUFFER_SIZE);
#ifdef HT_THREAD_POOL
    const uint32_t num_threads = MAX_THREAD;
    threads.resize(num_threads);

    ready.resize(num_threads * CACHELINE_SIZE);
    for (uint32_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(&HTSet::ThreadLoop, this, i);
      ready[i * CACHELINE_SIZE] = false;
      // threads.at(i) = std::thread(ThreadLoop, i);
    }
#endif
  }

  ~HTSet() {
#ifdef HT_THREAD_POOL
    should_terminate = true;
    mutex_condition.notify_all();
    for (auto &thread : threads) {
      thread.join();
    }
#endif
  }

  void emplace_back(T &&t) {
#ifdef HT
    buffer.emplace_back(std::move(t));
    checkBuffer();
#else
    set.emplace(std::move(t));
#endif
  }

  void emplace_back(const T &t) {
#ifdef HT
    buffer.emplace_back(t);
    checkBuffer();
#else
    set.emplace(t);
#endif
  }

  void emplace(T &&t) {
    emplace_back(t);
  }

  // the same as emplace_back
  void emplace(const T &t) {
    emplace_back(t);
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

#ifdef HT_THREAD_POOL
    pending_jobs = thread_count;

    for (uint32_t i = 0; i < thread_count; i++) {
      ready[i * CACHELINE_SIZE] = true;
    }

    // std::cout << "pending jobs: " << pending_jobs << std::endl;
    mutex_condition.notify_all();

    // std:: cout << "waiting for jobs to finish" << std::endl;

    // busy wait: check if all threads are done
    while (true) {
      // std::cout << "pending jobs: " << pending_jobs << std::endl;
      if (pending_jobs == 0) {
        break;
      }
    }
#endif

#ifndef HT_THREAD_POOL
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
#endif
  }
};

// HTMap_Redux, HTMap_T_Set

template <typename T, typename Hash = std::hash<T>,
          typename KeyEqual = std::equal_to<T>,
          uint32_t MAX_THREAD = 56,
          uint32_t BUFFER_SIZE = 1'000'000>
class HTMap_Sum {

    public:
      void Start() {
      }

    private:
#ifdef HT_THREAD_POOL
      bool should_terminate = false;           // Tells threads to stop looking for jobs
      std::mutex queue_mutex;                  // Prevents data races to the job queue
      std::condition_variable mutex_condition; // Allows threads to wait on new jobs or termination 
      std::vector<std::thread> threads;
      volatile int pending_jobs = 0;           // Number of jobs that have not been completed
      // avoid false sharing

      std::vector<bool> ready;

      void ThreadLoop(const int id) {
        auto map_chunk = std::make_unique<hash_map<T, unsigned, Hash, KeyEqual>>();

        const auto thread_count = MAX_THREAD;
        auto job = [&]() {
          const auto set_size = buffer.size() / thread_count;
          const auto buffer_size = buffer.size();

          auto begin = id * (buffer_size / thread_count);
          auto end = (id + 1) * (buffer_size / thread_count);

          // for each element in the chunk, insert into the map, and increment the count
          for (auto key : buffer) {
            auto it = map_chunk->find(key);
            if (it == map_chunk->end()) {
              map_chunk->insert({key, 1});
            } else {
              it->second++;
            }
          }

          m.lock();
          // lock the global set and insert the chunk
          // merge the map_chunk into the global map
          for (auto it = map_chunk->begin(); it != map_chunk->end(); ++it) {
            auto global_it = map.find(it->first);
            if (global_it == map.end()) {
              map.insert({it->first, it->second});
            } else {
              global_it->second += it->second;
            }
          }
          m.unlock();
          map_chunk->clear();
        };
        while (true) {
          {
            std::unique_lock<std::mutex> lock(queue_mutex);
            mutex_condition.wait(lock, [this, id] { return ready[id * CACHELINE_SIZE]  || should_terminate; });
            // std::cout << "Thread " << id << " is running" << std::endl;
            if (should_terminate) {
              return;
            }
          }
          job();
          ready[id * CACHELINE_SIZE] = false;
          {
            std::unique_lock<std::mutex> lock(queue_mutex);
            pending_jobs--;
          }
        }
      }
#endif


private:
  std::vector<T> buffer;
  std::mutex m;
  using hash_map_t = hash_map<T, unsigned, Hash, KeyEqual>;

public:
  hash_map_t map;
  HTMap_Sum() {
    buffer.reserve(BUFFER_SIZE);
#ifdef HT_THREAD_POOL
    const uint32_t num_threads = MAX_THREAD;
    threads.resize(num_threads);

    ready.resize(num_threads * CACHELINE_SIZE);
    for (uint32_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(&HTMap_Sum::ThreadLoop, this, i);
      ready[i * CACHELINE_SIZE] = false;
      // threads.at(i) = std::thread(ThreadLoop, i);
    }
#endif
  }

  ~HTMap_Sum() {
#ifdef HT_THREAD_POOL
    should_terminate = true;
    mutex_condition.notify_all();
    for (auto &thread : threads) {
      thread.join();
    }
#endif
  }

  void emplace_back(T &&t) {
#ifdef HT
    buffer.emplace_back(std::move(t));
    checkBuffer();
#else
    set.emplace(std::move(t));
#endif
  }

  void emplace_back(const T &t) {
#ifdef HT
    buffer.emplace_back(t);
    checkBuffer();
#else
    set.emplace(t);
#endif
  }

  void emplace(T &&t) {
    emplace_back(t);
  }

  // the same as emplace_back
  void emplace(const T &t) {
    emplace_back(t);
  }

  // iterator begin
  auto begin() {
    convertVectorToSet();
    return map.begin();
  }

  // iterator end
  auto end() {
    convertVectorToSet();
    return map.end();
  }

  // provide [] operator
  auto &operator[](const T &key) {
    convertVectorToSet();
    return map[key];
  }

  // void merge_set(HTSet &other) {
    // merge_set(other.begin(), other.end());
  // }

  // // insert (begin, end)
  // void merge_set(typename hash_set_t::iterator begin, typename hash_set_t::iterator end) {
  //   for (auto it = begin; it != end; ++it) {
  //     set.insert(*it);
  //   }
  // }


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
      // merge the buffer to the map
      for (auto key : buffer) {
        auto it = map.find(key);
        if (it == map.end()) {
          map.insert({key, 1});
        } else {
          it->second++;
        }
      }
      return;
    }

#ifdef HT_THREAD_POOL
    pending_jobs = thread_count;

    for (uint32_t i = 0; i < thread_count; i++) {
      ready[i * CACHELINE_SIZE] = true;
    }

    // std::cout << "pending jobs: " << pending_jobs << std::endl;
    mutex_condition.notify_all();

    // std:: cout << "waiting for jobs to finish" << std::endl;

    // busy wait: check if all threads are done
    while (true) {
      // std::cout << "pending jobs: " << pending_jobs << std::endl;
      if (pending_jobs == 0) {
        break;
      }
    }
#endif

#ifndef HT_THREAD_POOL
    static_assert(false, "HT_THREAD_POOL is not defined, invalid for map");
#endif
  }
};

template <typename TK, typename Hash = std::hash<TK>,
          typename KeyEqual = std::equal_to<TK>, uint32_t MAX_THREAD = 16,
          uint32_t BUFFER_SIZE = 1'000'000>
class HTMap_IsConstant {
  using TV = uint64_t;

public:
  constexpr static TV MAGIC_UNITIALIZED = 0xdeadbeefdeadbeef;
  constexpr static TV MAGIC_INVALID = 0xbeefdeadbeefdead;
  void Start() {}

private:
#ifdef HT_THREAD_POOL
  bool should_terminate = false; // Tells threads to stop looking for jobs
  std::mutex queue_mutex;        // Prevents data races to the job queue
  std::condition_variable
      mutex_condition; // Allows threads to wait on new jobs or termination
  std::vector<std::thread> threads;
  volatile int pending_jobs = 0; // Number of jobs that have not been completed
  // avoid false sharing

  std::vector<bool> ready;

  void ThreadLoop(const int id) {
    auto map_chunk = std::make_unique<hash_map_t>();

    const auto thread_count = MAX_THREAD;
    auto job = [&]() {
      const auto set_size = buffer.size() / thread_count;
      const auto buffer_size = buffer.size();

      auto begin = id * (buffer_size / thread_count);
      auto end = (id + 1) * (buffer_size / thread_count);

      // for each element in the chunk, insert into the map, and set invalid if not same
      for (auto &&[key, value]: buffer) {
        auto it = map_chunk->find(key);
        if (it == map_chunk->end()) {
          map_chunk->insert({key, value});
        } else {
          // check if value is the same
          if (it->second != MAGIC_INVALID && it->second != value) {
            it->second = MAGIC_INVALID;
          }
        }
      }

      m.lock();
      // lock the global set and insert the chunk
      // merge the map_chunk into the global map
      for (auto it = map_chunk->begin(); it != map_chunk->end(); ++it) {
        auto global_it = map.find(it->first);
        if (global_it == map.end()) {
          map.insert({it->first, it->second});
        } else {
          // check if value is the same
          if (global_it->second != MAGIC_INVALID && global_it->second != it->second) {
            global_it->second = MAGIC_INVALID;
          }
        }
      }
      m.unlock();
      map_chunk->clear();
    };
    while (true) {
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        mutex_condition.wait(lock, [this, id] {
          return ready[id * CACHELINE_SIZE] || should_terminate;
        });
        // std::cout << "Thread " << id << " is running" << std::endl;
        if (should_terminate) {
          return;
        }
      }
      job();
      ready[id * CACHELINE_SIZE] = false;
      {
        std::unique_lock<std::mutex> lock(queue_mutex);
        pending_jobs--;
      }
    }
  }
#endif

private:
  using buffer_item_t = std::pair<TK, TV>;
  std::vector<buffer_item_t> buffer;
  std::mutex m;
  using hash_map_t = hash_map<TK, TV, Hash, KeyEqual>;

public:
  hash_map_t map;
  HTMap_IsConstant() {
    buffer.reserve(BUFFER_SIZE);
#ifdef HT_THREAD_POOL
    const uint32_t num_threads = MAX_THREAD;
    threads.resize(num_threads);

    ready.resize(num_threads * CACHELINE_SIZE);
    for (uint32_t i = 0; i < num_threads; i++) {
      threads[i] = std::thread(&HTMap_IsConstant::ThreadLoop, this, i);
      ready[i * CACHELINE_SIZE] = false;
      // threads.at(i) = std::thread(ThreadLoop, i);
    }
#endif
  }

  ~HTMap_IsConstant() {
#ifdef HT_THREAD_POOL
    should_terminate = true;
    mutex_condition.notify_all();
    for (auto &thread : threads) {
      thread.join();
    }
#endif
  }

  void emplace_back(buffer_item_t &&t) {
#ifdef HT
    buffer.emplace_back(std::move(t));
    checkBuffer();
#else
    set.emplace(std::move(t));
#endif
  }

  void emplace_back(const buffer_item_t &t) {
#ifdef HT
    buffer.emplace_back(t);
    checkBuffer();
#else
    set.emplace(t);
#endif
  }

  void emplace(buffer_item_t &&t) { emplace_back(t); }

  // the same as emplace_back
  void emplace(const buffer_item_t &t) { emplace_back(t); }

  // iterator begin
  auto begin() {
    convertVectorToSet();
    return map.begin();
  }

  // iterator end
  auto end() {
    convertVectorToSet();
    return map.end();
  }

  auto count(const TK &key) {
    convertVectorToSet();
    return map.count(key);
  }

  // provide [] operator
  auto &operator[](const TK &key) {
    convertVectorToSet();
    return map[key];
  }

  // void merge_set(HTSet &other) {
  // merge_set(other.begin(), other.end());
  // }

  // // insert (begin, end)
  // void merge_set(typename hash_set_t::iterator begin, typename
  // hash_set_t::iterator end) {
  //   for (auto it = begin; it != end; ++it) {
  //     set.insert(*it);
  //   }
  // }

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
    running_threads =
        running_threads < MAX_THREAD ? running_threads : MAX_THREAD;

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
      // merge the buffer to the map
      for (auto p: buffer) {
        auto &key = p.first;
        auto &value = p.second;
        auto it = map.find(key);

        if (it == map.end()) {
          map.insert({key, value});
        } else {
          // check if value is the same
          if (it->second != MAGIC_INVALID && it->second != value) {
            it->second = MAGIC_INVALID;
          }
        }
      }
      return;
    }

#ifdef HT_THREAD_POOL
    pending_jobs = thread_count;

    for (uint32_t i = 0; i < thread_count; i++) {
      ready[i * CACHELINE_SIZE] = true;
    }

    // std::cout << "pending jobs: " << pending_jobs << std::endl;
    mutex_condition.notify_all();

    // std:: cout << "waiting for jobs to finish" << std::endl;

    // busy wait: check if all threads are done
    while (true) {
      // std::cout << "pending jobs: " << pending_jobs << std::endl;
      if (pending_jobs == 0) {
        break;
      }
    }
#endif

#ifndef HT_THREAD_POOL
    static_assert(false, "HT_THREAD_POOL is not defined, invalid for map");
#endif
  }
};
