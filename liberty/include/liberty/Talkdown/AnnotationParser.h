#pragma once

#include "Annotation.h"

namespace AutoMP
{
  AnnotationSet parseAnnotationsForInst(llvm::Instruction *i);
};
