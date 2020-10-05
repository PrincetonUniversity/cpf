#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"

#include "scaf/Utilities/CastUtil.h"
#include "scaf/Utilities/InstInsertPt.h"



using namespace llvm;
using namespace liberty;

Value *liberty::castToInt64Ty(Value *value, InstInsertPt &out, NewInstructions *ni) {

  Type *ty = value->getType();

  Type *int64Ty = Type::getInt64Ty(ty->getContext());

  if(ty->isFloatTy()) {

    Type *int32Ty = Type::getInt32Ty(ty->getContext());
    Instruction *bitcast = CastInst::CreateZExtOrBitCast(value, int32Ty);
    out << bitcast;
    if( ni ) ni->push_back(bitcast);

    Instruction *zext = CastInst::CreateZExtOrBitCast(bitcast, int64Ty);
    out << zext;
    if( ni ) ni->push_back(zext);

    return zext;
  }

  if(ty->isIntegerTy(32) || ty->isDoubleTy()) {
    Instruction *conv = CastInst::CreateZExtOrBitCast(value, int64Ty);
    out << conv;
    if( ni ) ni->push_back(conv);
    return conv;
  }

  if ( ty->isIntegerTy() && (ty->getPrimitiveSizeInBits()<=64) ) {
    Instruction *conv = CastInst::CreateSExtOrBitCast(value, int64Ty);
    out << conv;
    if( ni ) ni->push_back(conv);
    return conv;
  }

  if(ty->isPointerTy()) {
    Instruction *ptrcast = CastInst::CreatePointerCast(value, int64Ty);
    out << ptrcast;
    if( ni ) ni->push_back(ptrcast);
    return ptrcast;
  }

  if(ty->isIntegerTy(64))
    return value;

  assert(false && "Unimplemented");
}

Value *liberty::castIntToInt32Ty(Value *value, InstInsertPt &where, NewInstructions *ni)
{
  IntegerType *sty = cast< IntegerType >( value->getType() );
  Type *u32 = Type::getInt32Ty(sty->getContext());
  if( sty->getBitWidth() < 32 )
  {
    SExtInst *cast = new SExtInst(value, u32);
    where << cast;
    if( ni ) ni->push_back(cast);
    value = cast;
  }
  else if( sty->getBitWidth() > 32 )
  {
    TruncInst *cast = new TruncInst(value, u32);
    where << cast;
    if( ni ) ni->push_back(cast);
    value = cast;
  }

  return value;
}

Value *liberty::castPtrToVoidPtr(Value *ptr, InstInsertPt &where, NewInstructions *ni)
{
  Type *u8 = Type::getInt8Ty(ptr->getContext());
  Type *voidptr = PointerType::getUnqual( u8 );

  if( ptr->getType() != voidptr )
  {
    Instruction *cast = new BitCastInst(ptr,voidptr);
    where << cast;
    if( ni ) ni->push_back(cast);
    return cast;
  }

  return ptr;
}

Value *liberty::castFromInt64Ty(Type *ty, Value *value, InstInsertPt &out, NewInstructions *ni) {

  if(ty->isIntegerTy(64))
    return value;

  if(ty->isPointerTy()) {
    Instruction *ptrcast = new IntToPtrInst(value, ty);
    out << ptrcast;
    if( ni ) ni->push_back(ptrcast);
    return ptrcast;
  }

  if(ty->isDoubleTy() || ty->isIntegerTy(32)) {
    Instruction *bitcast = CastInst::CreateTruncOrBitCast(value, ty);
    out << bitcast;
    if( ni ) ni->push_back(bitcast);
    return bitcast;
  }

  if(ty->isFloatTy()) {

    Type *int32Ty = Type::getInt32Ty(ty->getContext());
    Instruction *trunc = CastInst::CreateTruncOrBitCast(value, int32Ty);
    out << trunc;
    if( ni ) ni->push_back(trunc);

    Type *floatTy = Type::getFloatTy(ty->getContext());
    Instruction *conv = CastInst::CreateTruncOrBitCast(trunc, floatTy);
    out << conv;
    if( ni ) ni->push_back(conv);

    return conv;
  }

  assert(false && "Unimplemented");
}
