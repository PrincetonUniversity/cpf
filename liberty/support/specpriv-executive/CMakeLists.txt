file(GLOB SRCS
    "*.c"
)

# Compilation flags
set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS 
    "-O3 -fPIC -flto -std=c11") #"-D_GNU_SOURCE"
set(PassName "specprivexecutive")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
#include(HandleLLVMOptions)
include(AddLLVM)

include_directories(./)
include_directories(../smtx)

add_llvm_library(${PassName} STATIC ${SRCS})
add_llvm_library(${PassName}_shared SHARED ${SRCS})
set_target_properties(${PassName}_shared PROPERTIES OUTPUT_NAME ${PassName}) 
#add_llvm_library(${PassName} SHARED ${SRCS}) # This is to generate libxxx.so
install(TARGETS ${PassName} ${PassName}_shared
        DESTINATION lib)
