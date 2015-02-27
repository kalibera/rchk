#ifndef RCHK_GUARDS_H
#define RCHK_GUARDS_H

#include "common.h"
#include "linemsg.h"
#include "state.h"

#include <map>

#include <llvm/IR/Instruction.h>

using namespace llvm;

// integer variable used as a guard

enum IntGuardState {
  IGS_ZERO = 0,
  IGS_NONZERO,
  IGS_UNKNOWN
};

typedef std::map<AllocaInst*,IntGuardState> IntGuardsTy;
struct StateWithGuardsTy;

std::string igs_name(IntGuardState igs);
IntGuardState getIntGuardState(IntGuardsTy& intGuards, AllocaInst* var);
bool isIntegerGuardVariable(AllocaInst* var);
bool isIntegerGuardVariable(AllocaInst* var, VarBoolCacheTy& cache);
bool handleStoreToIntGuard(StoreInst* store, VarBoolCacheTy& intGuardVarsCache, IntGuardsTy& intGuards, LineMessenger& msg);
bool handleBranchOnIntGuard(BranchInst* branch, VarBoolCacheTy& intGuardVarsCache, StateWithGuardsTy& s, LineMessenger& msg);

// SEXP - an "R pointer" used as a guard

enum SEXPGuardState {
  SGS_NIL = 0, // R_NilValue
  SGS_NONNIL,
  SGS_UNKNOWN
};

typedef std::map<AllocaInst*,SEXPGuardState> SEXPGuardsTy;

std::string sgs_name(SEXPGuardState sgs);
SEXPGuardState getSEXPGuardState(SEXPGuardsTy& sexpGuards, AllocaInst* var);
bool isSEXPGuardVariable(AllocaInst* var, GlobalVariable* nilVariable, Function* isNullFunction);
bool isSEXPGuardVariable(AllocaInst* var, GlobalVariable* nilVariable, Function* isNullFunction, VarBoolCacheTy& cache);
bool handleStoreToSEXPGuard(StoreInst* store, VarBoolCacheTy& sexpGuardVarsCache, SEXPGuardsTy& sexpGuards,
  GlobalVariable* nilVariable, Function* isNullFunction, LineMessenger& msg, FunctionsSetTy& possibleAllocators, bool USE_ALLOCATOR_DETECTION = false);
bool handleBranchOnSEXPGuard(BranchInst* branch, VarBoolCacheTy& sexpGuardVarsCache, StateWithGuardsTy& s,
  GlobalVariable* nilVariable, Function* isNullFunction, LineMessenger& msg);

// checking state with guards

struct StateWithGuardsTy : public StateBaseTy {
  IntGuardsTy intGuards;
  SEXPGuardsTy sexpGuards;
  
  StateWithGuardsTy(BasicBlock* bb, IntGuardsTy& intGuards, SEXPGuardsTy& sexpGuards): StateBaseTy(bb), intGuards(intGuards), sexpGuards(sexpGuards) {};
  StateWithGuardsTy(BasicBlock *bb): StateBaseTy(bb), intGuards(), sexpGuards() {};
  
  virtual StateWithGuardsTy* clone(BasicBlock *newBB) = 0;
  
  void dump(bool verbose);
};

#endif
