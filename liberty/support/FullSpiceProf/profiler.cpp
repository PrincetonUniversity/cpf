#include <iostream>
#include <vector>
#include <map>
#include <stdio.h>
#include <stack>
#include <assert.h>

extern "C"{
namespace std{
  //map of loop to its invoke_count
  map<int, int> loop_invoke_cnts;

  //record currently executing loops
  stack<int> loopStack;

  FILE* wf;

  void __spice_profile_load(void* ptr, int staticNum)
  {
    wf = fopen("spice.prof", "wb");
    assert(wf && "did you insert __spice_before_main?\n");

    //print loop
    int currLoopNum = loopStack.top();
    fwrite((char*)&currLoopNum, sizeof(int), 1, wf);
    //fprintf(stderr, "print loop %d\n", currLoopNum);

    //print invoke_count
    int invoke_count = loop_invoke_cnts[currLoopNum];
    fwrite((char*)&invoke_count, sizeof(int), 1, wf);
    //fprintf(stderr, "print invoke_count %d\n", invoke_count);

    //print static var count
    fwrite((char*)&staticNum, sizeof(int), 1, wf);
    //fprintf(stderr, "print static var Num %d\n", staticNum);

    //print ptr
    fwrite((char*)&ptr, sizeof(void*), 1, wf);
    //fprintf(stderr, "print ptr %ld\n", (long)ptr);
  }

  void __spice_start_invocation(int loop_num){

    //create a loop profile if not created
    if(!loop_invoke_cnts.count(loop_num)) // if there is no profile yet for this loop
      loop_invoke_cnts.insert({loop_num, 0});

    //push the loop onto the stack;
    loopStack.push(loop_num);
    fprintf(stderr, "entering loop: %d, current loop num is the same as the loop entering\n", loop_num);
  }

  void __spice_end_invocation(){
    fprintf(stderr, "leaving loop: %d\n", loopStack.top());
    int currLoopNum = loopStack.top();
    loop_invoke_cnts[currLoopNum] ++;
    loopStack.pop();
  }

  void __spice_before_main(){
    //open prof file
    wf = fopen("spice.prof", "wb");
  }

}
}
