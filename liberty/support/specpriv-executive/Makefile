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
CFLAGS_MANUAL += -O3 -fPIC -I$(LIBERTY_SMTX_DIR) -flto -D_GNU_SOURCE

manual_srcs = $(wildcard *.c)
manual_bcs = $(patsubst %.c,%.bc,$(manual_srcs))

compile_manually: $(manual_bcs)
	@echo $(manual_bcs)
	@echo $(CFLAGS)
	llvm-link $(manual_bcs) -o specprivexecutive.bc

%.bc : %.c
	clang $(CFLAGS_MANUAL) -emit-llvm -c $< -o $@

#
# Include Makefile.common so we know what to do.
#
# Comment this line out below to use compile_manually target lmao
include $(LEVEL)/Makefile.common

# specprivexecutive.bc
