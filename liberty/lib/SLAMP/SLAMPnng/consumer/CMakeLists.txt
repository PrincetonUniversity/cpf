#
# Copyright 2018 Capitar IT Group BV <info@capitar.com>
# Copyright 2018 Staysail Systems, Inc. <info@staysail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

cmake_minimum_required (VERSION 2.8.7)

# add CMAKE_PREFIX_PATH
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "/u/ziyangx/test/nng/nng-1.5.2/install/lib/cmake")
project(raw)


set(PARALLEL 128 CACHE STRING "Parallelism (min 4, max 1000)")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(nng CONFIG REQUIRED)

add_executable(raw raw.c)
target_link_libraries(raw nng::nng)
target_compile_definitions(raw PRIVATE -DPARALLEL=${PARALLEL})
