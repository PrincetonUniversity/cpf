#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tv.h"


int compare_tv_lte(Time *a, Time *b)
{
  uint32_t i;
  for(i=0;;++i)
  {
    if( i == a->length && i == b->length )
      return 1;
    else if( i == a->length )
      return 0;
    else if( i == b->length )
      return 1;

    if( a->indices[i] > b->indices[i] )
      return 0;
    if( a->indices[i] < b->indices[i] )
      return 1;
  }

  return 1;
}

int is_all_zero_indices_from(Time *a, uint32_t first)
{
  uint32_t i;
  for(i=first; i<a->length; ++i)
    if( a->indices[i] != 0 )
      return 0;
  return 1;
}

int tv_are_adjacent(Time *a, Time *b)
{
  if( a == 0 )
    return is_all_zero_indices_from(b,0);

  uint32_t i;

  for(i=0;;++i)
  {
    if( i == a->length && is_all_zero_indices_from(b,i) )
      return 1;
    else if( i == b->length )
      return 0;
    else if( i == a->length - 1
    &&       a->indices[i] + 1 == b->indices[i]
    &&       is_all_zero_indices_from(b,i+1) )
      return 1;
    else if( a->indices[i] != b->indices[i] )
      return 0;
  }

  return 1;
}

void free_tv(Time *tv)
{
  if( tv )
    free( tv );
}

Time * clone_time(Time *tv)
{
  if( !tv )
    return 0;

  const unsigned sz = (1 + tv->length) * sizeof(uint32_t);
  Time *t = (Time*) malloc( sz );
  memcpy(t, tv, sz );

  return t;
}

void print_time(Time *time)
{
  uint32_t i;
  fprintf(stderr, "[");

  if( time == 0 )
    fprintf(stderr, "nil");
  else
    for(i=0; i<time->length; ++i)
      fprintf(stderr,"%d ", time->indices[i]);
  fprintf(stderr, "]");
}


