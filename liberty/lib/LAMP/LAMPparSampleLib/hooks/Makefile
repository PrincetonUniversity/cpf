LEVEL=../../../..

LIBRARYNAME=lamp_hooks_par_sample

include $(LEVEL)/Makefile.common

LIBERTY_SMTX_DIR ?= 0

ifeq ($(LIBERTY_SMTX_DIR),0)
	CPP.BaseFlags += "-DHAS_SMTX=0"
else
	CPP.BaseFlags += "-DHAS_SMTX=1"
endif

CXXFLAGS+=-fexceptions -I$(LIBERTY_SMTX_DIR) -O3 -Wno-inline
