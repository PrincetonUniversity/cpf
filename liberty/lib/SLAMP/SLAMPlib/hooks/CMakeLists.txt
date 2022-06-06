file(GLOB SRCS
    "*.cpp"
)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_LINKER "ld.gold")

# Compilation flags
# set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS "-Wl,-save-temps -std=c++17 -Wno-inline -O3 -fexceptions")# -emit-llvm")
set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS "-flto -Wl,-save-temps -std=c++17 -Wno-inline -O3 -fexceptions")# -emit-llvm")
set(PassName "slamp_hooks")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
#include(HandleLLVMOptions)
include(AddLLVM)

include_directories(./)

add_llvm_library(${PassName} STATIC ${SRCS})
# add_llvm_library(${PassName}_shared SHARED ${SRCS})
# set_target_properties(${PassName}_shared PROPERTIES OUTPUT_NAME ${PassName})
set_property(TARGET ${PassName} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
#add_llvm_library(${PassName} SHARED ${SRCS}) # This is to generate libxxx.so
