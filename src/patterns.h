#ifndef RCHK_PATTERNS_H
#define RCHK_PATTERNS_H

#include "common.h"

#include <llvm/IR/Instruction.h>

using namespace llvm;

// integer variable used as a guard

bool isTypeCheck(Value *inst, bool& positive, AllocaInst*& var, unsigned& type);
bool isCallThroughPointer(Value *inst);

#endif
