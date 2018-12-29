##===- projects/sample/lib/sample/Makefile -----------------*- Makefile -*-===##

#
# Indicate where we are relative to the top of the source tree.
#
LEVEL=../..

#
# Give the name of a library.  This will build a dynamic version.
#
LIBRARYNAME=Util
BUILD_RELINKED=1
SHARED_LIBRARY=1

CFlags += -fPIC

# 'tred' is a utility which comes with graphviz.
# It simplifies graphs via transitive-reduction.
# If absent, fall-back to standard unix 'cat'
TRED_BINARY=${shell which tred || which cat }
CPP.BaseFlags += "-DPATH_TO_TRED=\"${TRED_BINARY}\""

HAS_UNWIND := ${shell				\
echo "\#include<libunwind.h>" |			\
${CC} -x c - -c -o /dev/null 2> /dev/null &&	\
echo 1 ||					\
echo 0 }

CPP.BaseFlags += "-DHAS_UNWIND=${HAS_UNWIND}"

#
# Include Makefile.common so we know what to do.
#
include $(LEVEL)/Makefile.common
