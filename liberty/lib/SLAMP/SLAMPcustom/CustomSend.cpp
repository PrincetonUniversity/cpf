#include "slamp_hooks.h"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/detail/os_file_functions.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include "malloc.h"

#include <unistd.h>
#include <xmmintrin.h>
#include "sw_queue_astream.h"

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

namespace bip = boost::interprocess;

static unsigned long counter_load = 0;
static unsigned long counter_store = 0;
static unsigned long counter_ctx = 0;
static unsigned long counter_alloc = 0;
// char local_buffer[LOCAL_BUFFER_SIZE];
// unsigned buffer_counter = 0;
static bool onProfiling = false;

static void *(*old_malloc_hook)(size_t, const void *);
static void *(*old_realloc_hook)(void *, size_t, const void *);
static void (*old_free_hook)(void *, const void *);
static void *(*old_memalign_hook)(size_t, size_t, const void *);
// create segment and corresponding allocator
bip::fixed_managed_shared_memory *segment;
bip::fixed_managed_shared_memory *segment2;
static SW_Queue the_queue;
static double_queue_p dqA, dqB, dq, dq_other;
static uint64_t dq_index = 0;
static uint64_t *dq_data;
// static uint64_t total_pushed = 0;
static uint64_t total_swapped = 0;
static void swap(){
  if(dq == dqA){
    dq = dqB;
    dq_other = dqA;
  }else{
    dq = dqA;
    dq_other = dqB;
  }
  dq_data = dq->data;
}

static void produce_wait() ATTRIBUTE(noinline){
  dq->size = dq_index;
  dq->ready_to_read = true;
  dq->ready_to_write = false;
  while (!dq_other->ready_to_write){
    // spin
    usleep(10);
  }
  swap();
  dq->ready_to_read = false;
  dq_index = 0;
  total_swapped++;
}

static void produce(uint64_t x) ATTRIBUTE(noinline) {
  if (dq_index == QSIZE){
    produce_wait();
  }
  dq_data[dq_index] = x;
  dq_index++;
  // total_pushed++;
  // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
}

static void produce_2(uint64_t x, uint64_t y) ATTRIBUTE(noinline) {
  if (dq_index + 2 >= QSIZE){
    produce_wait();
  }
  dq_data[dq_index] = x;
  dq_data[dq_index+1] = y;
  dq_index += 2;
  // total_pushed += 2;
  // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
}

static void produce_3(uint64_t x, uint64_t y, uint64_t z) ATTRIBUTE(noinline) {
  if (dq_index + 3 >= QSIZE){
    produce_wait();
  }
  // dq_data[0] = x;
  // dq_data[1] = y;
  // dq_data[2] = z;

  _mm_stream_pi((__m64*)&dq_data[dq_index], (__m64)x);
  _mm_stream_pi((__m64*)&dq_data[dq_index+1], (__m64)y);
  _mm_stream_pi((__m64*)&dq_data[dq_index+2], (__m64)z);
  // dq_data[dq_index] = x;
  // dq_data[dq_index+1] = y;
  // dq_data[dq_index+2] = z;
  dq_index += 3;
  // total_pushed += 3;
  // _mm_prefetch(&dq_data[dq_index] + QPREFETCH, _MM_HINT_T0);
}

static void produce_4(uint64_t x, uint64_t y, uint64_t z, uint64_t w) {// ATTRIBUTE(always_inline) {
  if (dq_index + 4 >= QSIZE){
    produce_wait();
  }
  dq_data[dq_index] = x;
  dq_data[dq_index+1] = y;
  dq_data[dq_index+2] = z;
  dq_data[dq_index+3] = w;
  dq_index += 4;
  // total_pushed += 4;
}

// #define CONSUME         sq_consume(the_queue);
// #define PRODUCE(x)      sq_produce(the_queue,(uint64_t)x);
#define PRODUCE(x)      produce((uint64_t)x);

#define COMBINE_2_32(x,y)   ((((uint64_t)x)<<32) | (uint32_t)(y)) 
#define CONSUME_2(x,y)  do { uint64_t tmp = CONSUME; x = (uint32_t)(tmp>>32); y = (uint32_t) tmp; } while(0)

#define TURN_ON_HOOKS \
  __malloc_hook = SLAMP_malloc_hook;\
  __realloc_hook = SLAMP_realloc_hook;\
  __memalign_hook = SLAMP_memalign_hook;


#define TURN_OFF_HOOKS \
  __malloc_hook = old_malloc_hook; \
  __realloc_hook = old_realloc_hook; \
  __memalign_hook = old_memalign_hook;

// Ringbuffer fully constructed in shared memory. The element strings are
// also allocated from the same shared memory segment. This vector can be
// safely accessed from other processes.

enum DepModAction: char
{
    INIT = 0,
    LOAD,
    STORE,
    ALLOC,
    LOOP_INVOC,
    LOOP_ITER,
    FINISHED
};

void SLAMP_init(uint32_t fn_id, uint32_t loop_id) {

  segment = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *4, (void*)(1UL << 32));
  // segment2 = new bip::fixed_managed_shared_memory(bip::open_or_create, "MySharedMemory2", sizeof(uint64_t) *QSIZE *2, (void*)(1UL << 28));
  
  dqA = segment->find<double_queue_t>("DQ_A").first;
  dqB = segment->find<double_queue_t>("DQ_B").first;
  dq = dqA;
  dq_other = dqB;
  dq_index = 0;
  dq_data = dq->data;

    // managed_shared_memory(bip::open_or_create, "MySharedMemory", sizeof(uint64_t) *QSIZE *2);
  // auto a_queue = new atomic_queue::AtomicQueueB<shm::Element, shm::char_alloc, shm::NIL>(65536);
  
  // the_queue = static_cast<SW_Queue>(segment->find_or_construct<sw_queue_t>("MyQueue")());
  // auto data = static_cast<uint64_t*>(segment2->find_or_construct<uint64_t>("smtx_queue_data")[QSIZE]());
  // if (the_queue == nullptr) {
    // std::cout << "Error: could not create queue" << std::endl;
    // exit(-1);
  // }
  // the_queue->data = data;
  // if (the_queue->data == nullptr) {
    // std::cout << "Error: could not create queue data" << std::endl;
    // exit(-1);
  // }

  // [> Initialize the queue data structure <]
  // the_queue->p_data = (uint64_t) the_queue->data;
  // the_queue->c_inx = 0;
  // the_queue->c_margin = 0;
  // the_queue->p_glb_inx = 0;
  // the_queue->c_glb_inx = 0;
  // the_queue->ptr_c_glb_inx = &(the_queue->c_glb_inx);
  // the_queue->ptr_p_glb_inx = &(the_queue->p_glb_inx);
  // // local_buffer = new LocalBuffer(queue);

  // // send a msg with "fn_id, loop_id"
  // // char msg[100];
  // // sprintf(msg, "%d,%d", fn_id, loop_id);
  // // queue->push(shm::shared_string(msg, *char_alloc));

  // local_buffer->push(INIT);
  // local_buffer->push(loop_id);
  uint32_t pid = getpid();
  printf("SLAMP_init: %d, %d, %d\n", fn_id, loop_id, pid);
  // local_buffer->push(pid);
  produce_3(INIT, loop_id, pid);

  auto allocateLibcReqs = [](void *addr, size_t size) {
    produce_3(ALLOC, (uint64_t)addr, size);
  };

  allocateLibcReqs((void*)&errno, sizeof(errno));
  allocateLibcReqs((void*)&stdin, sizeof(stdin));
  allocateLibcReqs((void*)&stdout, sizeof(stdout));
  allocateLibcReqs((void*)&stderr, sizeof(stderr));
  allocateLibcReqs((void*)&sys_nerr, sizeof(sys_nerr));

  const unsigned short int* ctype_ptr = (*__ctype_b_loc()) - 128;
  allocateLibcReqs((void*)ctype_ptr, 384 * sizeof(*ctype_ptr));
  const int32_t* itype_ptr = (*__ctype_tolower_loc()) - 128;
  allocateLibcReqs((void*)itype_ptr, 384 * sizeof(*itype_ptr));
  itype_ptr = (*__ctype_toupper_loc()) - 128;
  allocateLibcReqs((void*)itype_ptr, 384 * sizeof(*itype_ptr));

  old_malloc_hook = __malloc_hook;
  // old_free_hook = __free_hook;
  old_memalign_hook = __memalign_hook;
  old_realloc_hook = __realloc_hook;
  __malloc_hook = SLAMP_malloc_hook;
  __free_hook = nullptr;
  __realloc_hook = SLAMP_realloc_hook;
  // __free_hook = SLAMP_free_hook;
  __memalign_hook = SLAMP_memalign_hook;
}

void SLAMP_fini(const char* filename){
  // send a msg with "fini"
  // queue->push(shm::shared_string("fini", *char_alloc));
  std::cout << counter_load << " " << counter_store << " " << counter_ctx << std::endl;
  // local_buffer->push(FINISHED);
  // local_buffer->flush();
  PRODUCE(FINISHED);
  // sq_flushQueue(the_queue);
  dq->size = dq_index;
  dq->ready_to_read = true;
}

void SLAMP_allocated(uint64_t addr){}
void SLAMP_init_global_vars(const char *name, uint64_t addr, size_t size){
  // local_buffer->push(ALLOC)->push(addr)->push(size);
  produce_3(ALLOC, addr, size);
}
void SLAMP_main_entry(uint32_t argc, char** argv, char** env){}

void SLAMP_enter_fcn(uint32_t id){}
void SLAMP_exit_fcn(uint32_t id){}
void SLAMP_enter_loop(uint32_t id){}
void SLAMP_exit_loop(uint32_t id){}
void SLAMP_loop_iter_ctx(uint32_t id){}
void SLAMP_loop_invocation(){
  // send a msg with "loop_invocation"
  // local_buffer->push(LOOP_INVOC);
  PRODUCE(LOOP_INVOC);

  counter_ctx++;
  onProfiling = true;
}
void SLAMP_loop_iteration(){
  // local_buffer->push(LOOP_ITER);
  PRODUCE(LOOP_ITER);
  counter_ctx++;
}

void SLAMP_loop_exit(){
  onProfiling = false;
}

void SLAMP_report_base_pointer_arg(uint32_t, uint32_t, void *ptr){}
void SLAMP_report_base_pointer_inst(uint32_t, void *ptr){}
void SLAMP_callback_stack_alloca(uint64_t, uint64_t, uint32_t, uint64_t){}
void SLAMP_callback_stack_free(){}

void SLAMP_ext_push(const uint32_t instr){}
void SLAMP_ext_pop(){}

void SLAMP_push(const uint32_t instr){}
void SLAMP_pop(){}

void SLAMP_load(const uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value) ATTRIBUTE(always_inline) {
  // send a msg with "load, instr, addr, bare_instr, value"
  // char msg[100];
  // sprintf(msg, "load,%d,%lu,%d,%lu", instr, addr, bare_instr, value);
  // queue->push(shm::shared_string(msg, *char_alloc));
  //
  if (onProfiling) {
    // local_buffer->push(LOAD)->push(instr)->push(addr)->push(bare_instr); //->push(value);
    produce_3(LOAD, COMBINE_2_32(instr, bare_instr), addr);
    // // if (counter_load % 10000000 == 0) {
    //   // printf("load: %lu\n", counter_load);
    // // }
    // // if (counter_load >= 113957060 && counter_load <= 113957064) {
    //   // printf("SLAMP_load: %d, %lu, %d, %lu\n", instr, addr, bare_instr, value);
    //   // printf("counter load %d\n", counter_load);
    //   // printf("counter store%d\n", counter_store);
    //   // printf("dq_data[%ld - 4] = %lu at addr %p\n", dq_index, dq_data[dq_index - 4], &dq_data[dq_index - 4]);
    //   // printf("dq_data[%ld - 3] = %lu at addr %p\n", dq_index, dq_data[dq_index - 3], &dq_data[dq_index - 3]);
    //   // printf("dq_data[%ld - 2] = %lu at addr %p\n", dq_index, dq_data[dq_index - 2], &dq_data[dq_index - 2]);
    //   // printf("dq_data[%ld - 1] = %lu at addr %p\n", dq_index, dq_data[dq_index - 1], &dq_data[dq_index - 1]);
    //   // printf("Total pushed: %ld\n", total_pushed);
    //   // printf("Total swapped: %ld\n", total_swapped);
    // // }

    // if (dq_index > 2087456 && dq_index < 2087464) {
    //   // if (counter_load > 113957060 && counter_load < 113957064)
    //     // printf("SLAMP_load: %d, %lu, %d, %lu\n", instr, addr, bare_instr, value);
    //   // if (counter_load == 113957064) {
    //     // printf("SLAMP_load: %d, %lu, %d, %lu\n", instr, addr, bare_instr, value);
    //     // printf("counter load %d\n", counter_load);
    //   // }
    //   // printf("dq_index: %d\n", dq_index);
    //   // printf("dq: %p\n", dq);
    //   // printf("counter load %d\n", counter_load);
    //   if (instr == 12356 ) {
    //     printf("SLAMP_load: %d, %lu, %d, %lu\n", instr, addr, bare_instr, value);
    //     printf("counter load %d\n", counter_load);
    //     printf("Total pushed: %ld\n", total_pushed);
    //     printf("Total swapped: %ld\n", total_swapped);
    //     // printf("dq_data[%ld] = %lu at addr %p\n", dq_index, dq_data[dq_index], &dq_data[dq_index]);
    //     printf("dq_data[%ld - 4] = %lu at addr %p\n", dq_index, dq_data[dq_index - 4], &dq_data[dq_index - 4]);
    //     printf("dq_data[%ld - 3] = %lu at addr %p\n", dq_index, dq_data[dq_index - 3], &dq_data[dq_index - 3]);
    //     printf("dq_data[%ld - 2] = %lu at addr %p\n", dq_index, dq_data[dq_index - 2], &dq_data[dq_index - 2]);
    //     printf("dq_data[%ld - 1] = %lu at addr %p\n", dq_index, dq_data[dq_index - 1], &dq_data[dq_index - 1]);
    //   }
    // }
    // PRODUCE(value);
    counter_load++;
  }
}

void SLAMP_load1(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load(instr, addr, bare_instr, value);
}
void SLAMP_load2(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load(instr, addr, bare_instr, value);
}
void SLAMP_load4(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load(instr, addr, bare_instr, value);
}

void SLAMP_load8(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load(instr, addr, bare_instr, value);
}

void SLAMP_loadn(uint32_t instr, const uint64_t addr, const uint32_t bare_instr, size_t n){
  SLAMP_load(instr, addr, bare_instr, 0);
}

void SLAMP_load1_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load1(bare_instr, addr, bare_instr, value);
}
void SLAMP_load2_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load2(bare_instr, addr, bare_instr, value);
}
void SLAMP_load4_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load4(bare_instr, addr, bare_instr, value);
}
void SLAMP_load8_ext(const uint64_t addr, const uint32_t bare_instr, uint64_t value){
  SLAMP_load8(bare_instr, addr, bare_instr, value);
}
void SLAMP_loadn_ext(const uint64_t addr, const uint32_t bare_instr, size_t n){
  SLAMP_loadn(bare_instr, addr, bare_instr, n);
}

void SLAMP_store(const uint32_t instr, const uint64_t addr, const uint32_t bare_instr) ATTRIBUTE(always_inline) {
  // send a msg with "store, instr, addr, bare_instr, value"
  // char msg[100];
  // sprintf(msg, "store,%d,%lu,%d,%lu", instr, addr, bare_instr, value);
  // queue->push(shm::shared_string(msg, *char_alloc));
  if (onProfiling) {
    // local_buffer->push(STORE)->push(instr)->push(bare_instr)->push(addr);
    // PRODUCE(instr);
    // PRODUCE(bare_instr);
    produce_3(STORE, COMBINE_2_32(instr, bare_instr), addr);
    // // if (counter_load >= 113957060 && counter_load <= 113957064) {
    // if (counter_store == 32816445){
    //   printf("SLAMP_store: %d, %lu, %d\n", instr, addr, bare_instr);
    //   printf("counter load %d\n", counter_load);
    //   printf("counter store%d\n", counter_store);
    //   printf("dq_data[%ld - 4] = %lu at addr %p\n", dq_index, dq_data[dq_index - 4], &dq_data[dq_index - 4]);
    //   printf("dq_data[%ld - 3] = %lu at addr %p\n", dq_index, dq_data[dq_index - 3], &dq_data[dq_index - 3]);
    //   printf("dq_data[%ld - 2] = %lu at addr %p\n", dq_index, dq_data[dq_index - 2], &dq_data[dq_index - 2]);
    //   printf("dq_data[%ld - 1] = %lu at addr %p\n", dq_index, dq_data[dq_index - 1], &dq_data[dq_index - 1]);
    // }
    counter_store++;
  }
}

void SLAMP_store1(uint32_t instr, const uint64_t addr){
  SLAMP_store(instr, addr, instr);
}

void SLAMP_store2(uint32_t instr, const uint64_t addr){
  SLAMP_store(instr, addr, instr);
}
void SLAMP_store4(uint32_t instr, const uint64_t addr){
  SLAMP_store(instr, addr, instr);
}
void SLAMP_store8(uint32_t instr, const uint64_t addr){
  SLAMP_store(instr, addr, instr);
}
void SLAMP_storen(uint32_t instr, const uint64_t addr, size_t n){
  SLAMP_store(instr, addr, instr);
}

void SLAMP_store1_ext(const uint64_t addr, const uint32_t bare_inst){
  SLAMP_store1(bare_inst, addr);
}
void SLAMP_store2_ext(const uint64_t addr, const uint32_t bare_inst){
  SLAMP_store2(bare_inst, addr);
}
void SLAMP_store4_ext(const uint64_t addr, const uint32_t bare_inst){
  SLAMP_store4(bare_inst, addr);
}
void SLAMP_store8_ext(const uint64_t addr, const uint32_t bare_inst){
  SLAMP_store8(bare_inst, addr);
}
void SLAMP_storen_ext(const uint64_t addr, const uint32_t bare_inst, size_t n){
  SLAMP_storen(bare_inst, addr, n);
}

/* wrappers */
static void* SLAMP_malloc_hook(size_t size, const void *caller){
  TURN_OFF_HOOKS
  void* ptr = malloc(size);
  // local_buffer->push(ALLOC)->push((uint64_t)ptr)->push(size);
  produce_3(ALLOC, (uint64_t)ptr, size);
  // printf("malloc %lu at %p\n", size, ptr);
  counter_alloc++;
  TURN_ON_HOOKS
  return ptr;
}

static void* SLAMP_realloc_hook(void* ptr, size_t size, const void *caller){
  TURN_OFF_HOOKS
  void* new_ptr = realloc(ptr, size);
  // local_buffer->push(REALLOC)->push((uint64_t)ptr)->push((uint64_t)new_ptr)->push(size);
  produce_3(ALLOC, (uint64_t)new_ptr, size);
  // printf("realloc %p to %lu at %p", ptr, size, new_ptr);
  counter_alloc++;
  TURN_ON_HOOKS
  return new_ptr;
}

static void SLAMP_free_hook(void *ptr, const void *caller){
  old_free_hook(ptr, caller);
}
static void* SLAMP_memalign_hook(size_t alignment, size_t size, const void *caller){
  TURN_OFF_HOOKS
  void* ptr = memalign(alignment, size);
  // local_buffer->push(ALLOC)->push((uint64_t)ptr)->push(size);
  produce_3(ALLOC, (uint64_t)ptr, size);

  // printf("memalign %lu at %p\n", size, ptr);
  counter_alloc++;
  TURN_ON_HOOKS
  return ptr;
}
void* SLAMP_malloc(size_t size, uint32_t instr, size_t alignment){
  // void* ptr = malloc(size);
  // // send a msg with "malloc, instr, ptr, size, alignment"
  // // char msg[100];
  // // sprintf(msg, "malloc,%d,%lu,%lu,%lu", instr, ptr, size, alignment);
  // // queue->push(shm::shared_string(msg, *char_alloc));
  // queue->push(ALLOC);
  // queue->push(reinterpret_cast<uint64_t>(ptr));
  // queue->push(size);
  // counter_alloc++;
  // return ptr;
}

void* SLAMP_calloc(size_t nelem, size_t elsize){}
void* SLAMP_realloc(void* ptr, size_t size){}
void* SLAMP__Znam(size_t size){}
void* SLAMP__Znwm(size_t size){}



char* SLAMP_strdup(const char *s1){}
char* SLAMP___strdup(const char *s1){}
void  SLAMP_free(void* ptr){}
void  SLAMP_cfree(void* ptr){}
void  SLAMP__ZdlPv(void* ptr){}
void  SLAMP__ZdaPv(void* ptr){}
int   SLAMP_brk(void *end_data_segment){}
void* SLAMP_sbrk(intptr_t increment){}

/* llvm memory intrinsics */
void SLAMP_llvm_memcpy_p0i8_p0i8_i32(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint32_t sizeBytes){}
void SLAMP_llvm_memcpy_p0i8_p0i8_i64(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint64_t sizeBytes){}
void SLAMP_llvm_memmove_p0i8_p0i8_i32(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint32_t sizeBytes){}
void SLAMP_llvm_memmove_p0i8_p0i8_i64(const uint8_t* dstAddr, const uint8_t* srcAddr, const uint64_t sizeBytes){}
void SLAMP_llvm_memset_p0i8_i32(const uint8_t* dstAddr, const uint32_t len){}
void SLAMP_llvm_memset_p0i8_i64(const uint8_t* dstAddr, const uint64_t len){}

// void SLAMP_llvm_lifetime_start_p0i8(uint64_t size, uint8_t* ptr){}
// void SLAMP_llvm_lifetime_end_p0i8(uint64_t size, uint8_t* ptr){}

/* String functions */
size_t SLAMP_strlen(const char *str){}
char* SLAMP_strchr(char *s, int c){}
char* SLAMP_strrchr(char *s, int c){}
int SLAMP_strcmp(const char *s1, const char *s2){}
int SLAMP_strncmp(const char *s1, const char *s2, size_t n){}
char* SLAMP_strcpy(char *dest, const char *src){}
char* SLAMP_strncpy(char *dest, const char *src, size_t n){}
char* SLAMP_strcat(char *s1, const char *s2){}
char* SLAMP_strncat(char *s1, const char *s2, size_t n){}
char* SLAMP_strstr(char *s1, char *s2){}
size_t SLAMP_strspn(const char *s1, const char *s2){}
size_t SLAMP_strcspn(const char *s1, const char *s2){}
char* SLAMP_strtok(char *s, const char *delim){}
double SLAMP_strtod(const char *nptr, char **endptr){}
long int SLAMP_strtol(const char *nptr, char **endptr, int base){}
char* SLAMP_strpbrk(char *s1, char *s2){}

/* Mem* and b* functions */
void *SLAMP_memset (void *dest, int c, size_t n){}
void *SLAMP_memcpy (void *dest, const void *src, size_t n){}
void *SLAMP___builtin_memcpy (void *dest, const void *src, size_t n){}
void *SLAMP_memmove (void *dest, const void *src, size_t n){}
int   SLAMP_memcmp(const void *s1, const void *s2, size_t n){}
void* SLAMP_memchr(void* ptr, int value, size_t num){}
void* SLAMP___rawmemchr(void* ptr, int value){}

void  SLAMP_bzero(void *s, size_t n){}
void  SLAMP_bcopy(const void *s1, void *s2, size_t n){}

/* IO */
ssize_t SLAMP_read(int fd, void *buf, size_t count){}
int     SLAMP_open(const char *pathname, int flags, mode_t mode){}
int     SLAMP_close(int fd){}
ssize_t SLAMP_write(int fd, const void *buf, size_t count){}
off_t   SLAMP_lseek(int fildes, off_t offset, int whence){}

FILE *  SLAMP_fopen(const char *path, const char *mode){}
FILE *  SLAMP_fopen64(const char *path, const char *mode){}
FILE *  SLAMP_freopen(const char *path, const char *mode, FILE* stream){}
int     SLAMP_fflush(FILE *stream){}
int     SLAMP_fclose(FILE *stream){}
int     SLAMP_ferror(FILE *stream){}
int     SLAMP_feof(FILE *stream){}
long    SLAMP_ftell(FILE *stream){}
size_t  SLAMP_fread(void * ptr, size_t size, size_t nitems, FILE *stream){}
size_t  SLAMP_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream){}
int     SLAMP_fseek(FILE *stream, long offset, int whence){}
void    SLAMP_rewind(FILE *stream){}

int     SLAMP_fgetc(FILE *stream){}
int     SLAMP_fputc(int c, FILE *stream){}
char *  SLAMP_fgets(char *s, int n, FILE *stream){}
int     SLAMP_fputs(const char *s, FILE *stream){}

int     SLAMP_ungetc(int c, FILE *stream){}
int     SLAMP_putchar(int c){}
int     SLAMP_getchar(void){}

int     SLAMP_fileno(FILE *stream){}
char *  SLAMP_gets(char *s){}
int     SLAMP_puts(const char *s){}

int     SLAMP_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout){}
int     SLAMP_remove(const char *path){}

void    SLAMP_setbuf(FILE * stream, char * buf){}
void    SLAMP_setvbuf(FILE * stream, char * buf, int mode, size_t size){}
char * SLAMP_tmpnam(char *s){}
FILE* SLAMP_tmpfile(void){}
char *  SLAMP_ttyname(int fildes){}

FILE *  SLAMP_fdopen(int fildes, const char *mode){}
void    SLAMP_clearerr(FILE *stream){}

int SLAMP_truncate(const char *path, off_t length){}
int SLAMP_ftruncate(int fildes, off_t length){}

int SLAMP_dup(int oldfd){}
int SLAMP_dup2(int oldfd, int newfd){}
int SLAMP_pipe(int filedes[2]){}

int SLAMP_chmod(const char *path, mode_t mode){}
int SLAMP_fchmod(int fildes, mode_t mode){}
int SLAMP_fchown(int fd, uid_t owner, gid_t group){}
int SLAMP_access(const char *pathname, int mode){}
long SLAMP_pathconf(char *path, int name){}
int SLAMP_mkdir(const char *pathname, mode_t mode){}
int SLAMP_rmdir(const char *pathname){}
mode_t SLAMP_umask(mode_t mask){}
int SLAMP_fcntl(int fd, int cmd, struct flock *lock){}

DIR* SLAMP_opendir(const char* name){}
struct dirent* SLAMP_readdir(DIR *dirp){}
struct dirent64* SLAMP_readdir64(DIR *dirp){}
int SLAMP_closedir(DIR* dirp){}

/* Printf */
int SLAMP_printf(const char *format, ...){}
int SLAMP_fprintf(FILE *stream, const char *format, ...){}
int SLAMP_sprintf(char *str, const char *format, ...){}
int SLAMP_snprintf(char *str, size_t size, const char *format, ...){}

int SLAMP_vprintf(const char *format, va_list ap){}
int SLAMP_vfprintf(FILE *stream, const char *format, va_list ap){}
int SLAMP_vsprintf(char *str, const char *format, va_list ap){}
int SLAMP_vsnprintf(char *str, size_t size, const char *format, va_list ap){}

/* Scanf */
int SLAMP_fscanf(FILE *stream, const char *format, ... ){}
int SLAMP_scanf(const char *format, ... ){}
int SLAMP_sscanf(const char *s, const char *format, ... ){}
int SLAMP___isoc99_sscanf(const char *s, const char *format, ... ){}

int SLAMP_vfscanf(FILE *stream, const char *format, va_list ap){}
int SLAMP_vscanf(const char *format, va_list ap){}
int SLAMP_vsscanf(const char *s, const char *format, va_list ap){}

/* Time */
time_t SLAMP_time(time_t *t){}
struct tm *SLAMP_localtime(const time_t *timer){}
struct lconv* SLAMP_localeconv(){}
struct tm *SLAMP_gmtime(const time_t *timer){}
int SLAMP_gettimeofday(struct timeval *tv, struct timezone *tz){}

/* Math */
double SLAMP_ldexp(double x, int exp){}
float  SLAMP_ldexpf(float x, int exp){}
long double SLAMP_ldexpl(long double x, int exp){}
double SLAMP_log10(double x){}
float  SLAMP_log10f(float x){}
long double SLAMP_log10l(long double x){}
double SLAMP_log(double x){}
float SLAMP_logf(float x){}
long double SLAMP_logl(long double x){}

double SLAMP_exp(double x){}
float SLAMP_expf(float x){}
long double SLAMP_expl(long double x){}

double SLAMP_cos(double x){}
float SLAMP_cosf(float x){}
long double SLAMP_cosl(long double x){}
double SLAMP_sin(double x){}
double SLAMP_tan(double x){}
float SLAMP_sinf(float x){}
long double SLAMP_sinl(long double x){}

double SLAMP_atan(double x){}
float SLAMP_atanf(float x){}
long double SLAMP_atanl(long double x){}

double SLAMP_floor(double x){}
float SLAMP_floorf(float x){}
long double SLAMP_floorl(long double x){}
double SLAMP_ceil(double x){}
float SLAMP_ceilf(float x){}
long double SLAMP_ceill(long double x){}

double SLAMP_atan2(double y, double x){}
float SLAMP_atan2f(float y, float x){}
long double SLAMP_atan2l(long double y, long double x){}

double SLAMP_sqrt(double x){}
float SLAMP_sqrtf(float x){}
long double SLAMP_sqrtl(long double x){}

double SLAMP_pow(double x, double y){}
float SLAMP_powf(float x, float y){}
long double SLAMP_powl(long double x, long double y){}

double SLAMP_fabs(double x){}
float SLAMP_fabsf(float x){}
long double SLAMP_fabsl(long double x){}

double SLAMP_modf(double x, double *iptr){}
float SLAMP_modff(float x, float *iptr){}
long double SLAMP_modfl(long double x, long double *iptr){}

double SLAMP_fmod(double x, double y){}

double SLAMP_frexp(double num, int *exp){}
float SLAMP_frexpf(float num, int *exp){}
long double SLAMP_frexpl(long double num, int *exp){}

int SLAMP_isnan(){}

/* MISC */
char *SLAMP_getenv(const char *name){}
int SLAMP_putenv(char* string){}
char *SLAMP_getcwd(char *buf, size_t size){}
char* SLAMP_strerror(int errnum){}
void SLAMP_exit(int status){}
void SLAMP__exit(int status){}
int  SLAMP_link(const char *oldpath, const char *newpath){}
int  SLAMP_unlink(const char *pathname){}
int  SLAMP_isatty(int desc){}
int SLAMP_setuid(uid_t uid){}
uid_t SLAMP_getuid(void){}
uid_t SLAMP_geteuid(void){}
int SLAMP_setgid(gid_t gid){}
gid_t SLAMP_getgid(void){}
gid_t SLAMP_getegid(void){}
pid_t SLAMP_getpid(void){}
int SLAMP_chdir(const char *path){}
int SLAMP_execl(const char *path, const char *arg0, ... /*, (char *)0 */){}
int SLAMP_execv(const char *path, char *const argv[]){}
int SLAMP_execvp(const char *file, char *const argv[]){}
int SLAMP_kill(pid_t pid, int sig){}
pid_t SLAMP_fork(void){}
sighandler_t SLAMP___sysv_signal(int signum, sighandler_t handler){}
pid_t SLAMP_waitpid(pid_t pid, int* status, int options){}
void SLAMP_qsort(void* base, size_t nmemb, size_t size, int(*compar)(const void *, const void *)){}
int SLAMP_ioctl(int d, int request, ...){}
unsigned int SLAMP_sleep(unsigned int seconds){}
char* SLAMP_gcvt(double number, size_t ndigit, char* buf){}
char* SLAMP_nl_langinfo(nl_item item){}

/* Compiler/Glibc Internals */
void SLAMP___assert_fail(const char * assertion, const char * file, unsigned int line, const char * function){}
const unsigned short int **SLAMP___ctype_b_loc(void){}
int SLAMP__IO_getc(_IO_FILE * __fp){}
int SLAMP__IO_putc(int __c, _IO_FILE *__fp){}

int SLAMP___fxstat (int __ver, int __fildes, struct stat *__stat_buf){}
int SLAMP___xstat (int __ver, __const char *__filename, struct stat *__stat_buf){}