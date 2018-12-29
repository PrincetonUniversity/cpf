#include "liberty/Utilities/InsertPrintf.h"
#include <map>

namespace liberty
{
  using namespace llvm;

  static std::map<std::string, Constant*> slemap;

  Constant *getStringLiteralExpression(Module &m, const std::string &str)
  {
    if (slemap.count(str))
      return slemap[str];

    LLVMContext &Context = m.getContext();

    Constant *array =
      ConstantDataArray::getString(Context, str);

    GlobalVariable *strConstant = new GlobalVariable(m,
      array->getType(), true, GlobalValue::PrivateLinkage, array,
                                                     "__" + str);

    Constant * zero = ConstantInt::get( Type::getInt64Ty(Context), 0);
    Value * zeros[] = { zero, zero };
    ArrayRef<Value *> zerosRef(zeros, zeros + 2);

    //sot
    // TODO: not sure if I pass the correct type
    //slemap[str] = ConstantExpr::getInBoundsGetElementPtr(strConstant, zerosRef);
    slemap[str] = ConstantExpr::getInBoundsGetElementPtr(strConstant->getType()->getPointerElementType(),
                                        strConstant, zerosRef);
    return slemap[str];
  }

  Value *insertPrintf(
    InstInsertPt &where, const std::string &format, Value *oneArg,
    bool flush)
  {
    std::vector<Value*> actuals(1);
    actuals[0] = oneArg;

    return insertPrintf(where, format, actuals.begin(), actuals.end(), flush);
  }

  // Insert a call to printf that takes no arguments after the format string
  Value *insertPrintf(InstInsertPt &where, const std::string &format,
    bool flush)
  {
    std::vector<Value*> actuals(0);
    return insertPrintf(where, format, actuals.begin(), actuals.end(), flush);
  }

  StringRef getFormatStringForType(Type *ty)
  {
    if( ty->isIntegerTy(64) )
      return "%ld";

    if( ty->isIntegerTy() )
      return "%d";

    if( ty->isPointerTy() )
      return "%p";

    if( ty->isFloatTy() )
      return "%f";

    if( ty->isDoubleTy() )
      return "%lf";

    //sot
    //return 0;
    return "";
  }

}
