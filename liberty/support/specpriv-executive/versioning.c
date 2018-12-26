#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "versioning.h"

static VersioningControl *__vc;

static void userland_copy_on_write_segfault_handler(int sig, siginfo_t *si, void *ctx)
{
  if( sig == SIGSEGV )
  {
    // fault on a memory location
    void *fault_addr = si->si_addr;

    // round-down to a multiple of page size
    const uint64_t pagesize = getpagesize();
    uint64_t page_addr = (uint64_t) fault_addr;
    page_addr &= ~( pagesize - 1 );

    // Does this address belong to one of our versioned heaps?
    for(MappedHeapWithVersioning *i=__vc->first_versioned_heap; i; i=i->next)
    {
      if( i->type == NoVersioning )
        continue;

      const uint64_t low = (uint64_t) i->primary_version.base;
      const uint64_t high = low + i->primary_version.size;

      if( low <= page_addr && page_addr < high )
      {
        // YES.
        const uint64_t offset_within_virtual_address = page_addr - low;
        const uint64_t offset_within_shm = virtual_address_offset_to_shm_offset(offset_within_virtual_address);
        const PageNumber page = offset_within_shm / pagesize;

        // Check if we've already versioned this page
        if( pageset_test_page(i->versioning, page) )
        {
          // Yes, already versioned.  Do nothing.
        }
        else
        {
          // No, not already versioned.

          if( i->type == EagerVersioning )
          {
            // Eager versioning means that, if we are the first
            // worker to modify this page, then we will perform
            // copy-on-write, i.e. allocate a new physical page,
            // initialize it to the old contents, and then let
            // the worker operate on the new private page.

            // Copy the old contents to the new physical page.
            void *src_addr = (void*) page_addr;
            void *dst_addr = (void*) (
              offset_within_virtual_address + (char*) i->secondary_version.base );
            memcpy(dst_addr, src_addr, pagesize);
            // Replace the page mapping
            int fd = heap_fd( i->secondary_version.heap );
            if( MAP_FAILED == mmap(src_addr, pagesize, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, offset_within_shm) )
            {
              perror("mmap");
              _exit(0);
            }
            close(fd);

          }
          else if( i->type == InPlaceVersioning )
          {
            // In-place versioning means that the first worker to
            // modify this page will make a back-up copy, and that
            // all workers will continue modifying the original, shared page.

            // Copy the old contents.
            void *src_addr = (void*) page_addr;
            void *dst_addr = (void*) (
              offset_within_virtual_address + (char*) i->secondary_version.base );
            memcpy(dst_addr, src_addr, pagesize);
          }

          // Update our meta-data
          pageset_add_page(i->versioning, page);
        }

        // Seg fault is handled.  Retry the memory access.
        return;
      }
    }
  }

  // Chain to the next signal handler.
  if( (__vc->old_signals.sa_flags & SA_SIGINFO) != 0 )
    __vc->old_signals.sa_sigaction(sig,si,ctx);

  else
    __vc->old_signals.sa_handler(sig);
}

void start_versioning_control(VersioningControl *vc)
{
  assert( __vc == 0 );
  __vc = vc;

  vc->first_versioned_heap = 0;

  // install our custom segfault handler in order to
  // implement memory versioning in userland.
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset( &sa.sa_mask );
  sigaddset( &sa.sa_mask, SIGSEGV );
  sa.sa_sigaction = userland_copy_on_write_segfault_handler;
  sigaction(SIGSEGV, &sa, &vc->old_signals);
}

void stop_versioning_control(void)
{
  // turn off our signal handler
  sigaction(SIGSEGV, &__vc->old_signals, 0);

  __vc = 0;
}

void heap_map_eager_versioning(Heap *original_version, Heap *new_version, PageSet *pset, MappedHeapWithVersioning *mhwv)
{
  mhwv->next = __vc->first_versioned_heap;
  mhwv->type = EagerVersioning;
  heap_map_nrnw( original_version, & mhwv->primary_version );
  heap_map_anywhere( new_version, & mhwv->secondary_version );
  mhwv->versioning = pset;

  // Foreach page which has been privatized, remap
  PageInterval interval;
  interval.low_inclusive = interval.high_exclusive = 0;
  int fd = -1;
  while( pageset_next_interval(mhwv->versioning, &interval) )
  {
    if( fd < 0 )
      fd = heap_fd( mhwv->secondary_version.heap );

    const uint64_t offset_within_shm = ASSUMED_PAGE_SIZE * interval.low_inclusive;
    const Len len = ASSUMED_PAGE_SIZE * (interval.high_exclusive - interval.low_inclusive);

    void *src_addr = (void*) (
      offset_within_shm + (char*) mhwv->primary_version.base );

    if( MAP_FAILED == mmap(src_addr, len, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, offset_within_shm) )
    {
      perror("mmap");
      _exit(0);
    }

  }
  if( fd >= 0 )
    close(fd);

  __vc->first_versioned_heap = mhwv;
}

void heap_map_inplace_versioning(Heap *original_version, Heap *backup_version, PageSet *pset, MappedHeapWithVersioning *mhwv)
{
  mhwv->next = __vc->first_versioned_heap;
  mhwv->type = InPlaceVersioning;
  heap_map_readonly( original_version, & mhwv->primary_version );
  heap_map_anywhere( backup_version, & mhwv->secondary_version );
  mhwv->versioning = pset;
  __vc->first_versioned_heap = mhwv;
}

void heap_unmap_with_versioning(MappedHeapWithVersioning *mhwv)
{
  mhwv->type = NoVersioning;

  // Remove from list.
  if( __vc->first_versioned_heap == mhwv )
    __vc->first_versioned_heap = mhwv->next;

  else
    for(MappedHeapWithVersioning *i=__vc->first_versioned_heap; i; i=i->next)
      if( i->next == mhwv )
      {
        i->next = mhwv->next;
        break;
      }
  mhwv->next = 0;

  // Free memory.
  heap_unmap( & mhwv->secondary_version );
  heap_unmap( & mhwv->primary_version );
}





