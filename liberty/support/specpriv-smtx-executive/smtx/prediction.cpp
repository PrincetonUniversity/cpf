#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "internals/debug.h"
#include "internals/constants.h"
#include "internals/control.h"
#include "internals/strategy.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/prediction.h"
#include "internals/smtx/protection.h"
#include "internals/smtx/smtx.h"

#include <sys/mman.h>

namespace specpriv_smtx
{

volatile uint8_t* good_to_go;

struct LoopInvariantBuffer
{
  int64_t* ptr;
  int64_t  value;
  uint8_t  size;
  char     PAD[32-sizeof(uint64_t*)-sizeof(uint64_t)-sizeof(uint8_t)];
};

struct LinearPredictor
{
  int64_t* ptr;
  uint8_t  size;
};

typedef union
{
  int32_t ival;
  float   fval;
} I32OrFloat;

typedef union
{
  int64_t ival;
  double  dval;
} I64OrDouble;

static LoopInvariantBuffer** loop_invariant_buffer;
static LinearPredictor** linear_predictors;
static unsigned num_loop_invariant_loads;
static unsigned num_linear_predictable_loads;
static unsigned num_contexts;

static int64_t** coeff_a;
static int64_t** coeff_b;
static int32_t** is_double;


void PREFIX(init_predictors)(
  unsigned loop_invariant_loads, 
  unsigned linear_predictable_loads,
  unsigned contexts, 
  ...
)
{
  fprintf(stderr, "init_predictors: %u %u %u\n", loop_invariant_loads, linear_predictable_loads, contexts);

  num_loop_invariant_loads = loop_invariant_loads;
  num_linear_predictable_loads = linear_predictable_loads;
  num_contexts = contexts;

  init_loop_invariant_buffer(loop_invariant_loads, contexts); 
  init_linear_predictors(linear_predictable_loads, contexts); 

  va_list op;
  va_start(op, contexts);

  // init prediction coefficients
  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < contexts ; j++)
    {
      coeff_a[i][j] = va_arg(op, int64_t);
    }
  }
  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < contexts ; j++)
    {
      coeff_b[i][j] = va_arg(op, int64_t);
    }
  }
  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < contexts ; j++)
    {
      is_double[i][j] = va_arg(op, int32_t);
    }
  }

  va_end(op);

  // flag for loop invaraint value prediction which is valid from iter 1
  good_to_go = (uint8_t*)mmap(0, sizeof(uint8_t), PROT_WRITE|PROT_READ, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
}

void reset_predictors()
{
  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LoopInvariantBuffer* li = &(loop_invariant_buffer[i][j]);
      li->ptr = NULL;
    }
  }

  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LinearPredictor* lp = &(linear_predictors[i][j]);
      lp->ptr = NULL;
    }
  }
}

void PREFIX(fini_predictors)()
{
  // cleanup loop invariant buffer

  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    munmap(loop_invariant_buffer[i], sizeof(LoopInvariantBuffer) * num_contexts);
  }
  munmap(loop_invariant_buffer, sizeof(LoopInvariantBuffer*) * num_loop_invariant_loads);

  loop_invariant_buffer = NULL;
  num_loop_invariant_loads = 0;

  // cleanup linear prediction buffer
  
  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    munmap(linear_predictors[i], sizeof(LinearPredictor) * num_contexts);
    munmap(coeff_a[i], sizeof(int64_t) * num_contexts);
    munmap(coeff_b[i], sizeof(int64_t) * num_contexts);
    munmap(is_double[i], sizeof(int32_t) * num_contexts);
  }
  munmap(linear_predictors, sizeof(LinearPredictor*) * num_linear_predictable_loads);
  munmap(coeff_a, sizeof(int64_t*) * num_linear_predictable_loads);
  munmap(coeff_b, sizeof(int64_t*) * num_linear_predictable_loads);
  munmap(is_double, sizeof(int32_t*) * num_linear_predictable_loads);

  linear_predictors = NULL;
  coeff_a = NULL;
  coeff_b = NULL;
  is_double = NULL;
  num_linear_predictable_loads = 0;
  num_contexts = 0;

  munmap((void*)good_to_go, sizeof(uint8_t));
  good_to_go = NULL;
}

// loop invariant prediction

void init_loop_invariant_buffer(unsigned loads, unsigned contexts)
{
  loop_invariant_buffer = (LoopInvariantBuffer**)mmap(
      0, 
      sizeof(LoopInvariantBuffer*) * num_loop_invariant_loads, 
      PROT_WRITE | PROT_READ, 
      MAP_SHARED | MAP_ANONYMOUS, 
      -1, 
      0);

  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    loop_invariant_buffer[i] = (LoopInvariantBuffer*)mmap(
        0, 
        sizeof(LoopInvariantBuffer) * num_contexts, 
        PROT_WRITE | PROT_READ, 
        MAP_SHARED | MAP_ANONYMOUS, 
        -1, 
        0);
  }
}

void PREFIX(check_loop_invariant)(
    unsigned load_id, 
    uint64_t context, 
    uint64_t* read_ptr, 
    uint32_t read_size
    )
{
  LoopInvariantBuffer* li = &(loop_invariant_buffer[load_id][context]);
  assert( li );

#if DEBUG_ON
  DBG("read_size: %u li->value: %lx read: %lx\n", read_size, li->value, *read_ptr);
#endif

#if 0
  if (read_size == 1)
  {
    uint8_t* rptr = (uint8_t*)read_ptr;
    if ( *rptr != (uint8_t)(li->value & 0xff) ) 
    {
      PREFIX(misspec)("loop-invariant speculation failed\n");
    }
  }
  if (read_size == 2)
  {
    uint16_t* rptr = (uint16_t*)read_ptr;
    if ( *rptr != (uint16_t)(li->value & 0xffff) ) 
    {
      PREFIX(misspec)("loop-invariant speculation failed\n");
    }
  }
  if (read_size == 4)
  {
    uint32_t* rptr = (uint32_t*)read_ptr;
    if ( *rptr != (uint32_t)(li->value & 0xffffffff) ) 
    {
      PREFIX(misspec)("loop-invariant speculation failed\n");
    }
  }
  if (read_size == 8)
  {
    if ( *read_ptr != (uint64_t)(li->value) ) 
    {
      PREFIX(misspec)("loop-invariant speculation failed\n");
    }
  }
#endif
  if (read_ptr != (uint64_t*)li->ptr) PREFIX(misspec)("loop-invariant speculation failed\n");
}

void PREFIX(register_loop_invariant_buffer)(
  unsigned load_id, 
  uint64_t context, 
  int64_t* read_ptr,
  uint32_t read_size
  )
{
#if CTXTDBG
  DBG("register_loop_inv_buffer, loop_invariant_buffer: %p, load_id %u, context %lu\n", 
    loop_invariant_buffer, load_id, context);
#endif

  LoopInvariantBuffer* li = &(loop_invariant_buffer[load_id][context]);

#if CTXTDBG
  DBG("li %p read_ptr %p read_size %lu\n", li, read_ptr, read_size);
#endif

  li->ptr = read_ptr;
  li->size = (uint8_t)read_size;
}

void update_loop_invariant_buffer()
{
  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LoopInvariantBuffer* li = &(loop_invariant_buffer[i][j]);

      if (li->ptr) 
      {
        DBG("update li buffer: %u %u %p<-%lx\n", i, j, li->ptr, *(li->ptr));
        li->value = *(li->ptr);
      }
    }
  }
}

void update_loop_invariants()
{
  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LoopInvariantBuffer* li = &(loop_invariant_buffer[i][j]);

      if (li->ptr) 
      {
        set_page_rw((void*)((size_t)li->ptr & PAGE_MASK));

        switch(li->size)
        {
          case 1:
          {
            uint8_t* ptr = (uint8_t*)li->ptr;
            (*ptr) = (uint8_t)(li->value & 0xff);
            uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(ptr);
            (*shadow) = (uint8_t)(0x02);
            DBG("update li value: %p<-%lx\n", ptr, (uint8_t)(li->value & 0xff));
            break;
          }
          case 2:
          {
            uint16_t* ptr = (uint16_t*)li->ptr;
            (*ptr) = (uint16_t)(li->value & 0xffff);
            uint16_t* shadow = (uint16_t*)GET_SHADOW_OF(ptr);
            (*shadow) = (uint16_t)(0x0202);

            DBG("update li value: %p<-%lx\n", ptr, (uint16_t)(li->value & 0xffff));
            break;
          }
          case 4:
          {
            uint32_t* ptr = (uint32_t*) li->ptr;
            (*ptr) = (uint32_t)(li->value & 0xffffffff);
            uint32_t* shadow = (uint32_t*)GET_SHADOW_OF(ptr);
            (*shadow) = (uint32_t)(0x02020202);

            DBG("update li value: %p<-%lx\n", ptr, (uint32_t)(li->value & 0xffffffff));
            break;
          }
          case 8:
          {
            uint64_t* ptr = (uint64_t*)li->ptr;
            (*ptr) = (uint64_t)(li->value);
            uint64_t* shadow = (uint64_t*)GET_SHADOW_OF(ptr);
            (*shadow) = (uint64_t)(0x0202020202020202L);

            DBG("update li value: %p<-%lx\n", ptr, li->value);
            break;
          }
          default:
            assert(false);
        }
      }
    }
  }
}

void update_shadow_for_loop_invariants()
{
  DBG("update_shadow_for_loop_invariants\n");

  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LoopInvariantBuffer* li = &(loop_invariant_buffer[i][j]);

      if (!li->ptr) continue;

      uint8_t* ptr = (uint8_t*)(li->ptr);
      uint8_t* shadow = (uint8_t*)GET_SHADOW_OF((uint64_t)ptr);

      DBG("update_shadow_for_loop_invariants[%u][%u], li: %p, ptr: %p, size: %d\n", i, j, li, ptr, li->size);

      for (unsigned k = 0 ; k < li->size ; k++)
      {
        shadow[k] = shadow[k] & 0xfe;
      }
    }
  }
}

bool verify_loop_invariants()
{
  DBG("verify loop invariant buffer\n");

  Wid my_try_commit_id = PREFIX(my_worker_id)() - try_commit_begin;

  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LoopInvariantBuffer* li = &(loop_invariant_buffer[i][j]);

      if (!li->ptr) continue;
      if ( ( ((size_t)(li->ptr) >> PAGE_SHIFT) % num_aux_workers ) != my_try_commit_id ) continue;

      switch(li->size) 
      {
        case 1:
        {
          uint8_t* ptr = (uint8_t*)li->ptr;
          uint8_t  val = *ptr;
          
          if ( val != (uint8_t)(li->value & 0xff) )
          {
            DBG("loop invariant buffer verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n", ptr, li->size, val, li->value & 0xff);
            return false;
          }

          break;
        }
        case 2:
        {
          uint16_t* ptr = (uint16_t*)li->ptr;
          uint16_t  val = *ptr;
          
          if ( val != (uint16_t)(li->value & 0xffff) )
          {
            DBG("loop invariant buffer verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n", ptr, li->size, val, li->value & 0xffff);
            return false;
          }

          break;
        }
        case 4:
        {
          uint32_t* ptr = (uint32_t*) li->ptr;
          uint32_t  val = *ptr;
          
          if ( val != (uint32_t)(li->value & 0xffffffff) )
          {
            DBG("loop invariant buffer verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n", ptr, li->size, val, li->value & 0xffffffff);
            return false;
          }

          break;
        }
        case 8:
        {
          uint64_t* ptr = (uint64_t*)li->ptr;
          uint64_t  val = *ptr;
          
          if ( val != (uint64_t)(li->value) )
          {
            DBG("loop invariant buffer verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n", ptr, li->size, val, li->value);
            return false;
          }

          break;
        }
        default:
          DBG("Error: loop invaraint size is not 1, 2, 4, or 8\n");
          return false;
      }
    }
  }

  return true;
}

void try_commit_update_loop_invariants(int8_t* addr, int8_t* data, int8_t* shadow, size_t size)
{
  Wid my_try_commit_id = PREFIX(my_worker_id)() - try_commit_begin;

  for (unsigned i = 0 ; i < num_loop_invariant_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LoopInvariantBuffer* li = &(loop_invariant_buffer[i][j]);

      if (!li->ptr) continue;
      if ( ( ((size_t)(li->ptr) >> PAGE_SHIFT) % num_aux_workers ) != my_try_commit_id ) continue;
      if ( (int8_t*)li->ptr < addr || (int8_t*)li->ptr >= (addr+size) ) continue;

      assert( ((size_t)li->ptr + li->size) <= ((size_t)addr + size) );

      uint64_t off = (uint64_t)li->ptr - (uint64_t)addr;

      for (unsigned k = 0 ; k < li->size ; k++)
      {
        if ( shadow[off+k] & 0x2 ) 
        {
          addr[off+k] = data[off+k];
        }
      }
    }
  }
}

// linear prediction

void PREFIX(check_and_register_linear_predictor)(
  bool     valid,
  unsigned load_id, 
  uint64_t context, 
  int64_t* read_ptr,
  uint32_t read_size
  )
{
  if (!valid) return;

  Iteration iteration = PREFIX(current_iter)();
  LinearPredictor* lp = &(linear_predictors[load_id][context]);

  if (iteration) {
    if (read_ptr != lp->ptr) PREFIX(misspec)("loop-invariant speculation failed\n");
#if 0
    // see update_linear_predicted_values
    iteration += 1;

    // validate

    int64_t a = coeff_a[load_id][context];
    int64_t b = coeff_b[load_id][context];
    int32_t d = is_double[load_id][context];

    int64_t iprediction = a * iteration + b;

    I64OrDouble va, vb;
    va.ival = a;
    vb.ival = b;

    double   dprediction = va.dval * iteration + vb.dval;
    uint64_t read_val = (uint64_t)(*read_ptr);

    switch(read_size) 
    {
      case 1:
        {
          assert( !d );    

          if ( (uint8_t)read_val != (uint8_t)(iprediction & 0xff) )
          {
            DBG("linear predictor verification failed, load_id: %u, context: %u, size: %u, val: %lx, pred: %lx\n",
                load_id, context, read_size, read_val, iprediction & 0xff);
            PREFIX(misspec)("linear-prediction speculation failed\n");
          }

          break;
        }
      case 2:
        {
          assert( !d );

          if ( (uint16_t)read_val != (uint16_t)(iprediction & 0xffff) )
          {
            DBG("linear predictor verification failed, load_id: %u, context: %u, size: %u, val: %lx, pred: %lx\n",
                load_id, context, read_size, read_val, iprediction & 0xffff);
            PREFIX(misspec)("linear-prediction speculation failed\n");
          }

          break;
        }
      case 4:
        {
          if ( !d )
          {
            if ( (uint32_t)read_val != (uint32_t)(iprediction & 0xffffffff) )
            {
              DBG("linear predictor verification failed, load_id: %u, context: %u, size: %u, val: %lx, pred: %lx\n",
                  load_id, context, read_size, read_val, iprediction & 0xffffffff);
              PREFIX(misspec)("linear-prediction speculation failed\n");
            }
          }
          else
          {
            I32OrFloat v;
            v.fval = (float)dprediction;

            if ( (uint32_t)read_val != (uint32_t)(v.ival & 0xffffffff) )
            {
              DBG("linear predictor verification failed, load_id: %u, context: %u, size: %u, val: %lx, pred: %lx\n",
                  load_id, context, read_size, read_val, iprediction & 0xffffffff);
              PREFIX(misspec)("linear-prediction speculation failed\n");
            }
          }

          break;
        }
      case 8:
        {
          if ( !d )
          {
            if ( read_val != (uint64_t)(iprediction) )
            {
              DBG("linear predictor verification failed, load_id: %u, context: %u, size: %u, val: %lx, pred: %lx\n",
                  load_id, context, read_size, read_val, iprediction);
              PREFIX(misspec)("linear-prediction speculation failed\n");
            }
          }
          else
          {
            I64OrDouble v;
            v.dval = dprediction;

            if ( read_val != (uint64_t)(v.ival) )
            {
              DBG("linear predictor verification failed, load_id: %u, context: %u, size: %u, val: %lx, pred: %lx\n",
                  load_id, context, read_size, read_val, iprediction);
              PREFIX(misspec)("linear-prediction speculation failed\n");
            }
          }

          break;
        }
      default:
        DBG("Error: linear predictor size is not 1, 2, 4, or 8\n");
        PREFIX(misspec)("linear-prediction speculation failed\n");
    }
#endif
  }
  else {
    // register

    lp->ptr = read_ptr;
    lp->size = (uint8_t)read_size;
  }
}

void init_linear_predictors(unsigned loads, unsigned contexts)
{
  linear_predictors = (LinearPredictor**)mmap(
      0, 
      sizeof(LinearPredictor*) * loads, 
      PROT_WRITE | PROT_READ, 
      MAP_SHARED | MAP_ANONYMOUS, 
      -1, 
      0);
  coeff_a = (int64_t**)mmap(
      0, 
      sizeof(int64_t*) * loads, 
      PROT_WRITE | PROT_READ, 
      MAP_PRIVATE | MAP_ANONYMOUS, 
      -1, 
      0);
  coeff_b = (int64_t**)mmap(
      0, 
      sizeof(int64_t*) * loads, 
      PROT_WRITE | PROT_READ, 
      MAP_PRIVATE | MAP_ANONYMOUS, 
      -1, 
      0);
  is_double = (int32_t**)mmap(
      0, 
      sizeof(int32_t*) * loads, 
      PROT_WRITE | PROT_READ, 
      MAP_PRIVATE | MAP_ANONYMOUS, 
      -1, 
      0);

  for (unsigned i = 0 ; i < loads ; i++)
  {
    linear_predictors[i] = (LinearPredictor*)mmap(
        0, 
        sizeof(LinearPredictor) * contexts, 
        PROT_WRITE | PROT_READ, 
        MAP_SHARED | MAP_ANONYMOUS, 
        -1, 
        0);
    coeff_a[i] = (int64_t*)mmap(
        0, 
        sizeof(int64_t) * contexts, 
        PROT_WRITE | PROT_READ, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0);
    coeff_b[i] = (int64_t*)mmap(
        0, 
        sizeof(int64_t) * contexts, 
        PROT_WRITE | PROT_READ, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0);
    is_double[i] = (int32_t*)mmap(
        0, 
        sizeof(int32_t) * contexts, 
        PROT_WRITE | PROT_READ, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0);
  }
}

void update_linear_predicted_values()
{
  Iteration iteration = PREFIX(current_iter)() + 1;

  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LinearPredictor* lp = &(linear_predictors[i][j]);
      DBG("update_linear_predicted_value, lp: %p, ptr: %p\n", lp, lp?lp->ptr:NULL);

      if (lp->ptr) 
      {
        int64_t a = coeff_a[i][j];
        int64_t b = coeff_b[i][j];
        int32_t d = is_double[i][j];

        int64_t ival;
        double  dval;
        if (!d)
        {
          ival = a * iteration + b;
          DBG("update_linear_predicted_value, %ld(a) * %ld(iter) + %ld(b) = %ld\n", a, iteration, b, ival);
        }
        else
        {
          I64OrDouble va, vb;
          va.ival = a;
          vb.ival = b;

          dval = va.dval * iteration + vb.dval;
          DBG("update_linear_predicted_value, %f(a) * %ld(iter) + %f(b) = %f\n", 
            va.dval, iteration, vb.dval, dval);
        }

        set_page_rw((void*)((size_t)lp->ptr & PAGE_MASK));

        switch(lp->size)
        {
          case 1:
          {
            assert( !d );

            uint8_t* ptr = (uint8_t*)lp->ptr;
            (*ptr) = (uint8_t)(ival & 0xff);
            uint8_t* shadow = (uint8_t*)GET_SHADOW_OF(lp->ptr);
            (*shadow) = (uint8_t)0x02;

            break;
          }
          case 2:
          {
            assert( !d );

            uint16_t* ptr = (uint16_t*)lp->ptr;
            (*ptr) = (uint16_t)(ival & 0xffff);
            uint16_t* shadow = (uint16_t*)GET_SHADOW_OF(lp->ptr);
            (*shadow) = (uint16_t)0x0202;

            break;
          }
          case 4:
          {
            uint32_t* ptr = (uint32_t*) lp->ptr;

            if ( !d )
            {
              (*ptr) = (uint32_t)(ival & 0xffffffff);
            }
            else
            {
              I32OrFloat v;
              v.fval = (float)dval;

              (*ptr) = (uint32_t)(v.ival);
            }

            uint32_t* shadow = (uint32_t*)GET_SHADOW_OF(lp->ptr);
            (*shadow) = (uint32_t)0x02020202;

            break;
          }
          case 8:
          {
            uint64_t* ptr = (uint64_t*)lp->ptr;

            if ( !d )
            {
              (*ptr) = (uint64_t)(ival);
            }
            else
            {
              I64OrDouble v;
              v.dval = dval;

              (*ptr) = (uint64_t)(v.ival);
            }

            uint64_t* shadow = (uint64_t*)GET_SHADOW_OF(lp->ptr);
            (*shadow) = (uint64_t)0x0202020202020202L;

            break;
          }
          default:
            assert(false);
        }
      }

    }
  }
}

void update_shadow_for_linear_predicted_values()
{
  DBG("update_shadow_for_loop_invariants\n");

  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LinearPredictor* lp = &(linear_predictors[i][j]);

      if (!lp->ptr) continue;

      uint8_t* ptr = (uint8_t*)(lp->ptr);
      uint8_t* shadow = (uint8_t*)GET_SHADOW_OF((uint64_t)ptr);

      DBG("update_shadow_for_loop_invariants[%u][%u], lp: %p, ptr: %p, size: %d\n", i, j, lp, ptr, lp->size);

      for (unsigned k = 0 ; k < lp->size ; k++)
      {
        if ( (shadow[k]) & 0x1 )
          shadow[k] = (shadow[k] | 0x8);
      }
    }
  }
}

bool verify_linear_predicted_values()
{
  DBG("verify linear predictors\n");

  Wid       my_try_commit_id = PREFIX(my_worker_id)() - try_commit_begin;
  Iteration iteration = PREFIX(current_iter)() + 1;

  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LinearPredictor* lp = &(linear_predictors[i][j]);

      if (!lp->ptr) continue;
      if ( ( ((size_t)(lp->ptr) >> PAGE_SHIFT) % num_aux_workers ) != my_try_commit_id ) continue;

      int64_t a = coeff_a[i][j];
      int64_t b = coeff_b[i][j];
      int32_t d = is_double[i][j];

      int64_t iprediction = a * iteration + b;

      I64OrDouble va, vb;
      va.ival = a;
      vb.ival = b;

      double  dprediction = va.dval * iteration + vb.dval;

      switch(lp->size) 
      {
        case 1:
        {
          assert( !d );    
          
          uint8_t* ptr = (uint8_t*)lp->ptr;
          uint8_t  val = *ptr;
          
          if ( val != (uint8_t)(iprediction & 0xff) )
          {
            DBG("linear predictor verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n",
              ptr, lp->size, val, iprediction & 0xff);
            return false;
          }

          break;
        }
        case 2:
        {
          assert( !d );

          uint16_t* ptr = (uint16_t*)lp->ptr;
          uint16_t  val = *ptr;
          
          if ( val != (uint16_t)(iprediction & 0xffff) )
          {
            DBG("linear predictor verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n",
              ptr, lp->size, val, iprediction & 0xffff);
            return false;
          }

          break;
        }
        case 4:
        {
          uint32_t* ptr = (uint32_t*) lp->ptr;
          uint32_t  val = *ptr;

          if ( !d )
          {
            if ( val != (uint32_t)(iprediction & 0xffffffff) )
            {
              DBG("linear predictor verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n",
                ptr, lp->size, val, iprediction & 0xffffffff);
              return false;
            }
          }
          else
          {
            I32OrFloat v;
            v.fval = (float)dprediction;
            
            if ( val != (uint32_t)(v.ival & 0xffffffff) )
            {
              DBG("linear predictor verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n",
                ptr, lp->size, val, v.ival & 0xffffffff);
              return false;
            }
          }

          break;
        }
        case 8:
        {
          uint64_t* ptr = (uint64_t*)lp->ptr;
          uint64_t  val = *ptr;
         
          if ( !d )
          {
            if ( val != (uint64_t)(iprediction) )
            {
              DBG("linear predictor verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n",
                ptr, lp->size, val, iprediction);
              return false;
            }
          }
          else
          {
            I64OrDouble v;
            v.dval = dprediction;

            if ( val != (uint64_t)(v.ival) )
            {
              DBG("linear predictor verification failed, ptr: %p, size: %u, val: %lx, pred: %lx\n",
                ptr, lp->size, val, v.ival);
              return false;
            }
          }

          break;
        }
        default:
          DBG("Error: linear predictor size is not 1, 2, 4, or 8\n");
          return false;
      }
    }
  }

  return true;
}

void try_commit_update_linear_predicted_values(int8_t* addr, int8_t* data, int8_t* shadow, size_t size)
{
  Wid my_try_commit_id = PREFIX(my_worker_id)() - try_commit_begin;

  for (unsigned i = 0 ; i < num_linear_predictable_loads ; i++)
  {
    for (unsigned j = 0 ; j < num_contexts ; j++)
    {
      LinearPredictor* lp = &(linear_predictors[i][j]);

      if (!lp->ptr) continue;
      if ( ( ((size_t)(lp->ptr) >> PAGE_SHIFT) % num_aux_workers ) != my_try_commit_id ) continue;
      if ( (int8_t*)lp->ptr < addr || (int8_t*)lp->ptr >= (addr+size) ) continue;

      assert( ((size_t)lp->ptr + lp->size) <= ((size_t)addr + size) );

      uint64_t off = (uint64_t)lp->ptr - (uint64_t)addr;

      for (unsigned k = 0 ; k < lp->size ; k++)
      {
        if ( shadow[off+k] & 0x2 ) 
        {
          addr[off+k] = data[off+k];
        }
      }
    }
  }
}

}
