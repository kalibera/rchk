
struct VrfStateTy;
class CalledModuleTy;
void findVectorReturningFunctions(CalledModuleTy *cm);

#ifndef RCHK_VECTORS_H
#define RCHK_VECTORS_H

#include "common.h"
#include "guards.h"
#include "callocators.h"

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

bool isVectorType(unsigned type);
bool isVectorProducingCall(Value *inst);
bool isVectorOnlyVarOperation(Value *inst, AllocaInst*& var);

bool isVectorProducingCall(Value *inst, CalledModuleTy *cm, SEXPGuardsChecker* sexpGuardsChecker, SEXPGuardsTy *sexpGuards);
void printVectorReturningFunctions(CalledModuleTy *cm);
void freeVrfState(VrfStateTy *vrfState);

#endif
