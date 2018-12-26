#ifndef LIBERTY_PUREIO_TV_H
#define LIBERTY_PUREIO_TV_H

#include <stdint.h>
#include <stdarg.h>

#include "types.h"

struct s_time_vector
{
  uint32_t      length;
  uint32_t      indices[1];
};

//------------------------------------------------------------------------
// Methods for manipulating Time Vectors
// And time vector is a vector of integers representing 'hierarchical' time.
// They are ordered by lexicographic ordering.

int compare_tv_lte(Time *a, Time *b);
int is_all_zero_indices_from(Time *a, uint32_t first);
int tv_are_adjacent(Time *a, Time *b);
void free_tv(Time *tv);
Time * clone_time(Time *tv);

void print_time(Time *time);


#endif

