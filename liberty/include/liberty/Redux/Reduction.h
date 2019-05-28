#ifndef LLVM_LIBERTY_SPEC_PRIV_REDUCTION_H
#define LLVM_LIBERTY_SPEC_PRIV_REDUCTION_H

#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "PDG.hpp"
#include <set>

namespace liberty
{

class LoopAA;

namespace SpecPriv
{

//class PDG;
using namespace llvm;


typedef std::set<Value*> VSet;
typedef std::vector<Instruction*> Fringe;

struct Reduction
{
  // If you update this, be sure to also update:
  //  - the names[] array.
  //  - reduction types in support/specpriv-executive/fiveheaps.h
  enum Type { NotReduction = 0,

              Add_i8,
              Add_i16,
              Add_i32,
              Add_i64,

              Add_f32,
              Add_f64,

              Max_i8, // signed
              Max_i16,
              Max_i32,
              Max_i64,
              Max_u8, // unsigned
              Max_u16,
              Max_u32,
              Max_u64,

              Max_f32,
              Max_f64,

              Min_i8, // signed
              Min_i16,
              Min_i32,
              Min_i64,
              Min_u8, // unsigned
              Min_u16,
              Min_u32,
              Min_u64,

              Min_f32,
              Min_f64

            };
  static StringRef names[];

  struct ReduxInfo {
    Reduction::Type type;
    const Instruction *depInst;
    Reduction::Type depType;
  };

  static Type isReductionLoad(
    const LoadInst *load,
    /* optional output parameters */
    const BinaryOperator **add_out = 0,
    const CmpInst **cmp_out = 0,
    const BranchInst **br_out = 0,
    const StoreInst **st_out = 0);
  static Type isReductionStore(const StoreInst *store);
  static bool allOtherAccessesAreReduction(Loop *loop, Reduction::Type ty, Value *accumulator, LoopAA *loopaa);

  static Type isAssocAndCommut(const BinaryOperator *add);
  static Type isAssocAndCommut(const CmpInst *cmp);

  static bool demoteRegisterReductions(LoopInfo &li, ScalarEvolution &scev, Loop *loop);

  static Value *getIdentityValue(Type ty, LLVMContext &ctx);

  static Type isReduction(const LoadInst *load, const CmpInst *cmp, const BranchInst *branch, const StoreInst *store);
  static Type isReduction(const LoadInst *load, const BinaryOperator *add, const StoreInst *store);

  // PDG can be NULL
  static bool isRegisterReduction(
    /* Inputs */  ScalarEvolution &scev, Loop *loop, PHINode *phi0, const llvm::PDG *pdg, const std::set<PHINode*> &ignore,
    /* Outputs */ Reduction::Type &rt, BinaryOperator::BinaryOps &reductionOpcode,  VSet &u_phis, VSet &u_binops, VSet &u_cmps, VSet &u_brs, VSet &used_outside, Value* &initial_value);

private:
  // swap min <==> max
  static Type reverse(Type rt);

  static bool isLastUpdate(Value *v, VSet &u_binops, Loop *loop);
};

}
}

#endif

