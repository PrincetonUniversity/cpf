CC=g++
AR=ar
CLANG=clang
LLVM_LINK=llvm-link

WARNINGS=-Wall \
	-Wextra \
	-Wconversion \
	-Wshadow \
	-Wcast-qual \
	-Winit-self \
	-Wmissing-include-dirs \
	-Winline \
	-Wno-unused-parameter

CXXFLAGS=-O3 -pedantic -fPIC -Iinclude $(DEFINE) $(WARNINGS) -msse4.1
#CXXFLAGS=-O0 -g -pedantic -fPIC -Iinclude $(DEFINE) $(WARNINGS) -msse4.1  

SRCS    := $(shell find . -name '*.cpp')
#SRCS     := control.cpp debug.cpp loopevent.cpp pcb.cpp private.cpp speculation.cpp strategy.cpp sw_queue/sw_queue.cpp smtx/memops.cpp smtx/malloc.cpp smtx/packet.cpp smtx/smtx.cpp smtx/communicate.cpp smtx/units.cpp
SRCDIRS := $(shell find . -name '*.cpp' -exec dirname {} \; | uniq)
#SRCDIRS  := sw_queue smtx
OBJDIR   := obj
OBJS     := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRCS))
LLVMOBJS := $(patsubst %.cpp,$(OBJDIR)/%.bc,$(SRCS))
LIBDIR   := lib

all: buildrepo $(LIBDIR)/libsmtx.a $(LIBDIR)/libsmtx.so $(LIBDIR)/libsmtx.bca

clean:
	rm -rf $(OBJDIR) $(LIBDIR)

$(LIBDIR)/libsmtx.a: $(OBJS)
$(LIBDIR)/libsmtx.so: $(OBJS)
$(LIBDIR)/libsmtx.bca: $(LLVMOBJS)

buildrepo:
	@$(call make-repo)

define make-repo
   for dir in $(SRCDIRS); \
   do \
    mkdir -p $(OBJDIR)/$$dir; \
   done
   mkdir -p $(LIBDIR)
endef

$(OBJDIR)/%.o: %.cpp
	$(CC) $(CXXFLAGS) $< -c -o $@

$(OBJDIR)/%.bc: %.cpp
	$(CLANG) $(CXXFLAGS) $< -c -emit-llvm -o $@

$(LIBDIR)/%.a :
	$(AR) rcs $@ $^

$(LIBDIR)/%.so :
	$(CC) $(CFLAGS) $(LDFLAGS) -shared $^ -o $@

$(LIBDIR)/%.bca :
	$(LLVM_LINK) -o $@ $^
