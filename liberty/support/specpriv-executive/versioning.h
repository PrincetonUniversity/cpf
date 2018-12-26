/* versioning.
 *
 * This file contains a user-land implementation of copy-on-write page
 * protection, which is achieved through a combination of read-only
 * mappings and segfault handlers.  (It will work on x86-64 linux, but
 * relies on resumption semantics of segfault, which are not guaranteed by
 * POSIX).
 *
 * Basically, there are two collections of physical pages: the 'primary'
 * and the 'seconday'.  Those physical pages are built from the
 * abstractions found in heap.h.  Additionally, we use a PageSet
 * (pageset.h) to maintain information about the state of each page.  We
 * generally do something like this: - Install the primary version at the
 * desired virtual address; - Wait until the application attempts to modify
 * versioned memory, triggering a segfault; - Capture the segfault: -
 * perform copy-on-write or save-before-write (see VersioningType); -
 * update page maps, protections, and meta-data; - Allow the application to
 * resume.
 */
#ifndef LLVM_LIBERTY_SMTX2_VERSIONING_H
#define LLVM_LIBERTY_SMTX2_VERSIONING_H

#include <signal.h>

#include "heap.h"
#include "pageset.h"

enum e_versioning_type
{
  /* Not mapped */
  NoVersioning = 0,

  /* Eager versioning: create a private version for each worker who
   * wants to modify it
   */
  EagerVersioning,

  /* In-place versioning: copy the old contents to a back-up location
   * before each worker wants to modify it.
   */
  InPlaceVersioning
};
typedef enum e_versioning_type VersioningType;

/* A versioned heap comprises two heaps
 * and the logic to switch the page mapping between
 * these two heaps.
 */
struct s_mapped_heap_with_versioning
{
  struct s_mapped_heap_with_versioning * next;

  VersioningType  type;
  MappedHeap      primary_version;
  MappedHeap      secondary_version;
  PageSet *       versioning;
};
typedef struct s_mapped_heap_with_versioning MappedHeapWithVersioning;

struct s_versioning_control
{
  struct sigaction            old_signals;
  MappedHeapWithVersioning *  first_versioned_heap;
};
typedef struct s_versioning_control VersioningControl;

void start_versioning_control(VersioningControl *vc);
void stop_versioning_control(void);

void heap_map_eager_versioning(Heap *original_version, Heap *new_version, PageSet *pset, MappedHeapWithVersioning *mhv);
void heap_map_inplace_versioning(Heap *original_version, Heap *backup_version, PageSet *pset, MappedHeapWithVersioning *mhwv);

void heap_unmap_with_versioning(MappedHeapWithVersioning *mhev);



#endif

