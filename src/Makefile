
# ---------------------

HOST := $(shell hostname -s)

ifeq ($(HOST), prg)
  #LLVM := /home/tomas/work/opt/clang+llvm-3.5.0-x86_64-linux-gnu
  LLVM := /home/tomas/work/opt/clang+llvm-3.6.1-x86_64-linux-gnu
  #LLVM := /home/tomas/work/opt/inst3.6.1
  BCHECK_MAX_STATES := 1500000
  CALLOCATORS_MAX_STATES := 1000000
else ifeq ($(HOST), r-lnx400)
  LLVM := /var/scratch/tomas/opt/llvm/clang+llvm-3.61-x86_64-fedora20
  BCHECK_MAX_STATES := 6000000
  CALLOCATORS_MAX_STATES := 1000000
else
  # ------  CUSTOMIZE HERE --------- 
  $(error Please customize your Makefile here. Please set the home directory for LLVM 3.5.0)
  #LLVM := /home/tomas/work/opt/clang+llvm-3.5.0-x86_64-linux-gnu
  
  # please update the "MAX_STATES", these defaults seem to work fine for 8G RAM laptop
  BCHECK_MAX_STATES := 3000000
  CALLOCATORS_MAX_STATES := 1000000  
endif

# ---------------------

LLVMC := $(LLVM)/bin/llvm-config

HOSTFLAGS := -DBCHECK_MAX_STATES=$(BCHECK_MAX_STATES) -DCALLOCATORS_MAX_STATES=$(CALLOCATORS_MAX_STATES)

CPPFLAGS := $(shell $(LLVMC) --cppflags)
CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O3 -g3 -MMD $(HOSTFLAGS)
#CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O0 -g3 -MMD $(HOSTFLAGS)
LDFLAGS := $(shell $(LLVMC) --ldflags)
LDLIBS := $(shell $(LLVMC) --libs --system-libs) 


CXX := $(LLVM)/bin/clang++

# switch to GCC (it still seems to generate a tiny bit faster code)
#CXX := g++

LINK.o = $(LINK.cc) # link with C++ compiler by default

SOURCES := $(wildcard *.cpp)
DEPENDS := $(SOURCES:.cpp=.d)
OBJECTS := $(SOURCES:.cpp=.o)

TOOLS := errcheck symcheck sfpcheck csfpcheck maacheck bcheck ueacheck alloccheck

all: $(TOOLS)

alloccheck: alloccheck.o allocators.o callocators.o errors.o cgclosure.o symbols.o common.o linemsg.o guards.o state.o patterns.o exceptions.o

errcheck: errcheck.o errors.o common.o

sfpcheck: sfpcheck.o allocators.o errors.o cgclosure.o common.o exceptions.o patterns.o

csfpcheck: csfpcheck.o allocators.o callocators.o errors.o cgclosure.o symbols.o common.o linemsg.o guards.o state.o patterns.o exceptions.o

maacheck: maacheck.o allocators.o errors.o cgclosure.o common.o exceptions.o patterns.o

bcheck: bcheck.o state.o allocators.o balance.o errors.o freshvars.o guards.o cgclosure.o linemsg.o common.o patterns.o callocators.o patterns.o symbols.o exceptions.o liveness.o

ueacheck: ueacheck.o allocators.o errors.o cgclosure.o common.o exceptions.o patterns.o

symcheck: symcheck.o symbols.o common.o

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TOOLS)

-include $(DEPENDS)
