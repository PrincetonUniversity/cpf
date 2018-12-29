##===- projects/sample/lib/sample/Makefile -----------------*- Makefile -*-===##

#
# Indicate where we are relative to the top of the source tree.
#
LEVEL=../..

#
# Give the name of a library.  This will build a dynamic version.
#
LIBRARYNAME=SLAMP
BUILD_RELINKED=1
SHARED_LIBRARY=1
DIRS=SLAMPlib/hooks

CFlags += -fPIC

#
# Include Makefile.common so we know what to do.
#
include $(LEVEL)/Makefile.common
