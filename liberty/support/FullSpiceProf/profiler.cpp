#include <iostream>
#include <vector>
#include <map>
#include <stdio.h>
#include <stack>

extern "C"{
namespace std{
  //profile values per invocation
  typedef vector<long> vals_t;

  //profile/count per static variable
  typedef map<int, vals_t*> InvokeVal_t;

  //map of static variable to its profile/count
  typedef vector<InvokeVal_t*> LoopVal_t;

  //map of loop to its profiles/counts
  map<int, LoopVal_t*> per_loop_vals;

  InvokeVal_t* curr_InvokeVal;

  //record currently executing loops
  stack<int> loopStack;

  void __spice_profile_load(void* ptr, int staticNum)
  {
    //get the current invoke val
    int currLoopNum = loopStack.top();
    fprintf(stderr, "profile_load in loop %d\n", currLoopNum);
    curr_InvokeVal = (per_loop_vals[currLoopNum])->back();

    //if the current variable has no profile then create one
    if(!curr_InvokeVal->count(staticNum))
      curr_InvokeVal->insert({staticNum, new vals_t()});

      auto vals = (*curr_InvokeVal)[staticNum];
      vals->push_back((long)ptr);
  }

  void __spice_start_invocation(int loop_num){

    //create a loop profile if not created
    if(!per_loop_vals.count(loop_num)) // if there is no profile yet for this loop
      per_loop_vals.insert({loop_num, new LoopVal_t()});

    //create a new loop invocation
    LoopVal_t* curr_loop_vals = per_loop_vals[loop_num];
    curr_loop_vals->push_back(new InvokeVal_t());

    //push the loop onto the stack;
    loopStack.push(loop_num);
    fprintf(stderr, "entering loop: %d, current loop num is the same as the loop entering\n", loop_num);
  }

  void __spice_end_invocation(){
    fprintf(stderr, "leaving loop: %d\n", loopStack.top());
    loopStack.pop();
  }

  void __spice_profile_printAll(){
    //open prof file
    FILE *wf= fopen("spice.prof", "wb");

    //print number of loops
    int loopCnt = per_loop_vals.size();
    fwrite((char*)&loopCnt, sizeof(int), 1, wf);
    fprintf(stderr, "loopCnt = %d\n", loopCnt);

    //for each loop
    /*for(auto &x : per_loop_vals)
    {
      int loop_name = x.first;
      LoopVal_t loop_vals = *(x.second);
      //LoopIter_t loop_iters = *per_loop_itercounts[loop_name];
      int invoke_count = loop_vals.size();

      //print invocation counts
      fwrite((char*)&invoke_count, sizeof(int), 1, wf);
      fprintf(stderr, "invoke count = %d\n", invoke_count);

      //print loop name
      fwrite((char*)&loop_name, sizeof(int), 1, wf);
      fprintf(stderr, "loop name = %d\n", loop_name);

      for(int j=0; j<invoke_count; j++)
      {
        InvokeVal_t invoke_vals = *(loop_vals[j]);
        //InvokeIter_t invoke_iters = *loop_iters[j];
        int numVars = invoke_vals.size();

        //print num of vars in one invoke
        fwrite((char*)&numVars, sizeof(int), 1, wf);
        fprintf(stderr, "num of vars = %d\n", numVars);

        for(auto &y : invoke_vals)
        {
          int varId = y.first;

          //print current var Id
          fwrite((char*)&varId, sizeof(int), 1, wf);
          fprintf(stderr, "varId = %d\n", varId);
          vals_t vals = *(y.second);
          int iters = vals.size();

          //print current var iter counts
          fwrite((char*)&iters,sizeof(int), 1, wf);
          fprintf(stderr, "iters = %d\n", iters);
          //print curr var values
          for(int k=0; k<iters; k++)
            fwrite((char*)&vals[k], sizeof(long), 1, wf);
        }
      }
    }*/
    fclose(wf);
  }

}
}
