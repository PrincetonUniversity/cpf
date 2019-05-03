#ifndef LLVM_LIBERTY_SPECPRIV_MTCG_PRINT_STAGE_H
#define LLVM_LIBERTY_SPECPRIV_MTCG_PRINT_STAGE_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "liberty/CodeGen/MTCG.h"

namespace liberty
{
namespace SpecPriv
{
using namespace llvm;

void writeStageCFG(Loop *loop, unsigned stageno, StringRef type,
  const BBSet &rel, const ISet &insts,
  const PreparedStrategy::ProduceTo &prod, const PreparedStrategy::ConsumeFrom &cons);

}
}

#endif
