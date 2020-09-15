#include "liberty/Talkdown/Annotation.h"

namespace AutoMP
{
  llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const Annotation &a)
  {
    os << "Loop " << a.getLoop() << " | " << a.getKey() << " : " << a.getValue() << "\n";
    return os;
  }

  llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const std::pair<const llvm::Instruction *, const AnnotationSet &> &p)
  {
    os << "---- Annotations for each instruction ----\n";
    os << *(p.first) << ":\n";
    for ( auto &a : p.second )
    {
      os << a;
    }

    return os;
  }

  /**** XXX Below this line is unimplemented code! ***/
  ReduxAnnotation::ReduxAnnotation()
  : Annotation()
  {

  }
} // namespace llvm
