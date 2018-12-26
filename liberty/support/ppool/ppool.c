/*
 * Process-based threading library
 *
 * Limitations: Currently, it does not support:
 *              - multiple parallel regions
 *              - non-PSDSWP-style parallelism
 */

#include <stdio.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include "ppool_channel.h"
#include "ppool_bitcast.h"
#include "ppool_log.h"

#include "ppool.h"

int glb_processNum;
static channel chl;
chl_tid mytid;
chl_tid *tids;
pid_t *pid_array;


// FIXME: Using Runnable type will be more general.
// static Runnable worker_fcn = NULL;
static chl_runnable worker_fcn = NULL;

void
ppool_init(int num_processes, chl_runnable fcn)
{
  char **mat;
  int i,j;

  glb_processNum = num_processes;
  worker_fcn = fcn;

  mat = (char**) malloc(sizeof(char *)*2);
  for (i=0; i<num_processes; i++){
    mat[i] = (char*) malloc(sizeof(char *) * num_processes);
    for (j=0; j<num_processes; j++)
      mat[i][j] = ( i != j ) ? 1 : 0;
  }

  /* an array of chl_tid's */
  tids = (chl_tid *) malloc( sizeof(chl_tid) * glb_processNum );

//  fprintf(stderr, "libppool::ppool_init(): before channel creation\n");
//  chl = newChannel(num_processes, mat, 18);
  chl = newChannel(num_processes, mat, 12);
//  fprintf(stderr, "libppool::ppool_init(): after channel creation\n");

  /* fill in the array */
  for (i=0; i<glb_processNum; i++)
  {
    tids[i] = newTid(chl, i);
  }

  /* an array of pid's */
  pid_array = (pid_t *) malloc( sizeof(pid_t) * (glb_processNum-1) );

}

void
ppool_finish()
{
  deleteChannel(chl);
}

void
ppool_commence_parallel_exe()
{
  int i;
//  fprintf(stderr, "libppool::ppool_commence_parallel_exe(): commence Parallel Execution \n");

  for (i=0; i<glb_processNum-1; i++)
  {
    chl_tid tid = tids[i];
    spawnProcess(worker_fcn, tid, i, &pid_array[i]);
  }

  mytid = tids[glb_processNum-1];
  return;
}

void
ppool_wait(void)
{
  int i;

  for (i=0; i<glb_processNum-1; i++)
  {
    int status;
    waitpid(pid_array[i], &status, 0);
  }
}


chl_tid
ppool_get_my_tid(int id)
{
  assert ( id<glb_processNum && "libppool::ppool_get_my_tid(): ID must be < num_processes" );
  return tids[id];
}

