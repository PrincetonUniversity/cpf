#include <pthread.h>    /* for pthread */
#include <stdio.h>      /* for perror */
#include <errno.h>      /* for perror */
#include <sched.h>      /* for clone */
#include <sys/types.h>  /* for pid_t */
#include <signal.h>     /* for SIGCHLD */
#include <stdlib.h>     /* for exit */
// #include <malloc.h>      /* for malloc */

#include "ppool_channel.h"  /* header */
#include "ppool_log.h"      /* for LOG */

#define STACK_SIZE 1024*1024

chl_tid newTid(channel chl, unsigned id) {
  chl_tid tid;
  unsigned i, processes;
  Queue** q;

  if(chl==NULL) {
    fprintf(stderr, "libppool::newTid(): channel is not defined\n");
    return NULL;
  }

  processes = getChannelProcesses(chl);
  if(processes <= id) {
    fprintf(stderr, "libppool::newTid(): id cannot exceed the number of processes\n");
    return NULL;
  }

  tid = (chl_tid) malloc(sizeof(struct chl_tid_t));
  if(tid == NULL) {
    perror("libppool::newTid(): malloc tid");
    return NULL;
  }

  tid->cQ = (Queue *) malloc(sizeof(Queue)*processes);
  if(tid->cQ == NULL) {
    perror("libppool::newTid(): malloc cQ");
    return NULL;
  }

  tid->chl = chl;
  tid->processes = processes;
  tid->id = id;
  q = getChannelQueueMatrix(chl);
  tid->pQ = q[id];

  for(i=0; i<processes; i++) {
    tid->cQ[i] = q[i][id];
  }

  return tid;
}

int deleteTid(chl_tid tid) {
  int id;
  if(tid == NULL) return 0;
  id = tid->id;
  if(tid->stack != NULL) {
    free(tid->stack);
  }
  free(tid->cQ);
  free(tid);
  return id;
}

bool spawnProcess (chl_runnable fcn, chl_tid tid, int32_t arg, pid_t *pid_ptr) {
  //LOG("enter\n");

  if(fcn==NULL) return false;
  if(tid==NULL) return false;

  if((*pid_ptr=fork())) {
	  if (*pid_ptr < 0)
	  {
		  perror("spawnProcess");
	  }
    return true;
  } else {
    //LOG("Fork succeeded!\n");
    fcn(arg);
    exit(0);
  }
}


