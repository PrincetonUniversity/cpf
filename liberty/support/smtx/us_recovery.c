#define _GNU_SOURCE

#include <sys/wait.h>
#include <sched.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdio.h>

#include "us_recovery.h"

static const int cloneFlags = SIGCHLD | CLONE_FS | CLONE_FILES;
static const size_t stackSize = 1 << 16;

static void ver_startTramp(ver_tid tid) {
  tid->fcn(tid, tid->arg);
}

/*
 * Spawn a new thread
 */
bool ver_spawn(ver_runnable fcn, ver_tid tid, uint64_t arg) {

  assert(!tid->pid);

  tid->fcn = fcn;
  tid->arg = arg;

  if(!tid->stack)
    tid->stack = mmap((void *) (1UL << 32),
                      stackSize,
                      PROT_WRITE | PROT_READ,
                      MAP_SHARED | MAP_ANONYMOUS,
                      -1,
                      (off_t) 0);

  typedef int (*fn)(void *);
  void *topOfStack = (char *) tid->stack + stackSize;

  tid->pid = clone((fn) ver_startTramp, topOfStack, cloneFlags, tid);

  return tid->pid != -1;
}

/*
 * Wait for a thread to finish
 */
int ver_wait(ver_tid tid) {
  int waitVal = waitpid(tid->pid, NULL, 0);
  tid->pid = 0;
  return waitVal;
}

/*
 * Recover to commited state
 */
void ver_recover(const ver_tid id) {
  if(!setjmp(id->state)) {
    exit(0);
  }
}

static void ver_restartTramp(const ver_tid id) {
  longjmp(id->state, 1);
}

/*
 * Recover a child
 */
void ver_recoverChild(const ver_tid id) {

  if(waitpid(id->pid, NULL, 0) == -1) {
    perror("ver_recoverChild");
  }

  typedef int (*fn)(void *);

  /* Create a hole in the stack between the top of the current stack frame and
     where the return address from the call to clone will be. That way when the
     library implementation pushes runThread and tid onto the new stack, it will
     not overwrite the return address. I don't know why glibc thinks it is safe
     to futz with the new stack before syscalling. Anyway, the undocumented side
     effect of clone is that stack[0] will be arg and stack[1] will be fn. Check
     out sysdeps/unix/sysv/linux/x86_64/clone.S in glibc, for more exciting
     details! */
  uint64_t *stack = (uint64_t *) alloca(2 * sizeof(void *)) + 2;
  id->pid = clone((fn) ver_restartTramp, stack, cloneFlags, id);
}

/*
 * Create a new ver_tid
 */
ver_tid ver_newTID(channel chan, uint32_t curr) {

  ver_tid id = (ver_tid) mmap((void *) (1UL << 32),
                              sizeof(* (ver_tid) NULL),
                              PROT_WRITE | PROT_READ,
                              MAP_SHARED | MAP_ANONYMOUS,
                              -1,
                              (off_t) 0);
  id->stack = NULL;
  id->chan = chan;
  id->curr = curr;

  if(chan) {
    id->queue = ver_getQueue(chan, curr, 0);
    id->processes = chan->processes;
    id->tryCommitQueue = ver_getQueue(chan, curr, chan->processes - 2);
  }
  return id;
}

/*
 * Delete a ver_tid
 */
int ver_deleteTID(ver_tid id) {
  if(id->stack) {
    if(munmap(id->stack, stackSize) == -1) {
      return -1;
    }
  }
  return munmap(id, sizeof(* (ver_tid) NULL));
}

#ifdef UNIT_TEST

char c = 'X';

static void stage1(const ver_tid id, uint64_t arg) {

  (void) arg;

  ver_recover(id);
  printf("%c\n", c);

  ver_recover(id);
  printf("%c\n", c);
}

static void stage2(ver_tid other) {

  c = 'A';
  ver_recoverChild(other);

  c = 'B';
  ver_recoverChild(other);
}

int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  ver_tid tid = ver_newTID(NULL, 0);
  if(tid == (void *) -1) {
    perror("ver_newTID");
    exit(-1);
  }

  if(ver_spawn(stage1, tid, 0ULL) == -1) {
    perror("ver_spawn");
    exit(-1);
  }

  stage2(tid);

  if(ver_wait(tid) == -1) {
    perror("ver_wait");
    exit(-1);
  }

  if(ver_deleteTID(tid) == -1) {
    perror("ver_deleteTID");
    exit(-1);
  }

  return 0;
}

#endif
