#ifndef GET_INST_DBG_INFO_H
#define GET_INST_DBG_INFO_H

#include "llvm/IR/Instruction.h"

#include <string>

namespace liberty {
  bool GetInstDbgInfo(const llvm::Instruction *I,
                      unsigned &LineNo,
                      std::string &File,
                      std::string &Dir);
}

#endif /* GET_INST_DBG_INFO_H */
