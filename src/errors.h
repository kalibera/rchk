#ifndef RCHK_ERRORS_H
#define RCHK_ERRORS_H

#include "common.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

using namespace llvm;

bool isErrorFunction(Function *fun, FunctionsSetTy *knownErrorFunctions);
void findErrorFunctions(Module *m, FunctionsSetTy& errorFunctions);
void findErrorBasicBlocks(Function *fun, FunctionsSetTy *knownErrorFunctions, BasicBlocksSetTy& errorBlocks);

#endif
