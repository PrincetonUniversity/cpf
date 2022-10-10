cmake_minimum_required (VERSION 3.6.2 FATAL_ERROR)

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "/u/ziyangx/test/boost/boost_1_80_0/install/lib/cmake")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


find_package(Boost 1.80.0 REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})

add_executable (consumer consumer.cpp ./ProfilingModules/DependenceModule.cpp)
target_link_libraries(consumer LINK_PUBLIC 
	${Boost_LIBRARIES}
	rt
	Threads::Threads
)
