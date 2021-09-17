
# ---------------------

HOST := $(shell hostname -s)

ifeq ($(HOST), prg)
  LLVM := /usr
  CXX := g++
#  CXX := $(LLVM)/bin/clang++
  BCHECK_MAX_STATES := 800000
  CALLOCATORS_MAX_STATES := 1000000

else ifeq ($(HOST), ra)
  LLVM = /usr
  CXX := g++
#  CXX := $(LLVM)/bin/clang++
  BCHECK_MAX_STATES := 800000
  CALLOCATORS_MAX_STATES := 1000000

else ifeq ($(HOST), r-lnx400)
  LLVM := /usr
  CXX := g++
#  CXX := $(LLVM)/bin/clang++
  BCHECK_MAX_STATES := 6000000
  CALLOCATORS_MAX_STATES := 1000000

else ifeq ($(HOST), pod)
  LLVM = /usr/lib/llvm-8
  CXX := g++
#  CXX := $(LLVM)/bin/clang++
  BCHECK_MAX_STATES := 800000
  CALLOCATORS_MAX_STATES := 1000000

else
  # ------  CUSTOMIZE HERE --------- 
  # or provide these variables to make
  
  # use LLVM=/usr with Ubuntu 16.04, 17.04, 18.04 (to use system
  # installation of LLVM) or LLVM=/usr/lib/llvm-4.0 to select LLVM 4.0
  # specifically; WLLVM does not support compiler names like clang-4.0
  LLVM ?= /usr
  
  # use with Ubuntu 16.04 (not needed with newer Ubuntu/LLVM)
  # EXTRACXXFLAGS := -I/usr/include/llvm-3.8/
  
  ifeq ($(wildcard $(LLVM)),)
    $(error Please customize your Makefile here. Please set the home directory for LLVM)
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

CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O3 -g3 -MMD $(HOSTFLAGS) $(EXTRACXXFLAGS)

# for debugging
#CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O0 -gdwarf-2 -g3 -MMD $(HOSTFLAGS) $(EXTRACXXFLAGS)
#CXXFLAGS := $(filter-out -O2, $(CXXFLAGS))

# for GCC, debugging (bounds checking for containers)
#   note: LLVM has to be compiled with this option as well
#CXXFLAGS := $(CXXFLAGS) -D_GLIBCXX_DEBUG

# For address sanitizer
#   also have to set environment variable,
#     export ASAN_SYMBOLIZER_PATH=$(LLVM)/bin/llvm-symbolizer
#   can also set variable
#     ASAN_OPTIONS=detect_stack_use_after_return=1
#CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O1 -g3 -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -fno-optimize-sibling-calls -MMD $(HOSTFLAGS)
#CXXFLAGS := $(filter-out -O2, $(CXXFLAGS))

# For address sanitizer with fewer optimizations
#CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O0 -gdwarf-2 -g3 -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -fno-optimize-sibling-calls -MMD $(HOSTFLAGS) $(EXTRACXXFLAGS)
#CXXFLAGS := $(filter-out -O2, $(CXXFLAGS))

# for GCC, which does not support this warning
CXXFLAGS := $(filter-out -Wcovered-switch-default, $(CXXFLAGS))
CXXFLAGS := $(filter-out -Wstring-conversion, $(CXXFLAGS))
CXXFLAGS := $(filter-out -Werror=unguarded-availability-new, $(CXXFLAGS))

LDFLAGS := $(shell $(LLVMC) --ldflags)
LDLIBS := $(shell $(LLVMC) --libs --system-libs) 

# For address sanitizer
#LDFLAGS := $(shell $(LLVMC) --ldflags) -fsanitize=address
# ASAN with GCC
#LDLIBS := $(shell $(LLVMC) --libs --system-libs) -lasan 


LINK.o = $(LINK.cc) # link with C++ compiler by default

SOURCES := $(wildcard *.cpp)
DEPENDS := $(SOURCES:.cpp=.d)
OBJECTS := $(SOURCES:.cpp=.o)
DWOBJECTS := $(SOURCES:.cpp=.dwo)
SOBJECTS := $(filter-out %check.o, $(OBJECTS))

TOOLS := errcheck symcheck sfpcheck csfpcheck maacheck bcheck ueacheck alloccheck glcheck veccheck cgcheck fficheck

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

fficheck: fficheck.o $(SOBJECTS)

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TOOLS) $(DWOBJECTS)

info:
	@echo "CPPFLAGS: $(CPPFLAGS)"
	@echo "CXXFLAGS: $(CXXFLAGS)"

-include $(DEPENDS)
