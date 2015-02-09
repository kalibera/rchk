#ifndef RCHK_ALLOCATORS_H
#define RCHK_ALLOCATORS_H

#include "common.h"
#include "cgclosure.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

using namespace llvm;

const std::string gcFunction = "R_gc_internal";

Function *getGCFunction(Module *m);
unsigned getGCFunctionIndex(FunctionsInfoMapTy& functionsMap, Module *m);

bool mayBeAllocator(Function& f);
void findPossibleAllocators(Module *m, FunctionsSetTy& possibleAllocators);

#endif