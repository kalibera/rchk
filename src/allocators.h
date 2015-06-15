#ifndef RCHK_ALLOCATORS_H
#define RCHK_ALLOCATORS_H

#include "common.h"
#include "cgclosure.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

// these allocators are found using a path-insensitive algorithm which does
// not depend on callocators; on the contrary, this allocator detection is
// used by callocators for performance reasons
//
// more precise (even non-called) allocators can be obtained through
// callocators, because there may be some context deeper in the call-tree

using namespace llvm;

const std::string gcFunction = "R_gc_internal";

Function *getGCFunction(Module *m);
unsigned getGCFunctionIndex(FunctionsInfoMapTy& functionsMap, Module *m);

bool mayBeAllocator(Function& f);
void findPossibleAllocators(Module *m, FunctionsSetTy& possibleAllocators);

bool isAllocatingFunction(Function *fun, FunctionsInfoMapTy& functionsMap, unsigned gcFunctionIndex);
void findAllocatingFunctions(Module *m, FunctionsSetTy& allocatingFunctions);

void findPossiblyReturnedVariables(Function *f, VarsSetTy& possiblyReturned);
void getWrappedAllocators(Function *f, FunctionsSetTy& wrappedAllocators, Function* gcFunction);

bool isKnownNonAllocator(Function *f);

#endif
