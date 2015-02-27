
# ---------------------

# Set the root directory for LLVM here

LLVM := /home/tomas/work/opt/clang+llvm-3.5.0-x86_64-linux-gnu

# r-lnx400, LLVM 3.5 
#LLVM := /var/scratch/tomas/opt/llvm/inst

# ---------------------

LLVMC := $(LLVM)/bin/llvm-config

CPPFLAGS := $(shell $(LLVMC) --cppflags)
CXXFLAGS := $(shell $(LLVMC) --cxxflags) -O3 -g3 -MMD
LDFLAGS := $(shell $(LLVMC) --ldflags)
LDLIBS := $(shell $(LLVMC) --libs --system-libs) 

# note: --system-libs is supported by LLVM 3.5
#-lz -lpthread -ledit -ltinfo -ldl -lm

CXX := $(LLVM)/bin/clang++
LINK.o = $(LINK.cc) # link with C++ compiler by default

SOURCES := $(wildcard *.cpp)
DEPENDS := $(SOURCES:.cpp=.d)
OBJECTS := $(SOURCES:.cpp=.o)

TOOLS := errcheck sfpcheck maacheck bcheck ueacheck alloccheck

all: $(TOOLS)

alloccheck: alloccheck.o allocators.o errors.o cgclosure.o common.o

errcheck: errcheck.o errors.o common.o

sfpcheck: sfpcheck.o allocators.o errors.o cgclosure.o common.o

maacheck: maacheck.o allocators.o errors.o cgclosure.o common.o

bcheck: bcheck.o state.o allocators.o errors.o guards.o cgclosure.o linemsg.o common.o

ueacheck: ueacheck.o allocators.o errors.o cgclosure.o common.o

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TOOLS)

-include $(DEPENDS)
