#include <future>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <ThreadSafeQueue.hpp>
#include <ThreadSafeLockFreeQueue.hpp>
#include <ThreadPool.hpp>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>
#include <iostream>

using namespace MARC;

#define CACHE_LINE_SIZE 64

#ifdef DSWP_STATS
static int64_t numberOfPushes8 = 0;
static int64_t numberOfPushes16 = 0;
static int64_t numberOfPushes32 = 0;
static int64_t numberOfPushes64 = 0;
#endif

static ThreadPool pool{true, std::thread::hardware_concurrency()};

extern "C" {

  #if 0
  typedef void (*stageFunctionPtr_t)(void *, void*);

  void printReachedS(std::string s)
  {
    auto outS = "Reached: " + s;
    printf("%s\n",outS.c_str());
  }

  void printReachedI(int i){
    printf("Reached: %d\n",i);
  }

  void printPushedP(int32_t *p){
    printf("Pushed: %p\n", p);
  }

  void printPulledP(int32_t *p){
    printf("Pulled: %p\n", p);
  }

  void queuePush8(ThreadSafeQueue<int8_t> *queue, int8_t *val) {
    queue->push(*val);

    #ifdef DSWP_STATS
    numberOfPushes8++;
    #endif

    return ;
  }

  void queuePop8(ThreadSafeQueue<int8_t> *queue, int8_t *val) {
    queue->waitPop(*val);
    return ;
  }

  void queuePush16(ThreadSafeQueue<int16_t> *queue, int16_t *val) {
    queue->push(*val);

    #ifdef DSWP_STATS
    numberOfPushes16++;
    #endif

    return ;
  }

  void queuePop16(ThreadSafeQueue<int16_t> *queue, int16_t *val) {
    queue->waitPop(*val);
  }

  void queuePush32(ThreadSafeQueue<int32_t> *queue, int32_t *val) {
    queue->push(*val);

    #ifdef DSWP_STATS
    numberOfPushes32++;
    #endif

    return ;
  }

  void queuePop32(ThreadSafeQueue<int32_t> *queue, int32_t *val) {
    queue->waitPop(*val);
  }

  void queuePush64(ThreadSafeQueue<int64_t> *queue, int64_t *val) {
    queue->push(*val);

    #ifdef DSWP_STATS
    numberOfPushes64++;
    #endif

    return ;
  }

  void queuePop64(ThreadSafeQueue<int64_t> *queue, int64_t *val) {
    queue->waitPop(*val);

    return ;
  }
  #endif

  void doallDispatcher (void (*chunker)(void *, int64_t, int64_t, int64_t), void *env, int64_t numCores, int64_t chunkSize){
    #ifdef RUNTIME_PRINT
    std::cerr << "Starting dispatcher: num cores " << numCores << ", chunk size: " << chunkSize << std::endl;
    #endif

    std::vector<MARC::TaskFuture<void>> localFutures;
    for (auto i = 0; i < numCores; ++i) {
      localFutures.push_back(pool.submit(chunker, env, i, numCores, chunkSize));
      #ifdef RUNTIME_PRINT
      std::cerr << "Submitted chunker on core " << i << std::endl;
      #endif
    }
    #ifdef RUNTIME_PRINT
    std::cerr << "Submitted pool" << std::endl;
    #endif

    for (auto& future : localFutures){
      future.get();
    }
    #ifdef RUNTIME_PRINT
    std::cerr << "Got all futures" << std::endl;
    #endif

    return ;
  }
}
