#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdint.h>
//#define DEBUG_EXTERN_ICALL_PROF
#ifdef __cplusplus
extern "C" {
#endif

  void __extern_icall_prof_invoc(void *fnPtr, void *node, int id, int maxNumTarget);
  void __extern_icall_prof_init(int numICall, int maxNumTarget);
  void ExternIcallProfFinish(void);

#ifdef __cplusplus
}
#endif

typedef struct value_node_s {
  void * fn_ptr;
  uint32_t cnt;
  Dl_info *dl_info;
} value_node;

typedef struct profile_node_s {
  uint32_t total_cnt;
  value_node vnode[];
} profile_node; 

profile_node **all_nodes;
int TotalNumICall;
int MaxNumTarget;

void __extern_icall_prof_invoc(void *fnPtr, void *node, int id, int maxNumTarget){

  int i = 0;
  profile_node *pnode = (profile_node *) node;
  value_node *vnode = &(pnode->vnode);

  all_nodes[id] = pnode;

  while (i < maxNumTarget) {
    // found a new space
    if (vnode->fn_ptr == NULL){
      vnode->fn_ptr = fnPtr;
      vnode->cnt++;

      // get library and symbol name
      Dl_info *icall_info = (Dl_info *) malloc(sizeof(Dl_info));
      dladdr(fnPtr, icall_info);

#ifdef DEBUG_EXTERN_ICALL_PROF
      printf("#%d first time calling %p\n", id, fnPtr);
      printf("%p: (%s) %s\n", fnPtr, icall_info->dli_fname, icall_info->dli_sname);
#endif
      vnode->dl_info = icall_info;
      break;
    }
    // found an existing one
    else if (vnode->fn_ptr == fnPtr){
      vnode->cnt++;
      // TODO: do we want to check if the existing dladdr is correct
      break;
    }

    i++;
    vnode++;
  }

  pnode->total_cnt++;
}

void __extern_icall_prof_init(int numICall, int maxNumTarget)
{
  TotalNumICall = numICall;
  MaxNumTarget = maxNumTarget;
  all_nodes = (profile_node **) malloc(numICall * sizeof(profile_node*));

#ifdef DEBUG_EXTERN_ICALL_PROF
  printf("Starting external indirect call profiling\n");
#endif

  atexit(ExternIcallProfFinish);
}

/* Finish up the profiler, print profile
 */
void ExternIcallProfFinish()
{
  FILE *fp;

  fp  = fopen("./extern_icall_prof.out", "w+");

  // Total Instrumented CallSite 
  fprintf(fp, "%u %u\n", TotalNumICall, MaxNumTarget);
  for(int i = 0; i < TotalNumICall; ++i){
    // ID
    fprintf(fp, "%d ", i);
    if (all_nodes[i] != NULL){
      fprintf(fp, "%u\n", all_nodes[i]->total_cnt);
    }
    else {
      fprintf(fp, "0\n");
      continue;
    }

    int j = 0;

    value_node *vnode = &(all_nodes[i]->vnode);
    while (j < MaxNumTarget){
      if (vnode->fn_ptr == NULL)
        break;
      assert(vnode->dl_info != NULL);
      //  Target: %p Count: %u Library %s Symbol: %s
      fprintf(fp, "  %p %u %s %s\n", vnode->fn_ptr, vnode->cnt, vnode->dl_info->dli_fname, vnode->dl_info->dli_sname);
      free(vnode->dl_info);
      j++;
      vnode++;
    }
  }

  fclose(fp);
  free(all_nodes);
}
