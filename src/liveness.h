#ifndef RCHK_LIVENESS_H
#define RCHK_LIVENESS_H

#include "common.h"

#include <unordered_map>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

using namespace llvm;

// which variables are live after the given instruction executes
typedef std::unordered_map<Instruction*, VarsSetTy> LiveVarsTy;

LiveVarsTy findLiveVariables(Function *f);

#endif
