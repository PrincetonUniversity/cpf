#pragma once

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"
#include <string>
#include <set>

namespace llvm
{

  class Annotation
  {
  public:

  private:
    LoopInfo *loop_info;
    std::string key;
    std::string value;

  };

  class ReduxAnnotation : public Annotation
  {
    enum class Type
    {
      Sum,
      Product
    };

  private:
    Type type;
  };

} // namespace llvm
