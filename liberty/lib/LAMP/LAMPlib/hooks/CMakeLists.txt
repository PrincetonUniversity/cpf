file(GLOB SRCS
    "*.cpp"
)

# Compilation flags
set_source_files_properties(${CATPassSrc} PROPERTIES COMPILE_FLAGS "-std=c++17 -Wno-inline -O3 -fexceptions")
set(PassName "lamp_hooks")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
#include(HandleLLVMOptions)
include(AddLLVM)

include_directories(./)

add_llvm_library(${PassName} SHARED ${SRCS})
#add_llvm_library(${PassName} SHARED ${SRCS}) # This is to generate libxxx.so
