##===- projects/sample/lib/sample/Makefile -----------------*- Makefile -*-===##

#
# Indicate where we are relative to the top of the source tree.
#
LEVEL=../..

#
# Give the name of a library.  This will build a dynamic version.
#
LIBRARYNAME=specprivexecutive
DONT_BUILD_RELINKED=1
SHARED_LIBRARY=1

CFLAGS += -O3 -fPIC -I$(LIBERTY_SMTX_DIR) -flto -std=c11

#
# Include Makefile.common so we know what to do.
#
include $(LEVEL)/Makefile.common

