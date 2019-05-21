#ifndef LLVM_LIBERTY_SPEC_PRIV_STRATEGY_H
#define LLVM_LIBERTY_SPEC_PRIV_STRATEGY_H

#define GET_MY_STAGE(wid) (__specpriv__curLoop_wid2stage[(wid)])
#define GET_NUM_STAGES() (__specpriv__curLoop_num_stages)
#define GET_FIRST_WID_OF_STAGE(stage)                                          \
  (__specpriv__curLoop_stage2firstwid[(stage)])
#define GET_REPLICATION_FACTOR(stage) (__specpriv__curLoop_stage2rep[(stage)])
#define GET_WID_OFFSET_IN_STAGE(iter, stage)                                   \
  ((iter) % __specpriv__curLoop_stage2rep[(stage)])

extern unsigned  __specpriv__curLoop_num_stages;
extern unsigned* __specpriv__curLoop_stage2rep;
extern unsigned* __specpriv__curLoop_wid2stage;
extern unsigned* __specpriv__curLoop_stage2firstwid;

#endif
