#ifndef LLVM_LIBERTY_NO_ESCAPE_FIELDS_AA
#define LLVM_LIBERTY_NO_ESCAPE_FIELDS_AA

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"

#include "liberty/Analysis/ClassicLoopAA.h"
#include <set>

namespace liberty {
  using namespace llvm;

  /* A pass which identifies fields of sane types which
   * are accessed, but of which references never escape.
   */
  class NonCapturedFieldsAnalysis : public ModulePass {

    typedef DenseMap<StructType *, BitVector> CapturedFields;
    CapturedFields escapingFields;

  public:
    // Either:
    //  (global variable => constant expression from initializer), or
    //  (store instruction => stored value)
    typedef std::pair<Value*,Value*> Def;
    typedef std::set<Def> Defs;

  private:
    // A field => a list of definitions
    typedef std::map<const ConstantInt*, Defs> Field2Defs;
    // Structure => Field => Defs
    typedef DenseMap<StructType*,Field2Defs> Struct2Field2Defs;

    Struct2Field2Defs fieldDefinitions;

    Module *currentModule;

    void runOnFunction(Function &fcn);

    bool isSafeFieldPointer(Instruction *gep, StructType *structty, const ConstantInt *fieldno);

    void addDefinition(StructType *structty, const ConstantInt *fieldno, StoreInst *store);
    void addDefinition(StructType *structty, const ConstantInt *fieldno, GlobalVariable *gv, Constant *initializer);

    void eraseDefinitions(StructType *structty, const ConstantInt *fieldno = 0);

    void collectDefsFromGlobalVariable(GlobalVariable *gv);
    void collectDefsFromGlobalVariable(GlobalVariable *gv, Type *ty, Constant *initor);

    /// Canonicalize a ConstantInt, either null or type i64.
    static const ConstantInt *Const64(const ConstantInt *);

  public:

    static char ID;
    NonCapturedFieldsAnalysis()
      : ModulePass(ID) {}

    void getAnalysisUsage(AnalysisUsage &au) const;

    bool runOnModule(Module &);

    StringRef getPassName() const
    {
      return "Identify non-captured fields of sane types";
    }

    bool captured(const GetElementPtrInst *gep) const;
    bool captured(StructType *, const ConstantInt *) const;
    bool captured(StructType *, uint64_t) const;

    /// Determine all definitions to this field, either via store
    /// instructions or via initializers of global variables.  ONLY tracks
    /// pointer-typed values.  You should also assume that 'undef' is a
    /// possible definition.  You may pass 'fieldno'==null to refer to
    /// all non-escaping fields of this structure.
    /// @brief Collect a list of all definitions to this field.
    bool findAllDefs(StructType *structty, const ConstantInt *fieldno,
      Defs &defs_out) const;

    bool isFieldPointer(
      const Value *,
      StructType **,  // out parameter
      const ConstantInt **, // out parameter
      bool strict = false
      ) const;
  };


  /* A pass which identifies fields of sane types which
   * are accessed, but of which references never escape.
   */
  class NoEscapeFieldsAA : public ModulePass, public ClassicLoopAA {

    typedef DenseMap<const Instruction *, ModRefResult> CallsiteTouches;
    CallsiteTouches callsiteTouches;

    ModRefResult callsiteTouchesNonEscapingField(
      CallSite cs,
      const Pointer &p2,
      StructType *struct2,
      const ConstantInt *field2);

  public:

    static char ID;
    NoEscapeFieldsAA()
      : ModulePass(ID), ClassicLoopAA() {}

    void getAnalysisUsage(AnalysisUsage &au) const;

    bool runOnModule(Module &);

    /// getAdjustedAnalysisPointer - This method is used when a pass implements
    /// an analysis interface through multiple inheritance.  If needed, it
    /// should override this to adjust the this pointer as needed for the
    /// specified pass info.
    virtual void *getAdjustedAnalysisPointer(AnalysisID PI) {
      if (PI == &LoopAA::ID)
        return (LoopAA*)this;
      return this;
    }

    StringRef getPassName() const
    {
      return "Non-captured fields AA";
    }

    StringRef getLoopAAName() const { return "NoEscapeFieldsAA"; }

    AliasResult aliasCheck(
      const Pointer &P1,
      TemporalRelation rel,
      const Pointer &P2,
      const Loop *L);

    ModRefResult getModRefInfo(
      CallSite cs,
      TemporalRelation rel,
      const Pointer &p2,
      const Loop *L);

    ModRefResult getModRefInfo(
      CallSite cs,
      TemporalRelation rel,
      CallSite cs2,
      const Loop *L);
  };
}


#endif // LLVM_LIBERTY_NO_ESCAPE_FIELDS_AA

