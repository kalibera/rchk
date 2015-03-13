
#include "guards.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>

#include <llvm/Support/raw_ostream.h>

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

void handleIntGuardsForNonTerminator(Instruction *in, VarBoolCacheTy& intGuardVarsCache, IntGuardsTy& intGuards, LineMessenger& msg) {

  if (!StoreInst::classof(in)) {
    return;
  }
  StoreInst* store = cast<StoreInst>(in);
  Value* storePointerOp = store->getPointerOperand();
  Value* storeValueOp = store->getValueOperand();
  
  // intguard = ...
  if (!AllocaInst::classof(storePointerOp)) {
    return;
  }
  AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
  if (!isIntegerGuardVariable(storePointerVar, intGuardVarsCache)) { 
    return;
  }
  IntGuardState newState;
  if (ConstantInt::classof(storeValueOp)) {
    ConstantInt* constOp = cast<ConstantInt>(storeValueOp);
    if (constOp->isZero()) {
      newState = IGS_ZERO;
      if (msg.debug()) msg.debug("integer guard variable " + varName(storePointerVar) + " set to zero", store);
    } else {
      newState = IGS_NONZERO;
      if (msg.debug()) msg.debug("integer guard variable " + varName(storePointerVar) + " set to nonzero", store);
    }
  } else {
    // FIXME: could add support for intguarda = intguardb, if needed
    newState = IGS_UNKNOWN;
    if (msg.debug()) msg.debug("integer guard variable " + varName(storePointerVar) + " set to unknown", store);
  }
  intGuards[storePointerVar] = newState;
}

bool handleIntGuardsForTerminator(TerminatorInst* t, VarBoolCacheTy& intGuardVarsCache, StateWithGuardsTy& s, LineMessenger& msg) {

  if (!BranchInst::classof(t)) {
    return false;
  }
  BranchInst* branch = cast<BranchInst>(t);
  if (!branch->isConditional() || !CmpInst::classof(branch->getCondition())) {
    return false;
  }
  CmpInst* ci = cast<CmpInst>(branch->getCondition());
  if (!ci->isEquality()) {
    return false;
  }
  // comparison with zero
  Value *constOp;
  Value *load;
  
  if (ConstantInt::classof(ci->getOperand(0)) && LoadInst::classof(ci->getOperand(1))) {
    constOp = ci->getOperand(0);
    load = ci->getOperand(1);
  } else {
    constOp = ci->getOperand(1);
    load = ci->getOperand(0);
  }
  
  if (!ConstantInt::classof(constOp) || !cast<ConstantInt>(constOp)->isZero()) {
    return false;
  }
  if (!LoadInst::classof(load)) {
    return false;
  }
  Value *loadOp = cast<LoadInst>(load)->getPointerOperand();
  if (!AllocaInst::classof(loadOp)) {
    return false;
  }
  AllocaInst *var = cast<AllocaInst>(loadOp);
  if (!isIntegerGuardVariable(var, intGuardVarsCache)) {
    return false;
  } 
  // if (intguard) ...
   
  IntGuardState g = getIntGuardState(s.intGuards, var);
  int succIndex = -1;
  if (g != IGS_UNKNOWN) {
    if (ci->isTrueWhenEqual()) {
      // guard == 0
      succIndex = (g == IGS_ZERO) ? 0 : 1;
    } else {
      // guard != 0
      succIndex = (g == IGS_ZERO) ? 1 : 0;
    }
  } 

  if (msg.debug()) {
    switch(succIndex) {
      case -1:
        msg.debug("undecided branch on integer guard variable " + varName(var), branch);
        break;
      case 0:
        msg.debug("taking true branch on integer guard variable " + varName(var), branch);
        break;
      case 1:
        msg.debug("taking false branch on integer guard variable " + varName(var), branch);
        break;
    }
  }
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
      state->intGuards[var] = ci->isTrueWhenEqual() ? IGS_ZERO : IGS_NONZERO;
      if (state->add()) {
        msg.trace("added true branch on integer guard of branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
      state->intGuards[var] = ci->isTrueWhenEqual() ? IGS_NONZERO : IGS_ZERO;
      if (state->add()) {
        msg.trace("added false branch on integer guard of branch at", branch);
      }
    }
  }
  return true;
}

// SEXP guard is a local variable of type SEXP
//   that follows the heuristics included below
//   these heuristics are important because the keep the state space small(er)

bool isSEXPGuardVariable(AllocaInst* var, GlobalsTy* g) {
  if (!isSEXP(var)) {
    return false;
  }
  unsigned nComparisons = 0;
  unsigned nNilAssignments = 0;
  unsigned nCopies = 0;
  unsigned nStoresFromArgument = 0;
  unsigned nStoresFromFunction = 0;
  unsigned nEscapesToCalls = 0;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (!l->hasOneUse()) { // FIXME: too restrictive?
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
          if (GlobalVariable::classof(ol->getPointerOperand())) { // comparison against global variable, including R_NilValue
            nComparisons++;
            continue;
          }
        }
        continue;
      }
      CallSite cs(cast<Value>(uu));
      if (cs) {
        if (isTypeTest(cs.getCalledFunction(), g)) {
          // isNull(guard);
          nComparisons++;
        } else if (cs.getCalledFunction()) {
          nEscapesToCalls++;
        }
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
        if (l->getPointerOperand() == g->nilVariable) {
          nNilAssignments++;
        }
      }
      if (Argument::classof(v)) { // FIXME: we could take into account what we know about arguments
        // guard = function_argument; (implicit store)
        nStoresFromArgument++;
      }
      CallSite cs(v);
      if (cs) {
        nStoresFromFunction++;
      }
      continue;
    }
    // this can e.g. be a call (taking address of the variable, which we do not support)
    return false;
  } 
  
  return nComparisons >= 2 || ((nComparisons == 1 || nEscapesToCalls > 0) && (nNilAssignments + nCopies + nStoresFromArgument + nStoresFromFunction > 0));
}

bool isSEXPGuardVariable(AllocaInst* var, GlobalsTy* g, VarBoolCacheTy& cache) {
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }

  bool res = isSEXPGuardVariable(var, g);
  
  cache.insert({var, res});
  return res;
}

std::string sgs_name(SEXPGuardTy& g) {

  SEXPGuardState sgs = g.state;
  switch(sgs) {
    case SGS_NIL: return "nil (R_NilValue)";
    case SGS_NONNIL: return "non-nil (not R_NilValue)";
    case SGS_UNKNOWN: return "unknown";
    case SGS_SYMBOL: return "symbol \"" + g.symbolName + "\"";
  }
}

SEXPGuardState getSEXPGuardState(SEXPGuardsTy& sexpGuards, AllocaInst* var, std::string& symbolName) {
  auto gsearch = sexpGuards.find(var);
  if (gsearch == sexpGuards.end()) {
    return SGS_UNKNOWN;
  } else {
    symbolName = gsearch->second.symbolName;
    return gsearch->second.state;
  }
}

void handleSEXPGuardsForNonTerminator(Instruction* in, VarBoolCacheTy& sexpGuardVarsCache, SEXPGuardsTy& sexpGuards,
  GlobalsTy* g, ArgInfosTy *argInfos, SymbolsMapTy* symbolsMap, LineMessenger& msg, FunctionsSetTy* possibleAllocators) {

  if (!StoreInst::classof(in)) {
    return;
  }
  StoreInst* store = cast<StoreInst>(in);
  Value* storePointerOp = store->getPointerOperand();
  Value* storeValueOp = store->getValueOperand();
  
  if (!AllocaInst::classof(storePointerOp)) {
    return;
  }
  AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
  if (!isSEXPGuardVariable(storePointerVar, g, sexpGuardVarsCache)) {
    return;
  }
  // sexpguard = ...

  if (argInfos && Argument::classof(storeValueOp))  { // sexpguard = symbol_argument
    Argument *arg = cast<Argument>(storeValueOp);
    ArgInfoTy *ai = (*argInfos)[arg->getArgNo()];
    if (ai && ai->isSymbol()) {
      SEXPGuardTy newGS(SGS_SYMBOL, cast<SymbolArgInfoTy>(ai)->symbolName);
      sexpGuards[storePointerVar] = newGS;
      if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to symbol \"" +
        cast<SymbolArgInfoTy>(ai)->symbolName + "\" from argument\n", store);
      return;
    }
  }
              
  if (LoadInst::classof(storeValueOp)) {
    Value *src = cast<LoadInst>(storeValueOp)->getPointerOperand();
    if (src == g->nilVariable) {  // sexpguard = R_NilValue
      if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to nil", store);
      SEXPGuardTy newGS(SGS_NIL);
      sexpGuards[storePointerVar] = newGS;
      return;
    }
    if (AllocaInst::classof(src) && 
        isSEXPGuardVariable(cast<AllocaInst>(src), g, sexpGuardVarsCache)) { // sexpguard1 = sexpguard2

      auto gsearch = sexpGuards.find(cast<AllocaInst>(src));
      if (gsearch == sexpGuards.end()) {
        sexpGuards.erase(storePointerVar);
        if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to unknown state because " +
          varName(cast<AllocaInst>(src)) + " is also unknown.", store);
      } else {
        sexpGuards[storePointerVar] = gsearch->second;
        if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to state of " +
          varName(cast<AllocaInst>(src)) + ", which is " + sgs_name(gsearch->second), store);
      }
      return;
    }
    if (symbolsMap && GlobalVariable::classof(src)) {
      auto sfind = symbolsMap->find(cast<GlobalVariable>(src));
      if (sfind != symbolsMap->end()) {
        SEXPGuardTy newGS(SGS_SYMBOL, sfind->second);
        sexpGuards[storePointerVar] = newGS;
        if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to symbol \"" + sfind->second + "\" at assignment\n", store);
        return;
      } 
    }
  } else {
    CallSite acs(storeValueOp);
    if (acs && possibleAllocators) { // sexpguard = fooAlloc()
      Function *afun = acs.getCalledFunction();
      if (possibleAllocators->find(afun) != possibleAllocators->end()) {
        SEXPGuardTy newGS(SGS_NONNIL);
        sexpGuards[storePointerVar] = newGS;
        if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to non-nill (allocated by " + funName(afun) + ")", store);
        return;
      }
    }
  }
  sexpGuards.erase(storePointerVar);
  if (msg.debug()) msg.debug("sexp guard variable " + varName(storePointerVar) + " set to unknown", store);
}

bool handleNullCheck(bool positive, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s, LineMessenger& msg) {

  int succIndex = -1;
    
  // if (x == R_NilValue) ... positive == true
  // if (x != R_NilValue) ... positive == false

  if (gs != SGS_UNKNOWN) {
    // note a symbol cannot be R_NilValue

    if (positive) {       
      // guard == R_NilValue
      succIndex = (gs == SGS_NIL) ? 0 : 1;
    } else {
      // guard != R_NilValue
      succIndex = (gs == SGS_NIL) ? 1 : 0;
    }
  }

  if (msg.debug()) {
    switch(succIndex) {
      case -1:
        msg.debug("undecided branch on sexp guard variable " + varName(guard), branch);
        break;
      case 0:
        msg.debug("taking true branch on sexp guard variable " + varName(guard), branch);
        break;
      case 1:
        msg.debug("taking false branch on sexp guard variable " + varName(guard), branch);
        break;
    }
  }
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
      if (gs != SGS_SYMBOL) {
        SEXPGuardTy newGS(positive ? SGS_NIL : SGS_NONNIL);
        state->sexpGuards[guard] = newGS;
      }
      if (state->add()) {
        msg.trace("added true branch on sexp guard of branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
      if (gs != SGS_SYMBOL) {
        SEXPGuardTy newGS(positive ? SGS_NONNIL : SGS_NIL);
        state->sexpGuards[guard] = newGS;
      }
      if (state->add()) {
        msg.trace("added false branch on sexp guard of branch at", branch);
      }
    }
  }
  return true;
}


bool handleTypeCheck(bool positive, Function *testf, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s, LineMessenger& msg, GlobalsTy* g) {

  SEXPGuardState testedState = SGS_UNKNOWN;
  if (testf == g->isSymbolFunction) {
    testedState = SGS_SYMBOL;
  }
  
  bool canBeTrue = true;
  bool canBeFalse = true;

  // SGS_NONNIL and SGS_UNKNOWN are special states
  // SGS_NIL corresponds to a tested type and has a complement SGS_NONNIL
  // SGS_SYMBOL correspond to tested types (and this list can be extended), but does not have the complement (SGS_NONSYMBOL)
  
  // handling type checks: isSymbol
  assert(testedState == SGS_SYMBOL); // add more types here
  
  if (positive && gs == testedState) {
    canBeFalse = false; // isSymbol(symbol)
  }
  if (positive && gs != SGS_NONNIL && gs != SGS_UNKNOWN) {
    canBeTrue = false; // isSymbol(nonsymbol)
  }
  if (!positive && gs == testedState) {
    canBeTrue = false; // !isSymbol(symbol)
  }
  if (!positive && gs != SGS_NONNIL && gs != SGS_UNKNOWN) {
    canBeFalse = false; // !isSymbol(nonsymbol)
  }

  assert(canBeTrue || canBeFalse);
  
  int succIndex = -1;
  if (!canBeFalse) succIndex = 0;
  if (!canBeTrue) succIndex = 1;
  
  if (msg.debug()) {
    switch(succIndex) {
      case -1:
        msg.debug("undecided type branch on sexp guard variable " + varName(guard), branch);
        break;
      case 0:
        msg.debug("taking true type branch on sexp guard variable " + varName(guard), branch);
        break;
      case 1:
        msg.debug("taking false type branch on sexp guard variable " + varName(guard), branch);
        break;
    }
  }
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0)); // FIXME: capture that something is a symbol even if we don't know which one
      if (state->add()) {
        msg.trace("added true type branch on sexp guard of branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1)); // FIXME: capture that something is a symbol even if we don't know which one
      if (state->add()) {
        msg.trace("added false type branch on sexp guard of branch at", branch);
      }
    }
  }
  return true;
}

bool handleSEXPGuardsForTerminator(TerminatorInst* t, VarBoolCacheTy& sexpGuardVarsCache, StateWithGuardsTy& s, GlobalsTy *g, ArgInfosTy* argInfos, 
  SymbolsMapTy* symbolsMap, LineMessenger& msg) {
  
  if (!BranchInst::classof(t)) {
    return false;
  }
  BranchInst* branch = cast<BranchInst>(t);
  if (!branch->isConditional() || !CmpInst::classof(branch->getCondition())) {
    return false;
  }
  CmpInst* ci = cast<CmpInst>(branch->getCondition());
  if (!ci->isEquality()) {
    return false;
  }
  if (ConstantInt::classof(ci->getOperand(0)) || ConstantInt::classof(ci->getOperand(1))) {
    // comparison against a constant integer
    
    Value *op = NULL;
    if (ConstantInt::classof(ci->getOperand(0)) && cast<ConstantInt>(ci->getOperand(0))->isZero()) {
      op = ci->getOperand(1);
    } else if (ConstantInt::classof(ci->getOperand(1)) && cast<ConstantInt>(ci->getOperand(1))->isZero()) {
      op = ci->getOperand(0);
    }
    
    AllocaInst *guard = NULL;
    Function *f = NULL;
    if (op) {
      CallSite cs(op);
      if (cs && isTypeTest(cs.getCalledFunction(), g)) {
        f = cs.getCalledFunction();
        
        if (LoadInst::classof(cs.getArgument(0))) {
          Value *loadOp = cast<LoadInst>(cs.getArgument(0))->getPointerOperand();
          if (AllocaInst::classof(loadOp)) {
            guard = cast<AllocaInst>(loadOp);
          }
        }
      }
    }
    
    if (!guard || !isSEXPGuardVariable(guard, g, sexpGuardVarsCache)) {
      return false;
    }
  
    std::string guardSymbolName;
    SEXPGuardState gs = getSEXPGuardState(s.sexpGuards, guard, guardSymbolName);
    
    if (f == g->isNullFunction) {
      return handleNullCheck(ci->isTrueWhenEqual(), gs, guard, branch, s, msg);  // if (isNull(x)), if (!isNull(x))
    }
    
    return handleTypeCheck(ci->isTrueWhenEqual(), f, gs, guard, branch, s, msg, g);
  }
  
  
  if (!LoadInst::classof(ci->getOperand(0)) || !LoadInst::classof(ci->getOperand(1))) {
    return false;
  }
  Value *lo = cast<LoadInst>(ci->getOperand(0))->getPointerOperand();
  Value *ro = cast<LoadInst>(ci->getOperand(1))->getPointerOperand();

  AllocaInst *guard = NULL;
  GlobalVariable *gv = NULL;
  
  if (AllocaInst::classof(lo) && GlobalVariable::classof(ro)) {
    guard = cast<AllocaInst>(lo);
    gv = cast<GlobalVariable>(ro);
  } else if (GlobalVariable::classof(lo) && AllocaInst::classof(ro)) {
    guard = cast<AllocaInst>(ro);
    gv = cast<GlobalVariable>(lo);
  }

  if (!guard || !gv || !isSEXPGuardVariable(guard, g, sexpGuardVarsCache)) {
    return false;
  }
  
  std::string guardSymbolName;
  SEXPGuardState gs = getSEXPGuardState(s.sexpGuards, guard, guardSymbolName);
  int succIndex = -1;

  if (gv == g->nilVariable) {

    // handle comparisons with R_NilValue
                  
    // if (x == R_NilValue) ...
    // if (x != R_NilValue) ...
    
    return handleNullCheck(ci->isTrueWhenEqual(), gs, guard, branch, s, msg);
  }
  // handle comparisons with symbols
  
  if (!symbolsMap) {
    return false;
  }
  
  auto sfind = symbolsMap->find(gv);
  if (sfind == symbolsMap->end()) {
    return false;
  }
      
  std::string& constSymbolName = sfind->second;

  // if (x == R_XSymbol) ...
  // if (x != R_XSymbol) ...

  if (gs == SGS_SYMBOL) {
    if (ci->isTrueWhenEqual()) {
      // guard == R_XSymbol
      succIndex = (guardSymbolName == constSymbolName) ? 0 : 1;
    } else {
      // guard != R_XSymbol
      succIndex = (guardSymbolName == constSymbolName) ? 1 : 0;
    }
  }
  if (gs == SGS_NIL) {
    if (ci->isTrueWhenEqual()) {
      // guard == R_XSymbol
      succIndex = 1;
    } else {
      // guard != R_XSymbol
      succIndex = 0;
    }
  }

  if (msg.debug()) {
    switch(succIndex) {
      case -1:
        msg.debug("undecided symbol branch on sexp guard variable " + varName(guard), branch);
        break;
      case 0:
        msg.debug("taking true symbol branch on sexp guard variable " + varName(guard), branch);
        break;
      case 1:
        msg.debug("taking false symbol branch on sexp guard variable " + varName(guard), branch);
        break;
    }
  }
  
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
      if (gs != SGS_SYMBOL && ci->isTrueWhenEqual()) {
        SEXPGuardTy newGS(SGS_SYMBOL, constSymbolName);
        state->sexpGuards[guard] = newGS;
      }
      if (state->add()) {
        msg.trace("added true branch on sexp guard of symbol branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
      if (gs != SGS_SYMBOL && ci->isFalseWhenEqual()) {
        SEXPGuardTy newGS(SGS_SYMBOL, constSymbolName);
        state->sexpGuards[guard] = newGS;
      }
      if (state->add()) {
        msg.trace("added false branch on sexp guard of branch at", branch);
      }
    }
  }
  return true;  
}

// common

void StateWithGuardsTy::dump(bool verbose) {
  
  errs() << "=== integer guards: " << &intGuards << "\n";
  for(IntGuardsTy::iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; *gi++) {
    AllocaInst *i = gi->first;
    IntGuardState s = gi->second;
    errs() << "   " << demangle(i->getName()) << " ";
    if (verbose) {
      errs() << *i << " ";
    }
    errs() << " state: " << igs_name(s) << "\n";
  }

  errs() << "=== sexp guards: " << &sexpGuards << "\n";
  for(SEXPGuardsTy::iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; *gi++) {
    AllocaInst *i = gi->first;
    SEXPGuardTy &g = gi->second;
    
    errs() << "   " << demangle(i->getName()) << " ";
    if (verbose) {
      errs() << *i << " ";
    }
    errs() << " state: " << sgs_name(g) << "\n";
  }  
}
