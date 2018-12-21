#include <stdlib.h>
#include <unistd.h>

#include "nq.h"

//#define DEBUG

#ifdef DEBUG
#include <stdio.h>
#endif


Consumer *nq_new_consumer() {
  Consumer *cons = (Consumer*) malloc( sizeof(Consumer) );
  cons->chunk = 0;
  cons->offset = 0;

  // Allocate a pathway;
  // This cacheline will bounce back
  // and forth between producer and
  // consumer once per chunk transferred.
  cons->pathway = (Pathway*) malloc( sizeof(Pathway) );
  cons->pathway->chunk = 0;

  return cons;
}

Producer *nq_new_producer(Consumer *cons) {
  Producer *prod = (Producer*) malloc( sizeof(Producer) );

  prod->hasFlushedSinceLastCheck = 0;
  prod->chunk = (Chunk*) malloc( sizeof(Chunk) );
  prod->chunk->fill = 0;

  // Producer and consumer share a pathway.
  // In the common case, neither touches it.
  prod->pathway = cons->pathway;

  return prod;
}

void nq_delete_producer(Producer *prod) {
  if( prod->chunk )
    free(prod->chunk);
  free(prod);
}

void nq_delete_consumer(Consumer *cons) {
  if( cons->chunk )
    free( cons->chunk );
  if( cons->pathway->chunk )
    free( cons->pathway->chunk );
  free( cons->pathway );
  free( cons );
}

void nq_produce(Producer *prod, ValueType value) {

#ifdef DEBUG
  printf("nq_produce\n");
#endif

  prod->chunk->elts[ prod->chunk->fill ++ ] = value;

  // Uncommon case: chunk is full
  if( prod->chunk->fill == CHUNK_SIZE )
    nq_flush(prod);
}

void nq_flush(Producer *prod) {
#ifdef DEBUG
  printf("nq_flush\n");
#endif

  if( prod->chunk->fill == 0 )
    return;

  prod->hasFlushedSinceLastCheck = 1;

  // spin lock until the pathway
  // can accept more
  while( prod->pathway->chunk != 0 )
    usleep(5);

  // Send the chunk.
  prod->pathway->chunk = prod->chunk;

  // Allocate a new output buffer
  prod->chunk = (Chunk*) malloc( sizeof(Chunk) );
  prod->chunk->fill = 0;
}

ValueType nq_consume(Consumer *cons) {
#ifdef DEBUG
  printf("nq_consume\n");
#endif



  // Uncommon case: If our input is empty
  if( cons->chunk == 0 || cons->offset == cons->chunk->fill ) {

    // free the old chunk, if any
    if( cons->chunk )
      free( cons->chunk );

    // spin lock until the pathway has something
    while( cons->pathway->chunk == 0 )
      usleep(5);

    // grab the chunk from the pathway
    cons->chunk = cons->pathway->chunk;

    // start reading from the beginning
    cons->offset = 0;

    // free the pathway for future chunks
    cons->pathway->chunk = 0;
  }

  // common case: pull from our local buffer
  return cons->chunk->elts[ cons->offset ++ ];
}

unsigned nq_select_producer(unsigned c, Producer **producers, unsigned numProducers) {
  if( ++c >= numProducers )
    c = 0;
  return 0;

  /*
  if( c >= numProducers )
    c = 0;

  while( producers[c]->hasFlushedSinceLastCheck ) {
    producers[c]->hasFlushedSinceLastCheck = 0;
    c = (c + 1) % numProducers;
  }

  return c;
  */
}

unsigned nq_select_consumer(unsigned c, Consumer **consumers, unsigned numProducers) {
  if( ++c >= numProducers )
    c = 0;
  return 0;

  /*
  if( c >= numProducers )
    c = 0;

  unsigned i;
  for(i=0; i<numProducers; ++i) {
    // Would reading from this queue block?
    if( (consumers[c]->chunk == 0 || consumers[c]->offset >= consumers[c]->chunk->fill)
    &&   consumers[c]->pathway->chunk == 0 ) {
      // Yes, it would block; look at the next one
      c = (c + 1) % numProducers;

    } else {
      // No, select this one
      break;
    }
  }

  return c;
  */
}


