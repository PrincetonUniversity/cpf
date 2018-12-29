#ifndef SLAMPLIB_HOOKS_SLAMP_BOUND_MALLOC_H
#define SLAMPLIB_HOOKS_SLAMP_BOUND_MALLOC_H

namespace slamp 
{

void init_bound_malloc(void* heap_bound);
void fini_bound_malloc();

size_t get_object_size(void* ptr);

void* bound_malloc(size_t size);
void  bound_free(void* ptr);
void* bound_calloc(size_t num, size_t size);
void* bound_realloc(void* ptr, size_t size);
void  bound_discard_page();

}

#endif
