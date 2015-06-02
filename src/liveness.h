#ifndef RCHK_LIVENESS_H
#define RCHK_LIVENESS_H

#include "common.h"

#include <unordered_map>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

using namespace llvm;

struct VarsLiveness {
  VarsSetTy possiblyUsed; // the variable is read on some path
  VarsSetTy possiblyKilled; // the variable is overwritten and not read before that, or it is ignored, on some path
  
  bool isPossiblyUsed(AllocaInst* var) {
    return possiblyUsed.find(var) != possiblyUsed.end();
  }
  
  bool isPossiblyKilled(AllocaInst* var) {
    return possiblyKilled.find(var) != possiblyKilled.end();
  }
  
  bool isDefinitelyUsed(AllocaInst* var) { // we are certain the variable is used (loaded)
    return !isPossiblyKilled(var);
  }
};

// which variables are live after the given instruction executes
typedef std::unordered_map<Instruction*, VarsLiveness> LiveVarsTy;
LiveVarsTy findLiveVariables(Function *f);

#endif
