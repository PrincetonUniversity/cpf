#ifndef LLVM_LIBERTY_SPECPRIV_REDUX_H
#define LLVM_LIBERTY_SPECPRIV_REDUX_H

#include "types.h"
#include "heap.h"

// This must perfectly match the reduction types listed in
// include/liberty/SpecPriv/Reduction.h
#define NotReduction  (0)

// Signed/unsigned integer sum
#define Add_i8        (1)
#define Add_i16       (2)
#define Add_i32       (3)
#define Add_i64       (4)

// Floating point sum.
#define Add_f32       (5)
#define Add_f64       (6)

// Signed integer max
#define Max_i8        (7)
#define Max_i16       (8)
#define Max_i32       (9)
#define Max_i64       (10)

// Unsigned integer max
#define Max_u8        (11)
#define Max_u16       (12)
#define Max_u32       (13)
#define Max_u64       (14)

// Floating point max
#define Max_f32       (15)
#define Max_f64       (16)

// Signed integer min
#define Min_i8        (17)
#define Min_i16       (18)
#define Min_i32       (19)
#define Min_i64       (20)

// Unsigned integer min
#define Min_u8        (21)
#define Min_u16       (22)
#define Min_u32       (23)
#define Min_u64       (24)

// Floating point min
#define Min_f32       (25)
#define Min_f64       (26)

typedef struct s_reduction_info ReductionInfo;
struct s_reduction_info
{
  ReductionInfo * next;
  void          * au;
  unsigned        size;
  ReductionType   type;
};

struct s_checkpoint;

// partial <-- reduce(worker,partial)
// where worker, partial are from the same checkpoint-group of iterations.
// Return true if misspeculation happens (impossible?)
Bool __specpriv_distill_worker_redux_into_partial(
  struct s_checkpoint *partial,
  MappedHeap *partial_redux);

// partial <-- reduce(committed,partial)
// where committed comes from an EARLIER checkpoint-group of iterations.
// Return true if misspeculation happens (impossible?)
Bool __specpriv_distill_committed_redux_into_partial(
  struct s_checkpoint *commit, MappedHeap *commit_redux,
  struct s_checkpoint *partial, MappedHeap *partial_redux);

// main <-- reduce(main,committed)
// where main comes from an EARLIER checkpoint-group of iterations.
// Return true if misspeculation happens (impossible?)
Bool __specpriv_distill_committed_redux_into_main(
  struct s_checkpoint *commit,
  MappedHeap *commit_redux);


// If ptr refers to an address within the reduction
// heap, these will read the corresponding initial
// value.
uint8_t __specpriv_read_redux_initial_value_i8(uint8_t *ptr);
uint16_t __specpriv_read_redux_initial_value_i16(uint16_t *ptr);
uint32_t __specpriv_read_redux_initial_value_i32(uint32_t *ptr);
uint64_t __specpriv_read_redux_initial_value_i64(uint64_t *ptr);
float __specpriv_read_redux_initial_value_f32(float *ptr);
double __specpriv_read_redux_initial_value_f64(double *ptr);

// Report a write-to-redux to the runtime.
// Allows the runtime to perform reduction on fewer bytes during a checkpoint.
void __specpriv_init_redux(void);
void __specpriv_reset_redux_range(void);
void __specpriv_redux_write_range(uint8_t *ptr, unsigned size);

#endif

