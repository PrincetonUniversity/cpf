#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ucontext.h>

#include <set>

#include "api.h"
#include "internals/constants.h"
#include "internals/debug.h"
#include "internals/profile.h"
#include "internals/strategy.h"
#include "internals/smtx/communicate.h"
#include "internals/smtx/malloc.h"
#include "internals/smtx/protection.h"
#include "internals/smtx/separation.h"
#include "internals/smtx/smtx.h"

#define UNW_LOCAL_ONLY
#include <libunwind.h>

namespace specpriv_smtx
{

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

static std::set<uint8_t*> triggered;

static __attribute__((noinline)) void dumpstack() {
  unw_cursor_t cursor;
  unw_context_t uc;
  unw_word_t ip;

  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);

  // move up once because current stack frame (within handler) is meaningless

  assert(unw_step(&cursor) > 0);
  unw_get_reg(&cursor, UNW_REG_IP, &ip);

  while (unw_step(&cursor) > 0)
  {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    fprintf(stderr, "pid: %u, %p\n", getpid(), (void*)ip);
  }
}

static bool is_valid_page(uint8_t* page, int stage)
{
  uint8_t* shadow = (uint8_t*)(GET_SHADOW_OF(page));

  DBG("page %p stage %d shadowheap %u ro %u nrbw %u uc %u spv %d\n",
    page, stage, shadow_heaps->count(shadow), is_in_ro(page), is_in_nrbw(page), is_in_uc(page),
    is_in_stage_private(page) );

  if ( shadow_heaps->count(shadow) ) return true;

  if ( is_in_ro(page) || is_in_nrbw(page) || is_in_uc(page) ) return true;

  return (is_in_stage_private(page) == stage);
}

static void base_handler(int sig, siginfo_t *si, void *ucontext)
{
  fprintf(stderr, "segfault from %p, page %lx\n", si->si_addr, (size_t)si->si_addr & PAGE_MASK);
  dumpstack();
  while(1);
}

static void handler(int sig, siginfo_t *si, void *ucontext)
{
  uint8_t* page = (uint8_t*)((size_t)si->si_addr & PAGE_MASK);
  uint8_t* shadow = (uint8_t*)(GET_SHADOW_OF(page));
  int      stage = GET_MY_STAGE( PREFIX(my_worker_id)() );

  if ( is_valid_page(page, stage) )
  {
    if ( triggered.count((uint8_t*)(si->si_addr)) )
    {
      if ( is_in_ro(page) )
      {
        // TODO: this is misspec
        DBG("Error (ro violation): page %p\n", page);
      }
      else if ( is_in_stage_private(page) != -1 )
      {
        // TODO: this is misspec
        DBG("Error (stage privacy violation): page %p\n", page);
      }
      else
      {
        DBG("Error (page already triggered by the same address) : page %p\n", page);
      }

      handle_error("hopeless - page already triggered by the same address");
    }

    triggered.insert((uint8_t*)(si->si_addr));

    *shadow |= (0x80);

    ucontext_t* uc = (ucontext_t*)(ucontext);

    int prot = PROT_READ;
    if ( !is_in_ro(page) ) prot |= PROT_WRITE;

    if( mprotect((void*) page, (size_t) PAGE_SIZE, prot) == -1 )
    {
      DBG("Error (mprotect failed to retrieve the permission) : page %p\n", page);
      handle_error("hopeless - mprotect failed to retrieve the permission");
    }

    DBG("successfully retreive the permission for page %p\n", page);
  }
  else
  {
    DBG("Error (segfault out of registered pages) : page %p\n", page);
    PROFDUMP("Error (segfault out of registered pages) : addr %p page %p\n", si->si_addr, page);
    dumpstack();
    handle_error("segfault out of registered pages");
  }
}

void register_base_handler()
{
  assert( sysconf(_SC_PAGE_SIZE) == PAGE_SIZE );

  struct sigaction sa;

  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = base_handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1)
    handle_error("sigaction");
}

void register_handler()
{
  DBG("register handler\n");
  assert( sysconf(_SC_PAGE_SIZE) == PAGE_SIZE );

  struct sigaction sa;

  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1)
    handle_error("sigaction");
}

void reset_protection(Wid wid)
{
  DBG("reset_protection - heaps\n");

  for (std::set<uint8_t*>::iterator i=shadow_heaps->begin(), e=shadow_heaps->end() ; i != e ; i++)
  {
    DBG("new shadow heap\n");
#if (PROFILE || PROFILE_WEIGHT)
    reset_pages[wid] += 1;
#endif
    uint8_t* shadow = *i;
    DBG("reset_protection, shadow: %p\n", shadow);
    void*    original = (void*)GET_ORIGINAL_OF(shadow);
    DBG("reset_protection, original: %p\n", original);

    DBG("reset_protection, call mprotect for %p", original);

    if( mprotect(original, (size_t) PAGE_SIZE, PROT_NONE) == -1 )
    {
      DBG("Error: page %p\n", original);
      handle_error("mprotect failed to reset protection (heaps)");
    }

    DBG(", good\n");

    *shadow &= 0x7f;
  }

  set_separation_heaps_prot_none();

  triggered.clear();
}

void set_page_rw(void* page)
{
  if( mprotect(page, (size_t) PAGE_SIZE, PROT_READ | PROT_WRITE) == -1 )
  {
    DBG("Error: page %p\n", page);
    handle_error("mprotect failed to set the page RW");
  }
}

}
