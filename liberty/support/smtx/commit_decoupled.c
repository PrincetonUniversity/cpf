/*
 * AR 22 July 2009: 
 * 1. Modify Makefile by replacing commit.o with
 * commit_decoupled.o to use this implementation
 * 2. In version.h, ver_writeSilent writes to both 
 * id->processes - 1 and id->processes - 2. This 
 * should not be used in applications that depend 
 * on there being no tryCommit.
 */

#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#define _GNU_SOURCE
#include <sched.h>

#include "smtx.h"
#include "us_recovery.h"

/*
 * Handle recovery from misspeculation
 */
void ver_doRecovery(const ver_tid tid) {

  /* Flush outgoing queues */
  ver_flushAll(tid);

  /* Empty incoming queues to unstick threads waiting trying to produce to me. */
  ver_empty(tid);

  /* Recover commited state */
  ver_recover(tid);

  /* Wait until the commit thread has left recovery */
  ver_waitNotMode(tid, VER_RECOVER);

  ver_mode mode = tid->chan->mode;

  /* Empty incoming queues. Now they will really be empty. */
  ver_empty(tid);

  /* Wait until the commit thread has left recovery */
  if(mode == VER_MISSPEC) {

    /* Signal that recovery is complete */
    ver_signalRecovery(tid);

    ver_waitNotMode(tid, VER_MISSPEC);

    /* Exit if the commit process requests it */
  } else if(mode == VER_TERM) {
    fflush(NULL);
    _Exit(0);
    
  } else {
    assert(false && "Unknown mode");
  }
}

void ver_tryCommitStage(const ver_tid tid, uint64_t arg) {
  void (*fn) (void) = (void (*) (void)) arg;
  if (fn) {
    fn();
  }

  while(true) {

    /* See if committing will misspeculate */
    if(ver_tryCommit(tid) || ver_end(tid)) {

      /* Wait for all threads to recover */
      ver_doRecovery(tid);
    } else {
      /* Execute the function passed through arg */
      if (fn)
        fn();
    }
  }
}

/*
 * Commit to non-speculative state
 */
void ver_commitStage(const ver_tid *tids, 
                     ver_runnable recover, 
                     ver_runnable commit,
                     uint64_t arg) {
  
  const ver_tid id = tids[tids[0]->chan->processes - 1];
  
  ver_mode mode;
  do {

    while((mode = ver_begin(id)) == VER_OK) {
      /* Run user-supplied commit function */
      if(commit) {
        commit(id, arg);
      }
    }
    
    assert((mode == VER_TERM || mode == VER_MISSPEC) && "Unkown mode!");
    
    /* Signal all threads to recover */
    ver_setFlag(id, VER_RECOVER);

    /* Unstick anyone trying to produce to me. If you try to produce more
       than (queue size / 2) times per iteration, you will deadlock. */
    ver_empty(id);
    
    /* Run user-defined recovery function BEFORE recovering the children */
    if(mode == VER_MISSPEC && recover) {
      recover(id, arg);
    }

    /* Recover each child */
    for(unsigned i = 0; i < id->chan->processes - 1; ++i) {
      ver_recoverChild(tids[i]);
    }

    ver_empty(id);
    ver_setFlag(id, mode);

    /* Announce that normal execution may recommence */
    if(mode == VER_MISSPEC) {

      /* Wait for each child to indicate they are ready to continue */
      ver_waitAllRecover(id);

      ver_setFlag(id, VER_OK);

    } else if(mode == VER_TERM) {

      for(unsigned i = 0; i < id->chan->processes - 1; ++i) {
        if(ver_wait(tids[i]) == -1) {
          perror("ver_commitStage");
        }
      }
    } else {
      assert(false && "Unkown mode!");
    }
    *id->chan->forceFlush = false;

  } while(mode != VER_TERM);

  id->chan->mode = VER_OK;
}

#ifdef UNIT_TEST

#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

static const unsigned numThreads = 7;

static void printA(const ver_tid tid, const uint64_t arg) {
  (void) tid;
  (void) arg;

  for(int i = 0; i < 40; ++i) {
    printf("A");
    fflush(stdout);
    usleep(10);
  }

  exit(0);
}

static void printB(const ver_tid tid, const uint64_t arg) {
  (void) tid;
  (void) arg;

  for(int i = 0; i < 40; ++i) {
    printf("B");
    fflush(stdout);
    usleep(10);
  }

  exit(0);
}

static void spawnTest(void) {
  ver_tid tidA = ver_newTID(NULL, 0);
  ver_tid tidB = ver_newTID(NULL, 1);

  ver_spawn(printA, tidA, 0UL);
  ver_spawn(printB, tidB, 0UL);

  ver_wait(tidA);
  ver_wait(tidB);

  printf("\n");

  ver_deleteTID(tidA);
  ver_deleteTID(tidB);
}

char c = 'X';

static void wt_stage1(const ver_tid tid, const uint64_t arg) {
  (void) arg;

  for(unsigned i = 0; i < 79; ++i) {
    ver_begin(tid);

    ver_writeTo(tid, 1 + (i % (numThreads - 4)), &c, (i & 1));
    ver_writeSilent(tid, &c, (i & 1));

    ver_end(tid);
  }
  ver_terminate(tid);
  ver_doRecovery(tid);
}

static void wt_stage2(const ver_tid tid, const uint64_t arg) {

  for(unsigned i = 0; i < arg; ++i) {
    if(ver_begin(tid)) ver_doRecovery(tid);
    if(ver_end(tid)) ver_doRecovery(tid);
  }

  while(true) {
    if(ver_begin(tid)) ver_doRecovery(tid);

    ver_writeAll(tid, &c, 'A' + c);

    if(ver_end(tid)) ver_doRecovery(tid);

    for(unsigned i = 0; i < numThreads - 5; ++i) {
      if(ver_begin(tid)) ver_doRecovery(tid);
      if(ver_end(tid)) ver_doRecovery(tid);      
    }
  }
}

static void wt_stage3(const ver_tid tid, const uint64_t arg) {

  (void) arg;

  while(true) {
    if(ver_begin(tid)) ver_doRecovery(tid);
    printf("%c", c);
    if(ver_end(tid)) ver_doRecovery(tid);
  }
}

static void setupTIDs(channel chan, ver_tid *tids) {
  for(unsigned i = 0; i < numThreads; ++i) {
    tids[i] = ver_newTID(chan, i);
  }
  
  tids[0]->prev = -1;
  tids[0]->next = 1;

  for(unsigned i = 1; i < numThreads - 3; ++i) {
    tids[i]->prev = 0;
    tids[i]->next = numThreads - 3;
  }

  for(unsigned i = numThreads - 3; i < numThreads; ++i) {
    tids[i]->prev = (int) i - 1;
    tids[i]->next = i + 1;
  }

  tids[numThreads - 1]->prev = (int) numThreads - 3;
}

static void writeTest(void) {
  channel chan = ver_newChannel(numThreads);

  ver_tid tids[numThreads];
  setupTIDs(chan, tids);

  ver_spawn(wt_stage1, tids[0], 0UL);

  for(unsigned i = 1; i < numThreads - 3; ++i) {
    ver_spawn(wt_stage2, tids[i], BOX64(i - 1));
  }

  ver_spawn(wt_stage3, tids[numThreads - 3], 0UL);

  ver_spawn(ver_tryCommitStage, tids[numThreads - 2], 0UL);
  ver_commitStage(tids, NULL, NULL, 0UL);

  printf("%c\n", c + 1);

  for(unsigned i = 0; i < numThreads; ++i) {
    ver_deleteTID(tids[i]);
  }

  ver_deleteChannel(chan);
}

char rt_c = 'X';
char rt_d = 'X';
int rt_i = 0;

static void rt_stage1(const ver_tid tid, const uint64_t arg) {

  (void) arg;
  
  while(true) {

    for(; rt_i < 80; ) {
      if(ver_begin(tid)) { ver_doRecovery(tid); continue; }

      rt_c = (char) ((rt_i >> 2) & 1);
      ver_writeSilent(tid, &rt_c, rt_c);

      ++rt_i;
      ver_writeSilent(tid, &rt_i, rt_i);

      if(ver_end(tid)) { ver_doRecovery(tid); continue; }
    }
    
    ver_terminate(tid);
    ver_doRecovery(tid);
  }
}

static void rt_stage2(const ver_tid tid, const uint64_t arg) {

  (void) arg;

  while(true) {
    if(ver_begin(tid)) { ver_doRecovery(tid); continue; }

    ver_read(tid, &rt_c, rt_c);
    rt_d = (char) (rt_c + 'A');
    ver_writeAll(tid, &rt_d, rt_d);
    
    if(ver_end(tid)) { ver_doRecovery(tid); continue; }
  }
}

static void rt_commit(const ver_tid tid, uint64_t arg) {
  (void) tid;
  (void) arg;

  printf("%c", rt_d);
  fflush(stdout);
}

static void rt_recover(const ver_tid tid, uint64_t arg) {
  
  (void) tid;
  (void) arg;

  rt_c = (char) ((rt_i >> 2) & 1);
  ++rt_i;
  rt_d = (char) (rt_c + 'A');

  printf("%c", rt_d);
  fflush(stdout);
}

static void readTest(void) {

  const unsigned readThreads = 4;
  channel chan = ver_newChannel(readThreads);

  ver_tid tids[readThreads];

  for(unsigned i = 0; i < readThreads; ++i) {
    tids[i] = ver_newTID(chan, i);
    tids[i]->prev = (int)(i - 1);
    tids[i]->next = i + 1;
  }

  tids[readThreads - 1]->prev = (int) readThreads - 3;

  rt_i = 0;

  ver_spawn(rt_stage1, tids[0], 0UL);
  ver_spawn(rt_stage2, tids[1], 0UL);
  ver_spawn(ver_tryCommitStage, tids[2], 0UL);

  ver_commitStage(tids, rt_recover, rt_commit, 0UL);
  
  printf("\n");

  for(unsigned i = 0; i < readThreads; ++i) {
    ver_deleteTID(tids[i]);
  }

  ver_deleteChannel(chan);
}

int main(int argc, char **argv) {
  
  (void) argc;
  (void) argv;

  if(argc >  1 && !strcmp(argv[1], "--help")) {
    printf
      ("Syntax commit.unit [--help]\n"
       "Line 1: Test ver_spawn.\n" 
       "\tA thread printing A races with a thread printing B.\n"
       "Line 2: A PSDSWP pipeline.\n"
       "\tThe first stage writes 0 and 1 in alternate iterations. The second\n"
       "\tstage is parallel and add 'A' to the previous stage's write. The third\n" 
       "\tstage prints the result.\n"
       "Line 3: A SpecDSWP pipeline.\n"
       "\tThe second stage will misspeculate every fourth iteration when the\n" 
       "\tstore in the first stage is not silent.\n");
  }
  
  /* Test the ver_spawn function */
  spawnTest();

  /* Test ver_begin, ver_writeAll, ver_writeTo, ver_writeSilent, ver_terminate,
     ver_doRecovery, and ver_end */
  writeTest();
  
  /* Test ver_recover, ver_read, and ver_tryCommit */
  readTest();

  return 0;
}
#endif /* UNIT_TEST */
