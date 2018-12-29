#ifndef COUNT_H
#define COUNT_H

namespace liberty {

  template<typename InstType, typename Iterator>
  unsigned count(const Iterator &B, const Iterator E) {

    unsigned count = 0;

    for(Iterator it = B; it != E; ++it) {
      if(llvm::isa<InstType>(*it))
        ++count;
    }

    return count;
  }

  template<typename FunType, typename Iterator>
  unsigned count(const FunType &isCounted, const Iterator &B, const Iterator &E) {

    unsigned count = 0;

    for(Iterator it = B; it != E; ++it) {
      if(isCounted(*it))
        ++count;
    }

    return count;
  }
}

#endif /* COUNT_H */
