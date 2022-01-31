file(GLOB SRCS
    "SLAMP.cpp"
    "SLAMPLoad.cpp"
)

# Compilation flags
set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS "-std=c++17 -fPIC -g -Wall")
set(PassName "SLAMP")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
#include(HandleLLVMOptions)
include(AddLLVM)

include_directories(./)

#add_llvm_library(${PassName} MODULE ${SRCS})
add_llvm_library(${PassName} SHARED ${SRCS}) # This is to generate libxxx.so

add_subdirectory(SLAMPlib/hooks)
