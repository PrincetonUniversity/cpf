//#include "llvm/Support/CFG.h"
#include "llvm/IR/Instructions.h"

#include "liberty/Utilities/GetOrInsertCIV.h"
#include "liberty/Utilities/InstInsertPt.h"

namespace liberty
{
using namespace llvm;

PHINode *getOrInsertCanonicalInductionVariable(const Loop *loop)
{
  PHINode *civ = loop->getCanonicalInductionVariable();
  if( civ )
    return civ;

  // The loop does not have a CIV.
  // Create one.

  BasicBlock *header = loop->getHeader();

  LLVMContext &ctx = header->getContext();
  IntegerType *u32 = IntegerType::get(ctx, 32);
  ConstantInt *zero = ConstantInt::get(u32,0),
              *one  = ConstantInt::get(u32,1);

  const pred_iterator begin = pred_begin(header),
                      end   = pred_end(header);

  PHINode *phi = PHINode::Create(u32,0,"civ");
  BinaryOperator *increment = BinaryOperator::CreateNSWAdd(phi, one);

  for(pred_iterator i=begin; i!=end; ++i)
  {
    BasicBlock *pred = *i;
    if( loop->contains(pred) )
      // Loop backedge
      phi->addIncoming(increment, pred);
    else
      // Loop entry
      phi->addIncoming(zero, pred);
  }

  InstInsertPt::Beginning(header) << phi << increment;
  return phi;
}

}


