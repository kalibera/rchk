
# ---------------------

HOST := $(shell hostname -s)

ifeq ($(HOST), prg)
  LLVM := /usr
  CXX := g++
#  CXX := $(LLVM)/bin/clang++
  BCHECK_MAX_STATES := 800000
  CALLOCATORS_MAX_STATES := 1000000

else ifeq ($(HOST), r-lnx400)
  LLVM := /var/scratch/tomas/opt/llvm/clang+llvm-3.8.0-x86_64-fedora23
  CXX := g++
#  CXX := $(LLVM)/bin/clang++
  BCHECK_MAX_STATES := 6000000
  CALLOCATORS_MAX_STATES := 1000000

else
  # ------  CUSTOMIZE HERE --------- 
  # or provide these variables to make
  
  # use LLVM=/usr with Ubuntu 16.04 (to use system installation of LLVM)
  LLVM ?= /usr
  
  ifeq ($(wildcard $(LLVM)),)
    $(error Please customize your Makefile here. Please set the home directory for LLVM 3.8)
  endif

  # please update the "MAX_STATES", these defaults seem to work fine for 8G RAM laptop

  CXX ?= g++
  BCHECK_MAX_STATES ?= 3000000
  CALLOCATORS_MAX_STATES ?= 1000000
endif

# ---------------------

LLVMC := $(LLVM)/bin/llvm-config

HOSTFLAGS := -DBCHECK_MAX_STATES=$(BCHECK_MAX_STATES) -DCALLOCATORS_MAX_STATES=$(CALLOCATORS_MAX_STATES)

CPPFLAGS := $(shell $(LLVMC) --cppflags)

CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O3 -g3 -MMD $(HOSTFLAGS) -I/usr/include/llvm-3.8/
#CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O0 -g3 -MMD $(HOSTFLAGS) -I/usr/include/llvm-3.8/

# for GCC, which does not support this warning
CXXFLAGS := $(filter-out -Wcovered-switch-default, $(CXXFLAGS))

# For address sanitizer
#   also have to set environment variable,
#   export ASAN_SYMBOLIZER_PATH = $(LLVM)/bin/llvm-symbolizer
#CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O1 -g3 -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls -MMD $(HOSTFLAGS)

LDFLAGS := $(shell $(LLVMC) --ldflags)
LDLIBS := $(shell $(LLVMC) --libs --system-libs) 

LINK.o = $(LINK.cc) # link with C++ compiler by default

SOURCES := $(wildcard *.cpp)
DEPENDS := $(SOURCES:.cpp=.d)
OBJECTS := $(SOURCES:.cpp=.o)
SOBJECTS := $(filter-out %check.o, $(OBJECTS))

TOOLS := errcheck symcheck sfpcheck csfpcheck maacheck bcheck ueacheck alloccheck glcheck veccheck cgcheck

all: $(TOOLS)

alloccheck: alloccheck.o $(SOBJECTS)

errcheck: errcheck.o $(SOBJECTS)

sfpcheck: sfpcheck.o $(SOBJECTS)

cgcheck: cgcheck.o $(SOBJECTS)

csfpcheck: csfpcheck.o $(SOBJECTS)

maacheck: maacheck.o $(SOBJECTS)

bcheck: bcheck.o $(SOBJECTS)

ueacheck: ueacheck.o $(SOBJECTS)

symcheck: symcheck.o $(SOBJECTS)

veccheck: veccheck.o $(SOBJECTS)

glcheck: glcheck.o $(SOBJECTS)

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TOOLS)

info:
	@echo "CPPFLAGS: $(CPPFLAGS)"
	@echo "CXXFLAGS: $(CXXFLAGS)"

-include $(DEPENDS)
