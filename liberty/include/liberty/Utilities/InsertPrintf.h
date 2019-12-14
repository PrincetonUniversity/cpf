#ifndef LLVM_LIBERTY_INSERT_PRINTF_H
#define LLVM_LIBERTY_INSERT_PRINTF_H

#include "llvm/IR/LLVMContext.h"

#include "liberty/Utilities/InstInsertPt.h"

#include <list>

namespace liberty
{
  using namespace llvm;

  // Return a format string for the specified llvm type
  // e.g. i32 -> '%d'
  // Returns null if no format string is appropriate
  StringRef getFormatStringForType(Type *ty);

  // Gets a constant which represents a string literal.
  // Useful when passing a constant string to a function.
  Constant *getStringLiteralExpression(Module &m, const std::string &src);

  // Insert a call to printf
  template <class InputIterator>
  Value *insertPrintf(InstInsertPt &where, const std::string &format,
    const InputIterator &arg_begin, const InputIterator &arg_end,
    bool flush=false)
  {
    Module *module = where.getModule();

    LLVMContext &Context = module->getContext();

    Type *intty = Type::getInt32Ty(Context);
    Type *charPtr = PointerType::getUnqual( Type::getInt8Ty(Context) );

    std::vector<Type*> formals(1);
    formals[0] = charPtr;
    FunctionType *sig_printf = FunctionType::get(
      intty, formals, true);

    FunctionCallee wrapper = module->getOrInsertFunction("printf", sig_printf);
    Constant *fcn_printf = cast<Constant>(wrapper.getCallee());

    std::vector<Value*> actuals;
    actuals.push_back( getStringLiteralExpression(*module, format) );
    actuals.insert( actuals.end(), arg_begin, arg_end );

    Instruction *call = CallInst::Create(
      fcn_printf, actuals);
    where << call;

    if( flush )
    {
      Type *filePtrType = PointerType::getUnqual( module->getTypeByName("struct._IO_FILE") );
      Value *gv_stdout = module->getOrInsertGlobal("stdout", filePtrType);

      formals.resize(1);
      formals[0] = filePtrType;
      FunctionType *sig_fflush = FunctionType::get(
        intty, formals, false);

      FunctionCallee wrapper = module->getOrInsertFunction("fflush", sig_fflush);
      Constant *fcn_fflush = cast<Constant>(wrapper.getCallee());

      Instruction *load = new LoadInst(gv_stdout);
      actuals.resize(1);
      actuals[0] = load;

      where << load
            << CallInst::Create(fcn_fflush, actuals);
    }

    return call;
  }

  // Insert a call to printf that takes only one argument after the format string
  Value *insertPrintf(
    InstInsertPt &where, const std::string &format, Value *oneArg,
    bool flush = false);

  // Insert a call to printf that takes no arguments after the format string
  Value *insertPrintf(InstInsertPt &where, const std::string &format,
    bool flush = false);

}

#endif // LLVM_LIBERTY_INSERT_PRINTF_H
