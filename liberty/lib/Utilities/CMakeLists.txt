file(GLOB SRCS
    "*.cpp"
)

# Compilation flags
execute_process(COMMAND which tred OUTPUT_VARIABLE TRED_BINARY OUTPUT_STRIP_TRAILING_WHITESPACE)
set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS "-std=c++17 -fPIC -DPATH_TO_TRED=\\\"${TRED_BINARY}\\\"")
set(PassName "Util")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
#include(HandleLLVMOptions)
include(AddLLVM)

include_directories(./)

#add_llvm_library(${PassName} MODULE ${SRCS})
add_llvm_library(${PassName} SHARED ${SRCS}) # This is to generate libxxx.so
