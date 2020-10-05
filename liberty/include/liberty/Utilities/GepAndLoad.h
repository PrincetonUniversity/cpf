#ifndef LLVM_LIBERTY_GEP_AND_LOAD_H
#define LLVM_LIBERTY_GEP_AND_LOAD_H

#include "scaf/Utilities/InstInsertPt.h"

namespace liberty
{
using namespace llvm;

void storeIntoStructure(InstInsertPt &where, Value *valueToStore, Value *pointerToStructure, unsigned fieldOffset);
Value *loadFromStructure(InstInsertPt &where, Value *pointerToStructure, unsigned fieldOffset);

}


#endif

