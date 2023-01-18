file(GLOB SRCS
    "*.cpp"
)
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "/u/ziyangx/test/nng/nng-1.5.2/install/lib/cmake")

# add CMAKE_PREFIX_PATH 
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

# Compilation flags
# set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS "-Wl,-save-temps -std=c++17 -Wno-inline -O3 -fexceptions")# -emit-llvm")
set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS "-std=c++17 -Wno-inline -O3 -fexceptions")# -emit-llvm")
set(PassName "slamp_hooks_nng")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(nng REQUIRED CONFIG)

# list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
#include(HandleLLVMOptions)
# include(AddLLVM)


include_directories(./
        /u/ziyangx/test/nng/nng-1.5.2/install/include)

add_library(${PassName} SHARED ${SRCS})
# target_link_libraries(${PassName} nng::nng)
# add_llvm_library(${PassName}_shared SHARED ${SRCS})
# set_target_properties(${PassName}_shared PROPERTIES OUTPUT_NAME ${PassName})
# set_property(TARGET ${PassName} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
#add_llvm_library(${PassName} SHARED ${SRCS}) # This is to generate libxxx.so
