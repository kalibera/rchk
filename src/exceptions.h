#ifndef RCHK_EXCEPTIONS_H
#define RCHK_EXCEPTIONS_H

#include "common.h"
#include "callocators.h"

#include <llvm/IR/Function.h>

using namespace llvm;

// the function returns an (implicitly) protected object, even though it may allocate it
bool isKnownNonAllocator(Function *f);
bool isKnownNonAllocator(const CalledFunctionTy *f);

// lets assume this function does not allocate (even though it perhaps could, but it
// would lead to too pedantic tool behavior)
bool isAssertedNonAllocating(Function *f);
bool isAssertedNonAllocating(const CalledFunctionTy *f);

// these functions are too complex to allow tracking of SEXP guards at the moment
bool avoidSEXPGuardsFor(Function *f);
bool avoidSEXPGuardsFor(const CalledFunctionTy *f);

// these functions are too complex to allow tracking of int guards at the moment
bool avoidIntGuardsFor(Function *f);
bool avoidIntGuardsFor(const CalledFunctionTy *f);

bool protectsArguments(Function *f);
bool protectsArguments(const CalledFunctionTy *f);

#endif
