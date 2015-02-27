
#include "guards.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>

using namespace llvm;

// integer guard is a local variable
//   which is compared at least once against a constant zero, but never compared against anything else
//   which may be stored to and loaded from
//   which is not used for anything else (e.g. an address of it is not taken)
//
// there has to be either at least two comparisons using the guard, 
//   or there has to be one comparison and at least one assignment of a constant
//   [in other cases, we would gain nothing by tracking the guard]
//
// these heuristics are important because the keep the state space small(er)
bool isIntegerGuardVariable(AllocaInst* var) {

  if (!IntegerType::classof(var->getAllocatedType()) || var->isArrayAllocation()) {
    return false;
  }
  
  unsigned nComparisons = 0;
  unsigned nConstantAssignments = 0;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (!l->hasOneUse()) {
        continue;
      }
      User *uu = l->user_back();
      if (CmpInst::classof(uu)) {
        CmpInst *ci = cast<CmpInst>(uu);
        if (!ci->isEquality()) {
          continue;
        }
        if (Constant::classof(ci->getOperand(0))) {
          ci->swapOperands();
        }
        if (ConstantInt::classof(ci->getOperand(1))) {
          ConstantInt *constOp = cast<ConstantInt>(ci->getOperand(1));
          if (constOp->isZero()) {
            nComparisons++;
          } else {
            return false;
          }
        }
      }
      continue;
    }
    if (StoreInst::classof(u)) {
      Value *v = (cast<StoreInst>(u))->getValueOperand();
      if (ConstantInt::classof(v)) {
        // guard = TRUE;
        nConstantAssignments++;
      }
      continue;
    }
    // this can e.g. be a call (taking address of the variable, which we do not support)
    return false;
  } 
  return nComparisons >= 2 || (nComparisons == 1 && nConstantAssignments > 0);
}

bool isIntegerGuardVariable(AllocaInst* var, VarBoolCacheTy& cache) {
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }

  bool res = isIntegerGuardVariable(var);
  
  cache.insert({var, res});
  return res;
}

std::string igs_name(IntGuardState gs) {
  switch(gs) {
    case IGS_ZERO: return "zero";
    case IGS_NONZERO: return "nonzero";
    case IGS_UNKNOWN: return "unknown";
  }
}

IntGuardState getIntGuardState(IntGuardsTy& intGuards, AllocaInst* var) {
  auto gsearch = intGuards.find(var);
  if (gsearch == intGuards.end()) {
    return IGS_UNKNOWN;
  } else {
    return gsearch->second;
  }
}

bool handleStoreToIntGuard(StoreInst* store, VarBoolCacheTy& intGuardVarsCache, IntGuardsTy& intGuards, LineMessenger& msg) {
  Value* storePointerOp = store->getPointerOperand();
  Value* storeValueOp = store->getValueOperand();
  
  // intguard = ...
  if (!AllocaInst::classof(storePointerOp)) {
    return false;
  }
  AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
  if (!isIntegerGuardVariable(storePointerVar, intGuardVarsCache)) { 
    return false;
  }
  IntGuardState newState;
  if (ConstantInt::classof(storeValueOp)) {
    ConstantInt* constOp = cast<ConstantInt>(storeValueOp);
    if (constOp->isZero()) {
      newState = IGS_ZERO;
      msg.debug("integer guard variable " + storePointerVar->getName().str() + " set to zero", store);
    } else {
      newState = IGS_NONZERO;
      msg.debug("integer guard variable " + storePointerVar->getName().str() + " set to nonzero", store);
    }
  } else {
    // FIXME: could add support for intguarda = intguardb, if needed
    newState = IGS_UNKNOWN;
    msg.debug("integer guard variable " + storePointerVar->getName().str() + " set to unknown", store);
  }
  intGuards[storePointerVar] = newState;
  return true;

}

// SEXP guard is a local variable of type SEXP
//   which is compared at least once against R_NilValue
//   which may be stored to and loaded from
//   which is not used for anything else (e.g. an address of it is not taken)
//
// there has to be either at least two comparisons using the guard, 
//   or there has to be one comparison and 
//     either at least one assignment of a constant
//     or at least one copy of that guard into another variable
//   [in other cases, we would gain nothing by tracking the guard]
//
// these heuristics are important because the keep the state space small(er)

bool isSEXPGuardVariable(AllocaInst* var, GlobalVariable* nilVariable, Function* isNullFunction) {
  if (!isSEXP(var)) {
    return false;
  }
  unsigned nComparisons = 0;
  unsigned nNilAssignments = 0;
  unsigned nCopies = 0;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (!l->hasOneUse()) {
        continue;
      }
      User *uu = l->user_back();
      if (CmpInst::classof(uu)) {
        CmpInst *ci = cast<CmpInst>(uu);
        if (!ci->isEquality()) {
          continue;
        }
        Value *other;
        if (ci->getOperand(0) == l) {
          other = ci->getOperand(1);
        } else {
          other = ci->getOperand(0);
        }
        if (LoadInst::classof(other)) {
          LoadInst *ol = cast<LoadInst>(other);
          if (ol->getPointerOperand() == nilVariable) {
            nComparisons++;
            continue;
          }
        }
        continue;
      }
      CallSite cs(cast<Value>(uu));
      if (cs && cs.getCalledFunction() == isNullFunction) {
        // isNull(guard);
        nComparisons++;
        continue;
      }      
      if (StoreInst::classof(uu)) {
        nCopies++;
        continue;
      }
      continue;
    }
    if (StoreInst::classof(u)) {
      Value *v = (cast<StoreInst>(u))->getValueOperand();
      if (LoadInst::classof(v)) {
        // guard = R_NilValue;
        LoadInst *l = cast<LoadInst>(v);
        if (l->getPointerOperand() == nilVariable) {
          nNilAssignments++;
        }
      }
      continue;
    }
    // this can e.g. be a call (taking address of the variable, which we do not support)
    return false;
  } 
  
  return nComparisons >= 2 || (nComparisons == 1 && nNilAssignments > 0) || (nComparisons == 1 && nCopies > 0);
}

bool isSEXPGuardVariable(AllocaInst* var, GlobalVariable* nilVariable, Function* isNullFunction, VarBoolCacheTy& cache) {
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }

  bool res = isSEXPGuardVariable(var, nilVariable, isNullFunction);
  
  cache.insert({var, res});
  return res;
}

std::string sgs_name(SEXPGuardState sgs) {
  switch(sgs) {
    case SGS_NIL: return "nil (R_NilValue)";
    case SGS_NONNIL: return "non-nil (not R_NilValue)";
    case SGS_UNKNOWN: return "unknown";
  }
}

SEXPGuardState getSEXPGuardState(SEXPGuardsTy& sexpGuards, AllocaInst* var) {
  auto gsearch = sexpGuards.find(var);
  if (gsearch == sexpGuards.end()) {
    return SGS_UNKNOWN;
  } else {
    return gsearch->second;
  }
}

bool handleStoreToSEXPGuard(StoreInst* store, VarBoolCacheTy& sexpGuardVarsCache, SEXPGuardsTy& sexpGuards,
  GlobalVariable* nilVariable, Function* isNullFunction, LineMessenger& msg, FunctionsSetTy& possibleAllocators, bool USE_ALLOCATOR_DETECTION) {

  Value* storePointerOp = store->getPointerOperand();
  Value* storeValueOp = store->getValueOperand();
  
  if (!AllocaInst::classof(storePointerOp)) {
    return false;
  }
  AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
  if (!isSEXPGuardVariable(storePointerVar, nilVariable, isNullFunction, sexpGuardVarsCache)) {
    return false;
  }
  // sexpguard = ...
              
  SEXPGuardState newState = SGS_UNKNOWN;
            
  if (LoadInst::classof(storeValueOp)) {
    Value *src = cast<LoadInst>(storeValueOp)->getPointerOperand();
    if (src == nilVariable) {
      newState = SGS_NIL;
      msg.debug("sexp guard variable " + storePointerVar->getName().str() + " set to nil", store);
    } else if (AllocaInst::classof(src) && 
        isSEXPGuardVariable(cast<AllocaInst>(src), nilVariable, isNullFunction, sexpGuardVarsCache)) {

      newState = getSEXPGuardState(sexpGuards, cast<AllocaInst>(src));
      msg.debug("sexp guard variable " + storePointerVar->getName().str() + " set to state of " +
        cast<AllocaInst>(src)->getName().str() + ", which is " + sgs_name(newState), store);
    } else {

      msg.debug("sexp guard variable " + storePointerVar->getName().str() + " set to unknown (unsupported loadinst source)", store);
    }
  } else {
    CallSite acs(storeValueOp);
    if (acs && USE_ALLOCATOR_DETECTION) {
      Function *afun = acs.getCalledFunction();
      if (possibleAllocators.find(afun) != possibleAllocators.end()) {
        newState = SGS_NONNIL;
        msg.debug("sexp guard variable " + storePointerVar->getName().str() + " set to non-nill (allocated by " + afun->getName().str() + ")", store);
      }
    }
    if (newState == SGS_UNKNOWN) {
      msg.debug("sexp guard variable " + storePointerVar->getName().str() + " set to unknown", store);
    }
  }
  sexpGuards[storePointerVar] = newState;
  return true;
}