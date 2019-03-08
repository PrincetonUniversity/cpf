#include "smtx.h"
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
