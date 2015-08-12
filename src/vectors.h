#ifndef RCHK_VECTORS_H
#define RCHK_VECTORS_H

#include "common.h"

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>

using namespace llvm;

// vector guards, like "isVector" and other

bool isVectorGuard(Function *f);
bool trueForVector(Function *f);
bool trueForNonVector(Function *f);
bool falseForVector(Function *f);
bool falseForNonVector(Function *f);
bool impliesVectorWhenTrue(Function *f);
bool impliesVectorWhenFalse(Function *f);

bool isVectorProducingCall(Value *inst);
bool isVectorOnlyVarOperation(Value *inst, AllocaInst*& var);

#endif
