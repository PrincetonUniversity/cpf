#include "smtx.h"
#include <stdio.h>

static int numRecover;

static void stage1(const ver_tid id, uint64_t arg) {

  (void) arg;

  for(int i = 0; i < numRecover; ++i) {
    ver_recover(id);
  }
}

static void stage2(ver_tid other) {

  for(int i = 0; i < numRecover; ++i) {
    ver_recoverChild(other);
  }
}

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Format: test_recover <number of recoveries>\n");
    exit(1);
  }

  numRecover = atoi(argv[1]);

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
