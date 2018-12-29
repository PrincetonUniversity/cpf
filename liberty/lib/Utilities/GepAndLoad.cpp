#include "liberty/Utilities/GepAndLoad.h"

namespace liberty
{

void storeIntoStructure(InstInsertPt &where, Value *valueToStore, Value *pointerToStructure, unsigned fieldOffset)
{
  Module *mod = where.getModule();
  IntegerType *u32 = Type::getInt32Ty( mod->getContext() );
  Value *indices[] = { ConstantInt::get(u32,0), ConstantInt::get(u32, fieldOffset) };
  GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(pointerToStructure, ArrayRef<Value*>(&indices[0], &indices[2]));
  StoreInst *store = new StoreInst(valueToStore, gep);
  where << gep << store;
}


Value *loadFromStructure(InstInsertPt &where, Value *pointerToStructure, unsigned fieldOffset)
{
  Module *mod = where.getModule();
  IntegerType *u32 = Type::getInt32Ty( mod->getContext() );
  Value *indices[] = { ConstantInt::get(u32,0), ConstantInt::get(u32, fieldOffset) };
  GetElementPtrInst *gep = GetElementPtrInst::CreateInBounds(pointerToStructure, ArrayRef<Value*>(&indices[0], &indices[2]));
  LoadInst *load = new LoadInst(gep);
  where << gep << load;
  return load;
}

}

