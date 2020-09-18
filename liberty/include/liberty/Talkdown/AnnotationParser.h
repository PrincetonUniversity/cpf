#pragma once

#include "Annotation.h"

namespace AutoMP
{
  AnnotationSet parseAnnotationsForInst(const llvm::Instruction *i);
};
