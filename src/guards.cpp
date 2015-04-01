
#include "guards.h"
#include "patterns.h"

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
static bool isIntegerGuardVariable(AllocaInst* var) {

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

bool IntGuardsChecker::isGuard(AllocaInst* var) {
  auto csearch = varsCache.find(var);
  if (csearch != varsCache.end()) {
    return csearch->second;
  }

  bool res = isIntegerGuardVariable(var);
  
  varsCache.insert({var, res});
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

void IntGuardsChecker::handleForNonTerminator(Instruction *in, IntGuardsTy& intGuards) {

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
  if (!isGuard(storePointerVar)) { 
    return;
  }
  IntGuardState newState;
  if (ConstantInt::classof(storeValueOp)) {
    ConstantInt* constOp = cast<ConstantInt>(storeValueOp);
    if (constOp->isZero()) {
      newState = IGS_ZERO;
      if (msg->debug()) msg->debug("integer guard variable " + varName(storePointerVar) + " set to zero", store);
    } else {
      newState = IGS_NONZERO;
      if (msg->debug()) msg->debug("integer guard variable " + varName(storePointerVar) + " set to nonzero", store);
    }
  } else {
    // FIXME: could add support for intguarda = intguardb, if needed
    newState = IGS_UNKNOWN;
    if (msg->debug()) msg->debug("integer guard variable " + varName(storePointerVar) + " set to unknown", store);
  }
  intGuards[storePointerVar] = newState;
}

bool IntGuardsChecker::handleForTerminator(TerminatorInst* t, StateWithGuardsTy& s) {

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
  if (!isGuard(var)) {
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

  if (msg->debug()) {
    switch(succIndex) {
      case -1:
        msg->debug("undecided branch on integer guard variable " + varName(var), branch);
        break;
      case 0:
        msg->debug("taking true branch on integer guard variable " + varName(var), branch);
        break;
      case 1:
        msg->debug("taking false branch on integer guard variable " + varName(var), branch);
        break;
    }
  }
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
      state->intGuards[var] = ci->isTrueWhenEqual() ? IGS_ZERO : IGS_NONZERO;
      if (state->add()) {
        msg->trace("added true branch on integer guard of branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
      state->intGuards[var] = ci->isTrueWhenEqual() ? IGS_NONZERO : IGS_ZERO;
      if (state->add()) {
        msg->trace("added false branch on integer guard of branch at", branch);
      }
    }
  }
  return true;
}

PackedIntGuardsTy IntGuardsChecker::pack(const IntGuardsTy& intGuards) {
  PackedIntGuardsTy packed(intGuards.size());
  
  for(IntGuardsTy::const_iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; ++gi) {
    AllocaInst* var = gi->first;
    IntGuardState gs = gi->second;
    
    unsigned varIdx = varIndex.indexOf(var);
    unsigned base = varIdx * IGS_BITS;
    
    switch(gs) {
      case IGS_NONZERO: packed.bits[base] = true; break;     // 1 0
      case IGS_ZERO:    packed.bits[base + 1] = true; break; // 0 1
      case IGS_UNKNOWN: break;                               // implied 0 0
    }
    // 0 0 means UNKNOWN (or not included)
  }

  return packed;
}

IntGuardsTy IntGuardsChecker::unpack(const PackedIntGuardsTy& intGuards) {

  IntGuardsTy unpacked;
  unsigned nvars = intGuards.bits.size() / 2;
  
  for(unsigned varIdx = 0; varIdx < nvars; varIdx++) {
    unsigned base = varIdx * IGS_BITS;
    IntGuardState gs = IGS_UNKNOWN;
    
    if (intGuards.bits[base]) {
      gs = IGS_NONZERO;  
    } else if (intGuards.bits[base + 1]) {
      gs = IGS_ZERO;
    }
    
    if (gs != IGS_UNKNOWN) {
      unpacked.insert({varIndex.at(varIdx), gs});
    }
  }
  return unpacked;
}
  
void IntGuardsChecker::hash(size_t& res, const IntGuardsTy& intGuards) {

  hash_combine(res, intGuards.size());
  for(IntGuardsTy::const_iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; *gi++) {
    AllocaInst* var = gi->first;
    IntGuardState s = gi->second;
    hash_combine(res, (void *)var);
    hash_combine(res, (size_t) s);
  } // ordered map
}

size_t IntGuardsChecker::IntGuardsTy_hash::operator()(const IntGuardsTy& t) const { // FIXME: cannot call SEXPGuardsCheckerTy::hash
  size_t res = 0;
  hash_combine(res, t.size());
  for(IntGuardsTy::const_iterator gi = t.begin(), ge = t.end(); gi != ge; *gi++) {
    AllocaInst* var = gi->first;
    IntGuardState s = gi->second;
    hash_combine(res, (void *)var);
    hash_combine(res, (size_t) s);
  } // ordered map
  return res;
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
  unsigned nGEPs = 0;
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
      if (GetElementPtrInst::classof(uu)) {
        nGEPs++; // this almost always means a type check, such as isNull(x) or isSymbol(x)
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
  
  return nComparisons >= 2 || ((nComparisons == 1 || nGEPs > 0 || nEscapesToCalls > 0) && (nNilAssignments + nCopies + nStoresFromArgument + nStoresFromFunction > 0));
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

SEXPGuardState getSEXPGuardState(SEXPGuardsTy& sexpGuards, AllocaInst* var) {
  auto gsearch = sexpGuards.find(var);
  if (gsearch == sexpGuards.end()) {
    return SGS_UNKNOWN;
  } else {
    return gsearch->second.state;
  }
}

void handleSEXPGuardsForNonTerminator(Instruction* in, VarBoolCacheTy& sexpGuardVarsCache, SEXPGuardsTy& sexpGuards,
  GlobalsTy* g, const ArgInfosVectorTy *argInfos, SymbolsMapTy* symbolsMap, LineMessenger& msg, FunctionsSetTy* possibleAllocators) {

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
    const ArgInfoTy *ai = (*argInfos)[arg->getArgNo()];
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

bool handleTypeCheck(bool positive, unsigned testedType, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s, LineMessenger& msg, GlobalsTy* g) {

  // SGS_NONNIL and SGS_UNKNOWN are special states
  // SGS_NIL corresponds to a tested type and has a complement SGS_NONNIL
  // SGS_SYMBOL correspond to tested types (and this list can be extended), but does not have the complement (SGS_NONSYMBOL)

  if (testedType == RT_UNKNOWN) { // not a type check, or one that we do not support
    return false;
  }

  if (testedType == RT_NIL) {
    return handleNullCheck(positive, gs, guard, branch, s, msg);
  }
  
  SEXPGuardState testedState = SGS_UNKNOWN;
  if (testedType == RT_SYMBOL) {
    testedState = SGS_SYMBOL;
  }

  assert(testedState != SGS_NIL && testedState != SGS_NONNIL);
  // testedState == SGS_UNKNOWN means testing for a known, specific, but unsupported state (unsupported type)
  
  bool canBeTrue = true;
  bool canBeFalse = true;
  
  if (positive) {
    if (gs == testedState && gs != SGS_UNKNOWN) {
      canBeFalse = false; // isSymbol(symbol)
    } 
    if (gs != testedState && gs != SGS_UNKNOWN && gs != SGS_NONNIL) {  // gs == SGS_NONNIL can be any type...
      canBeTrue = false; // isSymbol(nonsymbol)
    }
  }
  
  if (!positive) {
    if (gs == testedState && gs != SGS_UNKNOWN) {
      canBeTrue = false; // !isSymbol(symbol)
    }
    if (gs != testedState && gs != SGS_UNKNOWN && gs != SGS_NONNIL) {
      canBeFalse = false; // !isSymbol(nonsymbol)
    }
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

bool handleSEXPGuardsForTerminator(TerminatorInst* t, VarBoolCacheTy& sexpGuardVarsCache, StateWithGuardsTy& s, GlobalsTy *g, const ArgInfosVectorTy* argInfos, 
  SymbolsMapTy* symbolsMap, LineMessenger& msg) {
  
  if (!BranchInst::classof(t)) {
    return false;
  }
  BranchInst* branch = cast<BranchInst>(t);
  if (!branch->isConditional() || !CmpInst::classof(branch->getCondition())) {
    return false;
  }
  
  // handle (inlined) type checks
  bool tcPositive;
  AllocaInst* tcVar;
  unsigned tcType;
  if (isTypeCheck(branch->getCondition(), tcPositive, tcVar, tcType) && isSEXPGuardVariable(tcVar, g, sexpGuardVarsCache)) {
    return handleTypeCheck(tcPositive, tcType, getSEXPGuardState(s.sexpGuards, tcVar), tcVar, branch, s, msg, g);
  }
  
  CmpInst* ci = cast<CmpInst>(branch->getCondition());
  if (!ci->isEquality()) {
    return false;
  }
  if (ConstantInt::classof(ci->getOperand(0)) || ConstantInt::classof(ci->getOperand(1))) {
    // handle non-inlined type check
   
    // comparison against a constant integer
    Value *op = NULL;
    if (ConstantInt::classof(ci->getOperand(0)) && cast<ConstantInt>(ci->getOperand(0))->isZero()) {
      op = ci->getOperand(1);
    } else if (ConstantInt::classof(ci->getOperand(1)) && cast<ConstantInt>(ci->getOperand(1))->isZero()) {
      op = ci->getOperand(0);
    }
    
    AllocaInst *guard = NULL;
    Function *f = NULL;
    SEXPType tcType = RT_UNKNOWN;
    if (op) {
      CallSite cs(op);
      if (cs) {
        f = cs.getCalledFunction();
        tcType = g->getTypeForTypeTest(cs.getCalledFunction());
        
        if (LoadInst::classof(cs.getArgument(0))) {
          Value *loadOp = cast<LoadInst>(cs.getArgument(0))->getPointerOperand();
          if (AllocaInst::classof(loadOp)) {
            guard = cast<AllocaInst>(loadOp);
          }
        }
      }
    }
    
    if (tcType == RT_UNKNOWN || !guard || !isSEXPGuardVariable(guard, g, sexpGuardVarsCache)) {
      return false;
    }
    SEXPGuardState gs = getSEXPGuardState(s.sexpGuards, guard);
    
    return handleTypeCheck(ci->isTrueWhenEqual(), tcType, gs, guard, branch, s, msg, g);
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
  
PackedSEXPGuardsTy SEXPGuardsCheckerTy::pack(const SEXPGuardsTy& sexpGuards) {

  return PackedSEXPGuardsTy(sgTable.intern(sexpGuards)); // FIXME: the envelope is not interned
}

SEXPGuardsTy SEXPGuardsCheckerTy::unpack(const PackedSEXPGuardsTy& sexpGuards) {
  return SEXPGuardsTy(*sexpGuards.sexpGuards);
}
  
void SEXPGuardsCheckerTy::hash(size_t& res, const SEXPGuardsTy& sexpGuards) {
  hash_combine(res, sexpGuards.size());
  for(SEXPGuardsTy::const_iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; *gi++) {
    AllocaInst* var = gi->first;
    const SEXPGuardTy& g = gi->second;
    hash_combine(res, (void *) var);
    hash_combine(res, (size_t) g.state);
    hash_combine(res, g.symbolName);
  } // ordered map
}

size_t SEXPGuardsCheckerTy::SEXPGuardsTy_hash::operator()(const SEXPGuardsTy& t) const { // FIXME: cannot call SEXPGuardsCheckerTy::hash
  size_t res = 0;
  hash_combine(res, t.size());
  for(SEXPGuardsTy::const_iterator gi = t.begin(), ge = t.end(); gi != ge; *gi++) {
    AllocaInst* var = gi->first;
    const SEXPGuardTy& g = gi->second;
    hash_combine(res, (void *) var);
    hash_combine(res, (size_t) g.state);
    hash_combine(res, g.symbolName);
  } // ordered map
  return res;
}

// common

void StateWithGuardsTy::dump(bool verbose) {
  
  errs() << "=== integer guards: " << &intGuards << "\n";
  for(IntGuardsTy::iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; *gi++) {
    AllocaInst *i = gi->first;
    IntGuardState s = gi->second;
    errs() << "   " << varName(i) << " ";
    if (verbose) {
      errs() << *i << " ";
    }
    errs() << " state: " << igs_name(s) << "\n";
  }

  errs() << "=== sexp guards: " << &sexpGuards << "\n";
  for(SEXPGuardsTy::iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; *gi++) {
    AllocaInst *i = gi->first;
    SEXPGuardTy &g = gi->second;
    
    errs() << "   " << varName(i) << " ";
    if (verbose) {
      errs() << *i << " ";
    }
    errs() << " state: " << sgs_name(g) << "\n";
  }  
}
