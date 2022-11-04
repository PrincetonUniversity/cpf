/*
 * HT (High Throughput) Containers
 * Author: Ziyang Xu
 * 
 * Use vector as buffer and use parallelism to improve performance
 * This can replace set and map in STL
 *
 */
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

template <typename T, typename Hash = std::hash<T>,
          typename KeyEqual = std::equal_to<T>,
          uint32_t MAX_THREAD = 56,
          uint32_t BUFFER_SIZE = 1'000'000>
class HTSet {
private:
  std::vector<T> buffer;
  std::mutex m;

public:
  std::unordered_set<T, Hash, KeyEqual> set;
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

private:
  // TODO: adaptive thread count
  const uint32_t getThreadCount() { return MAX_THREAD; }

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
            auto *set_chunk = new std::unordered_set<T, Hash, KeyEqual>();
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
