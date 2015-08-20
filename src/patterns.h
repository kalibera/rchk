#ifndef RCHK_PATTERNS_H
#define RCHK_PATTERNS_H

#include "common.h"

#include <llvm/IR/Instruction.h>

using namespace llvm;

// integer variable used as a guard
bool isTypeCheck(Value *inst, bool& positive, AllocaInst*& var, unsigned& type);

bool isCallThroughPointer(Value *inst);

typedef std::unordered_set<Value*> ValuesSetTy;
AllocaInst* originsOnlyFromLoad(Value *inst);

ValuesSetTy valueOrigins(Value *inst);

bool isAllocVectorOfKnownType(Value *inst, unsigned& type);

bool isBitCastOfVar(Value *inst, AllocaInst*& var, Type*& type);

bool isCallPassingVar(Value *inst, AllocaInst*& var, std::string& fname);

bool isStoreToStructureElement(Value *inst, std::string structType, std::string elementType, AllocaInst*& var);

bool aliasesVariable(Value *useInst, AllocaInst *proxyVar, AllocaInst*& origVar);

bool findOnlyStoreTo(AllocaInst* var, StoreInst*& definingStore);

#endif
