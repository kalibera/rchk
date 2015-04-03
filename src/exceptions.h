#ifndef RCHK_EXCEPTIONS_H
#define RCHK_EXCEPTIONS_H

#include "common.h"
#include "callocators.h"

#include <llvm/IR/Function.h>

using namespace llvm;

// the function returns an (implicitly) protected object, even though it may allocate it
bool isKnownNonAllocator(Function *f);
bool isKnownNonAllocator(CalledFunctionTy *f);

#endif
