/** ***********************************************/
/** *** SW Queue with Supporting Variable Size ****/
/** ***********************************************/
#ifndef SW_QUEUE_H
#define SW_QUEUE_H

#include <cstdint>
#include <iostream>
#define DUALCORE

#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <xmmintrin.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "inline.h"
#include "bitcast.h"
#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
/*#define CACHELINE_SIZE 64 */
/* Cache line size of glacier, as reported by lmbench. Other tests,
 * namely the smtx ones, vindicate this. */
#endif /* CACHELINE_SIZE */

#define QTYPE uint32_t
#ifndef QSIZE
#define QSIZE_BYTES (1 << 26) // 1 << 0 - 1 byte; 1 << 10 1KB; 1 << 20 1MB; 1 << 24 8MB
#define QSIZE (QSIZE_BYTES / sizeof(QTYPE))
// #define QSIZE (1 << 23)
#endif /* QSIZE */

#ifndef QPREFETCH
#define QPREFETCH (1 << 7)
#endif /* QPREFETCH */

#define PAD(suffix, size) char padding ## suffix [CACHELINE_SIZE - (size)]

struct UnderlyingQueue {
  volatile bool ready_to_read;
  PAD(1, sizeof(bool));
  volatile bool ready_to_write;
  PAD(2, sizeof(bool));
  uint64_t size;
  PAD(3, sizeof(uint64_t));
  uint32_t *data;
  
  void init(uint32_t *data){
    this->ready_to_read = false;
    this->ready_to_write = true;
    this->size = 0;
    this->data = data;
  }
};

using Queue = UnderlyingQueue;
using Queue_p = Queue *;

struct DoubleQueue {
  Queue_p qA, qB, qNow, qOther;
  uint64_t index = 0;
  uint64_t size = 0;
  uint32_t *data;

  std::mutex &m;
  std::condition_variable &cv;
  unsigned &running_threads;

  DoubleQueue(Queue_p dqA, Queue_p dqB, bool isConsumer, unsigned &threads, 
      std::mutex &m, std::condition_variable &cv)
      : qA(dqA), qB(dqB), m(m), cv(cv), running_threads(threads) {
    this->qA = dqA;
    this->qB = dqB;

    // consumer
    if (isConsumer) {
      this->qNow = dqB;
      this->qOther = dqA;
    }
    // producer
    else {
      this->qNow = dqA;
      this->qOther = dqB;
    }

    this->data = qNow->data;
  }

  void swap(){
    if(qNow == qA){
      qNow = qB;
      qOther = qA;
    }else{
      qNow = qA;
      qOther = qB;
    }
    data = qNow->data;
  }

  void check() {
    if (index == size) {
      // only the last thread one does this
      auto lock = std::unique_lock<std::mutex>(m);
      // lock is locked
      if (running_threads == 1) {
        qNow->ready_to_read = false;
        qNow->ready_to_write = true;
        // std::cerr << "Thread " << std::this_thread::get_id() << " is waiting for queue" << std::endl;
        while (!qOther->ready_to_read) {
          // spin
          usleep(10);
        }
        qOther->ready_to_write = false;
        // std::cerr << "Thread " << std::this_thread::get_id() << " ready for queue" << std::endl;
        lock.unlock();
        // allow all other threads to continue
        cv.notify_all();
        // std::cerr << "Thread " << std::this_thread::get_id() << " is ready to go" << std::endl;
      }
      else {
        // lock is locked
        running_threads--;
        // std::cerr << "Thread " << std::this_thread::get_id() << " is waiting, threads: " << running_threads << std::endl;

        // wait fo the last thread to finish
        cv.wait(lock); // unlocks
        // std::cerr << "Thread " << std::this_thread::get_id() << " is unlocked" << std::endl;
        // lock reaquires
        running_threads++;
        lock.unlock();
        // std::cerr << "Thread " << std::this_thread::get_id() << " is ready to go" << std::endl;
      }
      swap();
      index = 0;
      size = qNow->size;
    }
  }

  uint32_t consume32() {
    uint32_t ret = data[index];
    index++;
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
    return ret;
  }

  uint64_t consume64() {
    uint64_t ret = *(uint64_t *) &data[index];
    index += 2;
    return ret;
  }

  void consume_64_64(uint64_t &x, uint64_t &y) {
    uint64_t *ptr = (uint64_t *) &data[index];
    x = ptr[0];
    y = ptr[1];
    index += 4;
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
  }

  void consume_32_32(uint32_t &x, uint32_t &y) {
    uint32_t *ptr = &data[index];
    x = ptr[0];
    y = ptr[1];
    index += 2;
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
  }

  void consume_32_64(uint32_t &x, uint64_t &y) {
    uint32_t *ptr = &data[index];
    x = ptr[0];
    y = *(uint64_t *) &ptr[1];
    index += 3;
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
  }

  void consume_32_32_64(uint32_t &x, uint32_t &y, uint64_t &z) {
    uint32_t *ptr = &data[index];
    x = ptr[0];
    y = ptr[1];
    z = *(uint64_t *) &ptr[2];
    index += 4;
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
    // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_NTA);
  }
};
#endif /* SW_QUEUE_H */
