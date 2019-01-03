#ifndef LLVM_LIBERTY_SPECPRIV_PROFILER_READ_H
#define LLVM_LIBERTY_SPECPRIV_PROFILER_READ_H

#include "llvm/ADT/FoldingSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/Analysis/LoopAA.h"

#include "liberty/SpecPriv/ControlSpeculator.h"
#include "liberty/SpecPriv/FoldManager.h"
#include "liberty/SpecPriv/Parse.h"
#include "liberty/SpecPriv/Reduction.h"
#include "liberty/SpecPriv/UpdateOnClone.h"

#include <map>
#include <list>

namespace liberty
{
class PureFunAA;
class SemiLocalFunAA;

namespace SpecPriv
{

using namespace llvm;


typedef std::pair<AU*, Reduction::Type> ReduxAU;
typedef std::vector< ReduxAU > ReduxAUs;

/// This class is responsible for managing and
/// interpreting profiling results.
struct Read : public SemanticAction, public UpdateOnClone
{
  Read();
  virtual ~Read();

  typedef std::map<Ctx*,unsigned> Ctx2Count;
  typedef Ctx2Count Ctx2Residual;
  typedef std::map<Ctx*,Ints> Ctx2Ints;
  typedef std::map<Ctx*,Ptrs> Ctx2Ptrs;

  // Additional analyses will be necessary for certain types of queries.
  void setPureFunAA(const PureFunAA *pure);
  void setSemiLocalFunAA(const SemiLocalFunAA *semi);
  void setControlSpeculator(ControlSpeculation *ctrl);

  // ------------------ query profile results ---------------------

  const Ctx2Count &find_escapes(const AU *au) const;
  const Ctx2Count &find_locals(const AU *au) const;

  const Ctx2Ints &predict_int(const Value *v) const;
  const Ctx2Ptrs &predict_pointer(const Value *v) const;

  const Ctx2Ptrs &find_underylying_objects(const Value *v) const;
  const Ctx2Residual &pointer_residuals(const Value *v) const;

  // Find the union of all pointer-residual sets for
  // the specified pointer in the given context.
  // Returns 0 on error.
  uint16_t getPointerResiduals(const Value *ptr, const Ctx *ctx) const;

  // Determine the underlying objects observed for this
  // pointer when executing within an iteration of the
  // loop 'ctx'.  Save result into 'aus'.  Return false
  // if it failed for some reason.
  bool getUnderlyingAUs(const Value *ptr, const Ctx *ctx, Ptrs &aus) const;

  // Predict the value of an integer at the given loop (NOT subloops);
  bool predictIntAtLoop(const Value *v, const Ctx *ctx, Ints &predictions) const;
  bool predictPtrAtLoop(const Value *v, const Ctx *ctx, Ptrs &predictions) const;

  // Get a set of AUs which were written/read by this instruction
  bool getFootprint(const Instruction *op, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions) const;

  // Get a set of AUs which were written/read by this loop
  bool getFootprint(const Loop *loop, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions) const;

  // Determine if two contexts are ever simultaneously active
  bool areEverSimultaneouslyActive(const Ctx *A, const Ctx *B) const;

  Ctx *getCtx(const Loop *loop, const Ctx *within=0) const;
  Ctx *getCtx(const Function *fcn, const Ctx *within=0) const;

  // ------------------ update on clone ----------------------

  FoldManager *getFoldManager() const { return fm; }

  virtual void contextRenamedViaClone(
    const Ctx *changedContext,
    const ValueToValueMapTy &vmap,
    const CtxToCtxMap &cmap,
    const AuToAuMap &amap);

  // Remove an instruction from the profile.
  // The object may already be freed.
  void removeInstruction(const Instruction *remove);

  // ------------------ parser callbacks ---------------------

  virtual bool sem_escape_object(AU *au, Ctx *ctx, unsigned cnt);
  virtual bool sem_local_object(AU *au, Ctx *ctx, unsigned cnt);
  virtual bool sem_int_predict(Value *value, Ctx *ctx, Ints &ints);
  virtual bool sem_ptr_predict(Value *value, Ctx *ctx, Ptrs &ptrs);
  virtual bool sem_obj_predict(Value *value, Ctx *ctx, Ptrs &ptrs);
  virtual bool sem_pointer_residual(Value *value, Ctx *ctx, unsigned char bitvector);

  virtual AU *fold(AU *) const;
  virtual Ctx *fold(Ctx *) const;

private:
  // These passes are needed for some queries...
  const PureFunAA *pure;
  const SemiLocalFunAA *semi;
  ControlSpeculation *ctrlspec;

  //sot
  const DataLayout *DL;

  typedef std::map<const AU*,Ctx2Count> AU2Ctx2Count;
  typedef std::map<const Value*,Ctx2Ints> Value2Ctx2Ints;
  typedef std::map<const Value*,Ctx2Ptrs> Value2Ctx2Ptrs;
  typedef std::map<const Value*,Ctx2Residual> Value2Ctx2Residual;

  typedef std::set<const Instruction*> CallSiteSet;

  // Manage AU and Ctx objects
  // It's a pointer, so that we can update it within
  // const methods of Read.
  FoldManager   * fm;

  // Profile Data.
  // (0) Escape/Locality Results
  AU2Ctx2Count    escapes;
  AU2Ctx2Count    locals;

  // (1) Integer Prediction Results
  Value2Ctx2Ints  integerPredictions;

  // (2) Pointer Prediction Results
  Value2Ctx2Ptrs  pointerPredictions;

  // (3) Underlying Object Results.
  Value2Ctx2Ptrs  underlyingObjects;

  // (4) Pointer residuals
  Value2Ctx2Residual pointerResiduals;

  template <class BlockIterator>
  bool getFootprint(const BlockIterator &begin, const BlockIterator &end, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions, CallSiteSet &already) const;

  // Get a set of AUs which were written/read by this instruction
  bool getFootprint(const Instruction *op, const Ctx *exec_ctx, AUs &reads, AUs &writes, ReduxAUs &reductions, CallSiteSet &already) const;

  // Do the right thing when profiling info is incomplete
  // due to limited profile coverage.
  bool missingAUs(const Value *obj, const Ctx *ctx, Ptrs &aus) const;
  bool guess(const Value *obj, const Ctx *ctx, Ptrs &aus) const;

  void updateAu2Ctx2Count( AU2Ctx2Count &a2c2c, const CtxToCtxMap &cmap, const AuToAuMap &amap);
  void updateValue2Ctx2Residual( Value2Ctx2Residual &v2c2i, const ValueToValueMapTy &vmap, const CtxToCtxMap &cmap );

  void updateValue2Ctx2Inst( Value2Ctx2Ints &v2c2i, const ValueToValueMapTy &vmap, const CtxToCtxMap &cmap );
  void updateValue2Ctx2Ptrs( Value2Ctx2Ptrs &v2c2p, const ValueToValueMapTy &vmap, const CtxToCtxMap &cmap, const AuToAuMap &amap );


  static void removeInstructionFromPtrs(const Instruction *no_longer_exists, Ptrs &collection);
  static void removeInstructionFromCtx2Ptrs(const Instruction *no_longer_exists, Ctx2Ptrs &collection);
  static void removeInstructionFromValue2Ctx2Ptrs(const Instruction *no_longer_exists, Value2Ctx2Ptrs &collection);
};


// Or, run it as a pass if you prefer.
struct ReadPass : public ModulePass
{
  static char ID;
  ReadPass() : ModulePass(ID), read(0) {}
  ~ReadPass() { if(read) delete read; }

  void getAnalysisUsage(AnalysisUsage &au) const;
  bool runOnModule(Module &mod);
  const Read &getProfileInfo() const { return *read; }
  Read &getProfileInfo() { return *read; }

private:
  Read *read;
};


}
}

#endif

