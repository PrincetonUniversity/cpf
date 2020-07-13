set(SRCS
    "commit.c"
    "version.c"
    "sw_queue.c"
    "us_recovery.c"
    )


set(FLAGS
  # Warning
    "-Wall"
	"-Wextra"
	"-Wconversion"
    "-Wshadow"
    "-Wcast-qual"
    "-Wstrict-prototypes"
    "-Winit-self"
    "-Wmissing-include-dirs"
    "-Wc++-compat"
	"-Winline"
   # Define
    "-D_BSD_SOURCE"
	"-D_POSIX_C_SOURCE=199309L"
	"-U_FORTIFY_SOURCE"
   # Other 
    "-std=c99 -g -O3 -pedantic -fPIC"
    )

string(REPLACE ";" " " FLAGS_STR "${FLAGS}")

set_source_files_properties(${SRCS} PROPERTIES COMPILE_FLAGS ${FLAGS_STR})
set(PassName "smtx")

include_directories(./)

add_library(${PassName} STATIC ${SRCS})
add_library(${PassName}_shared SHARED ${SRCS})
set_target_properties(${PassName}_shared PROPERTIES OUTPUT_NAME ${PassName}) 
install(TARGETS ${PassName} ${PassName}_shared
        DESTINATION lib)
