#ifndef NQ_H
#define NQ_H

#include <stdint.h>

// A simple software queue library
// requires shared memory.
// Assumes one consumer and one
// producer per queue.
// Works on 32-bit


// size of a cacheline, in bytes
#define CACHELINE_SIZE        (128)

// number of cachelines per chunk
#define CHUNK_MULTIPLIER      (32)

typedef uint64_t              ValueType;

// the number of ValueType that fit in a chunk
#define CHUNK_SIZE            ( ( CHUNK_MULTIPLIER * CACHELINE_SIZE - sizeof(size_t) ) / sizeof(ValueType) )

// a chuck of elements.
// Fits in exactly one cacheline
typedef struct s_chunk {
  size_t                      fill;
  ValueType                   elts[ CHUNK_SIZE ];
} Chunk;

// A cacheline-sized pointer holder.
// This is the only data structure shared
// between producer and consumer.
// In the common case, producer only
// deals with his chunk, and the consumer
// only deals with his own chunk.
//
// There is no cache contention between
// producer and consumer except when
// a chunk is being passed through
// the pathway.
typedef struct s_pathway {
  Chunk * volatile            chunk;

  // fill my cacheline, so I don't accidentally
  // share it with anything else
  char                        padding[ CACHELINE_SIZE - sizeof(Chunk*) ];
} Pathway;

// interface for the consumer to a chunk.
// This is effectively a private cacheline
// that contains a reference to a chunk
// which has reached the consumer, and
// which the producer can no longer touch.
typedef struct s_consumer {
  Chunk *                     chunk;
  size_t                      offset;
  Pathway *                   pathway;

  // fill my cacheline, so I don't accidentally
  // share it with anything else
  char                        padding[ CACHELINE_SIZE - sizeof(Chunk*) - sizeof(size_t) - sizeof(Pathway*) ];
} Consumer;

// interface for the producer to a chunk.
// This is effectively a private cacheline
// that contains a reference to a chunk
// which the producer is still filling,
// and which the consumer cannot yet touch.
typedef struct s_producer {
  unsigned                    hasFlushedSinceLastCheck;
  Chunk *                     chunk;
  Pathway *                   pathway;

  // fill my cacheline, so I don't accidentally
  // share it with anything else
  char                        padding[ CACHELINE_SIZE - sizeof(Chunk*) - sizeof(Pathway*) - sizeof(unsigned)];
} Producer;



// Functions


Consumer * nq_new_consumer(void);
Producer * nq_new_producer(Consumer *cons);

void nq_delete_consumer(Consumer *cons);
void nq_delete_producer(Producer *prod);

unsigned nq_select_producer(unsigned prevChoice, Producer **producers, unsigned numProducers);
unsigned nq_select_consumer(unsigned prevChoice, Consumer **consumers, unsigned numConsumers);

void nq_produce(Producer *prod, ValueType value);
void nq_flush(Producer *prod);
ValueType nq_consume(Consumer *cons);


#endif

