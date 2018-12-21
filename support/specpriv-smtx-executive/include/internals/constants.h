#include "types.h"

// Unit of chunk-level operations. Not necessarilly match the linux page size
#define PAGE_SIZE  (4*1024)
#define PAGE_SHIFT (12)
#define PAGE_MASK  (0xFFFFFFFFFFFFF000L)

// address translation between original address and shadow address
#define SHADOW_MASK (0x0000200000000000L)
#define GET_SHADOW_OF(x) ( (uint64_t)(x)^SHADOW_MASK )
#define GET_ORIGINAL_OF(x) ( (uint64_t)(x)^SHADOW_MASK )

// dedicated range for versioned mallocs
#define VER_MALLOC_CHUNK_BEGIN (0x100000000000L)
#define VER_MALLOC_CHUNK_BOUND (0x180000000000L) // 4G
#define VER_MALLOC_CHUNKSIZE   (0x1000) // 4K

// dedicated range for specialized heaps
#define SEPARATION_HEAP_BEGIN     (0x080000000000L)
#define SEPARATION_HEAP_BOUND     (0x100000000000L)
#define SEPARATION_HEAP_CHUNKSIZE (0x1000) // 4K

// dedicate range for versioned-specialied heaps
#define VER_SEPARATION_HEAP_BEGIN (0x180000000000L)
#define VER_SEPARATION_HEAP_BOUND (0x200000000000L)

// code for memory operations
#define READ  (0)
#define WRITE (1)

// code for chunk checks
#define CHECK_FREE     (0)
#define CHECK_REQUIRED (1)
#define CHECK_RO_PAGE  (2)

// code for ALLOC broadcast
#define REGULAR    (0)
#define SEPARATION (1)

// code for packets
#define NORMAL  (0)
#define SUPER   (1)
#define BOI     (2)
#define EOI     (3)
#define MISSPEC (4)
#define EOW     (5)
#define ALLOC   (6)
#define FREE    (7)

// size of packet pools
#define PACKET_POOL_SIZE (8192*2*4*8)

// alignment
#define ALIGNMENT (64)

// Number of processors; used to schedule affinities.
// #define NUM_PROCS (24)
#define OS2PHYS (0)

// maximum number of stages
#define MAX_STAGE_NUM (64)

// maximum number of workers
#define MAX_WORKERS (32-1)

// number of auxiliary worker process required to support the parallelization strategy
//#define NUM_AUX_WORKERS (1)

// code for specific workers
#define MAIN_PROCESS_WID (~(Wid)0)
#define NOT_A_PARALLEL_STAGE (~(Wid)0)

// round-up/-down to a power of two
#define ROUND_DOWN(n,k)   ( (~((k)-1)) & (uint64_t) (n) )
#define ROUND_UP(n,k)     ROUND_DOWN( (n) + ((k) - 1), (k))

// prefixing function names
#define CAT(a,b) a##b
#define PREFIX(x) CAT(__specpriv_, x)

// shadow tracking for stack?
#define  HANDLE_STACK 1

// debug on and off
#define  DEBUG_ON 0
//#define  DEBUG_ON 1
#define  MEMDBG 0
#define  CTXTDBG 0

// performance profile
#define PROFILE 0
#define PROFILE_MEMOPS 0
#define PROFILE_WEIGHT 0

#define SEQMODE 0
