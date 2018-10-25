
#include "guards.h"
#include "patterns.h"
#include "vectors.h"

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
  unsigned nVariableAssignments = 0;
  
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
      if (LoadInst* li = dyn_cast<LoadInst>(v)) {
        if (AllocaInst::classof(li->getPointerOperand())) {
          // guard = variable
          nVariableAssignments++; // FIXME: we don't really know if this was a guard variable
        }
      }
      continue;
    }
    // this can e.g. be a call (taking address of the variable, which we do not support)
    return false;
  } 
  return nComparisons >= 2 || (nComparisons == 1 && (nConstantAssignments > 0 || nVariableAssignments > 0));
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
  myassert(false);
}

IntGuardState IntGuardsChecker::getGuardState(const IntGuardsTy& intGuards, AllocaInst* var) {
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
    newState = IGS_UNKNOWN;
    
    if (LoadInst* li = dyn_cast<LoadInst>(storeValueOp)) {
      AllocaInst* srcVar = dyn_cast<AllocaInst>(li->getPointerOperand());
      if (srcVar && isGuard(srcVar)) {
        newState = getGuardState(intGuards, srcVar);
        if (msg->debug()) msg->debug("integer guard variable " + varName(storePointerVar) + " set to the value of guard " + varName(srcVar), store);
      }
    }
    if (newState == IGS_UNKNOWN) {
      if (msg->debug()) msg->debug("integer guard variable " + varName(storePointerVar) + " (set to) unknown", store);
    } 
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
   
  IntGuardState g = getGuardState(s.intGuards, var);
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
        msg->debug("taking (only) true branch on integer guard variable " + varName(var), branch);
        break;
      case 1:
        msg->debug("taking (only) false branch on integer guard variable " + varName(var), branch);
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

  // note we first have to call indexOf on each variable to make sure
  // it is indexed [later this should be perhaps done alreading when manipulating guards]
  
  for(IntGuardsTy::const_iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; ++gi) {
    AllocaInst* var = gi->first;
    varIndex.indexOf(var);
  }  

  PackedIntGuardsTy packed(varIndex.size());
  
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
  unsigned nvars = intGuards.bits.size() / IGS_BITS;
  
  myassert(nvars * IGS_BITS == intGuards.bits.size());
  
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
  for(IntGuardsTy::const_iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; ++gi) {
    AllocaInst* var = gi->first;
    IntGuardState s = gi->second;
    hash_combine(res, (void *)var);
    hash_combine(res, (size_t) s);
  } // ordered map
}

// SEXP guard is a local variable of type SEXP
//   that follows the heuristics included below
//   these heuristics are important because they keep the state space small(er)
//   but also they are fragile - if something important is not a guard, the results will be less
//     precise, may have more false alarms

bool SEXPGuardsChecker::uncachedIsGuard(AllocaInst* var) {
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
  unsigned nVectorTests = 0;
  
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
        if (isVectorGuard(cs.getCalledFunction())) {
          nVectorTests++;
          // FIXME: could perhaps increment only if one of the branches is an error block?
        }
        
        if (isTypeTest(cs.getCalledFunction(), g) || isVectorGuard(cs.getCalledFunction())) { // isNull(guard);
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
  
  return nVectorTests >= 1 || nComparisons >= 2 || ((nComparisons == 1 || nGEPs > 0 || nEscapesToCalls > 0) && (nNilAssignments + nCopies + nStoresFromArgument + nStoresFromFunction > 0));
}

bool SEXPGuardsChecker::isGuard(AllocaInst* var) {
  auto csearch = varsCache.find(var);
  if (csearch != varsCache.end()) {
    return csearch->second;
  }

  bool res = uncachedIsGuard(var);
  
  varsCache.insert({var, res});
  return res;
}

std::string sgs_name(SEXPGuardTy& g) {

  SEXPGuardState sgs = g.state;
  switch(sgs) {
    case SGS_NIL: return "nil (R_NilValue)";
    case SGS_NONNIL: return "non-nil (not R_NilValue)";
    case SGS_UNKNOWN: return "unknown";
    case SGS_SYMBOL: return "symbol \"" + g.symbolName + "\"";
    case SGS_VECTOR: return "vector";
  }
  myassert(false);
}

SEXPGuardState SEXPGuardsChecker::getGuardState(const SEXPGuardsTy& sexpGuards, AllocaInst* var, std::string& symbolName) {
  auto gsearch = sexpGuards.find(var);
  if (gsearch == sexpGuards.end()) {
    return SGS_UNKNOWN;
  } else {
    SEXPGuardState gs = gsearch->second.state;
    if (gs == SGS_SYMBOL) {
      symbolName = gsearch->second.symbolName;
    }
    return gs;
  }
}

SEXPGuardState SEXPGuardsChecker::getGuardState(const SEXPGuardsTy& sexpGuards, AllocaInst* var) {
  auto gsearch = sexpGuards.find(var);
  if (gsearch == sexpGuards.end()) {
    return SGS_UNKNOWN;
  } else {
    return gsearch->second.state;
  }
}

void SEXPGuardsChecker::handleForNonTerminator(Instruction* in, SEXPGuardsTy& sexpGuards) {

  // TODO: handle more "vector-only" operations, including passing to vector-only arguments of functions
  AllocaInst* vvar;
  if (isVectorOnlyVarOperation(in, vvar)) {
    // vvar is a vector
    
    SEXPGuardState gs = getGuardState(sexpGuards, vvar);
    if (gs != SGS_VECTOR) {
      SEXPGuardTy newGS(SGS_VECTOR);
      sexpGuards[vvar] = newGS;
      if (msg->debug()) msg->debug("sexp guard variable " + varName(vvar) + " set to vector because used with vector-only operation", in);
    }
    return;
  }
  
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
  if (!isGuard(storePointerVar)) {
    return;
  }
  // sexpguard = ...

  // sexpguard = PROTECT( ... )
  // FIXME: they may be more filtering functions like PROTECT
  CallSite cs(storeValueOp);
  if (cs) {
    Function *pfun = cs.getCalledFunction();
    if (pfun && (pfun->getName() == "Rf_protect" || pfun->getName() == "Rf_protectWithIndex")) {
      storeValueOp = cs.getArgument(0); // fall through
      if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " receiving its value from call to PROTECT/PROTECT_WITH_INDEX", store);
    }
  }

  if (argInfos && Argument::classof(storeValueOp))  { // sexpguard = function_argument
    Argument *arg = cast<Argument>(storeValueOp);
    const ArgInfoTy *ai = (*argInfos)[arg->getArgNo()];
    if (ai && ai->isSymbol()) { // sexpguard = symbol_argument
      SEXPGuardTy newGS(SGS_SYMBOL, static_cast<const SymbolArgInfoTy*>(ai)->symbolName);
      sexpGuards[storePointerVar] = newGS;
      if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to symbol \"" +
        static_cast<const SymbolArgInfoTy*>(ai)->symbolName + "\" from argument", store);
      return;
    }
    if (ai && ai->isVector()) { // sexpguard = vector_argument
      SEXPGuardTy newGS(SGS_VECTOR);
      sexpGuards[storePointerVar] = newGS;
      if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to vector from argument", store);
      return;
    }
  }

  if (LoadInst::classof(storeValueOp)) {
    Value *src = cast<LoadInst>(storeValueOp)->getPointerOperand();
    if (src == g->nilVariable) {  // sexpguard = R_NilValue
      if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to nil", store);
      SEXPGuardTy newGS(SGS_NIL);
      sexpGuards[storePointerVar] = newGS;
      return;
    }
    if (AllocaInst::classof(src) && 
        isGuard(cast<AllocaInst>(src))) { // sexpguard1 = sexpguard2

      auto gsearch = sexpGuards.find(cast<AllocaInst>(src));
      if (gsearch == sexpGuards.end()) {
        sexpGuards.erase(storePointerVar);
        if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to unknown state because " +
          varName(cast<AllocaInst>(src)) + " is also unknown.", store);
      } else {
        sexpGuards[storePointerVar] = gsearch->second;
        if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to state of " +
          varName(cast<AllocaInst>(src)) + ", which is " + sgs_name(gsearch->second), store);
      }
      return;
    }
    if (symbolsMap && GlobalVariable::classof(src)) {
      auto sfind = symbolsMap->find(cast<GlobalVariable>(src));
      if (sfind != symbolsMap->end()) {
        SEXPGuardTy newGS(SGS_SYMBOL, sfind->second);
        sexpGuards[storePointerVar] = newGS;
        if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to symbol \"" + sfind->second + "\" at assignment", store);
        return;
      } 
    }
  } else {
    CallSite acs(storeValueOp);
    const CalledFunctionTy *atgt = msg->debug() ? cm->getCalledFunction(storeValueOp, this, &sexpGuards, true) : NULL; // just for debugging - seeing the context

    if (acs && isVectorProducingCall(storeValueOp, cm, this, &sexpGuards)) {
      SEXPGuardTy newGS(SGS_VECTOR);
      sexpGuards[storePointerVar] = newGS;
      if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to vector (created by " + funName(atgt) +  ")", store);
      return;
    }

    if (acs) {
      std::string symbolName;
      if (isInstallConstantCall(storeValueOp, symbolName)) {
        SEXPGuardTy newGS(SGS_SYMBOL, symbolName);
        sexpGuards[storePointerVar] = newGS;
        if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to symbol \"" + symbolName + "\" at install call " + funName(atgt), store);
        return;        
      }
    }
    
    if (acs && possibleAllocators) { // sexpguard = fooAlloc()
      Function *afun = acs.getCalledFunction();
      if (possibleAllocators->find(afun) != possibleAllocators->end()) {
        SEXPGuardTy newGS(SGS_NONNIL);
        sexpGuards[storePointerVar] = newGS;
        if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to non-nill (allocated by " + funName(atgt) + ")", store);
        return;
      }
    }
  }
  sexpGuards.erase(storePointerVar);
  if (msg->debug()) msg->debug("sexp guard variable " + varName(storePointerVar) + " set to unknown", store);
}

bool SEXPGuardsChecker::handleNullCheck(bool positive, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s) {

  int succIndex = -1;
    
  // if (x == R_NilValue) ... positive == true
  // if (x != R_NilValue) ... positive == false

  if (gs != SGS_UNKNOWN) {
    // note a symbol cannot be R_NilValue
    // note a vector cannot be R_NilValue
    //  so SGS_NONNIL and SGS_SYMBOL and SGS_VECTOR all mean "non-nil"
    
    if (positive) {       
      // guard == R_NilValue
      succIndex = (gs == SGS_NIL) ? 0 : 1;
    } else {
      // guard != R_NilValue
      succIndex = (gs == SGS_NIL) ? 1 : 0;
    }
  }

  if (msg->debug()) {
    switch(succIndex) {
      case -1:
        msg->debug("undecided branch on sexp guard variable " + varName(guard), branch);
        break;
      case 0:
        msg->debug("taking (only) true branch on sexp guard variable " + varName(guard), branch);
        break;
      case 1:
        msg->debug("taking (only) false branch on sexp guard variable " + varName(guard), branch);
        break;
    }
  }
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
      if (gs != SGS_SYMBOL && gs != SGS_VECTOR) {
        SEXPGuardTy newGS(positive ? SGS_NIL : SGS_NONNIL);
        state->sexpGuards[guard] = newGS; // added information from that the true branch was taken
      }
      if (state->add()) {
        msg->trace("added true branch on sexp guard of branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
      if (gs != SGS_SYMBOL && gs != SGS_VECTOR) {
        SEXPGuardTy newGS(positive ? SGS_NONNIL : SGS_NIL);
        state->sexpGuards[guard] = newGS; // added information from that the false branch was taken
      }
      if (state->add()) {
        msg->trace("added false branch on sexp guard of branch at", branch);
      }
    }
  }
  return true;
}

bool SEXPGuardsChecker::handleTypeCheck(bool positive, int testedType, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s) {

  // SGS_NONNIL and SGS_UNKNOWN are special states
  // SGS_NIL corresponds to a tested type and has a complement SGS_NONNIL
  // SGS_SYMBOL correspond to tested types (and this list can be extended), but does not have the complement (SGS_NONSYMBOL)

  if (testedType == RT_UNKNOWN) { // not a type check, or one that we do not support
    return false;
  }

  if (testedType == RT_NIL) {
    return handleNullCheck(positive, gs, guard, branch, s);
  }
  
  SEXPGuardState testedState = SGS_UNKNOWN;
  if (testedType == RT_SYMBOL) {
    testedState = SGS_SYMBOL;
  }

  myassert(testedState != SGS_NIL && testedState != SGS_NONNIL);
  // testedState == SGS_UNKNOWN means testing for a known, specific, but unsupported state (unsupported type)
  
  bool canBeTrue = true;
  bool canBeFalse = true;
  
  if (positive) {
    if (gs == testedState && gs != SGS_UNKNOWN) {
      canBeFalse = false; // isSymbol(symbol)
    } 
    if (gs != testedState && gs != SGS_UNKNOWN && gs != SGS_NONNIL && gs != SGS_VECTOR) {  // gs == SGS_NONNIL can be any type...
                                                                                           // gs == SGS_VECTOR cannot be a symbol
      canBeTrue = false; // isSymbol(nonsymbol), isReal(symbol)
    }
    if (gs == SGS_VECTOR && !isVectorType(testedType)) {
      canBeTrue = false; // isList(vector)
    }
  }
  
  if (!positive) {
    if (gs == testedState && gs != SGS_UNKNOWN) {
      canBeTrue = false; // !isSymbol(symbol)
    }
    if (gs != testedState && gs != SGS_UNKNOWN && gs != SGS_NONNIL && gs != SGS_VECTOR) {
      canBeFalse = false; // !isSymbol(nonsymbol)
    }
    if (gs == SGS_VECTOR && !isVectorType(testedType)) {
      canBeFalse = false; // isList(vector)
    }    
  }
  
  myassert(canBeTrue || canBeFalse);
  
  int succIndex = -1;
  if (!canBeFalse) succIndex = 0;
  if (!canBeTrue) succIndex = 1;
  
  if (msg->debug()) {
    switch(succIndex) {
      case -1:
        msg->debug("undecided type branch on sexp guard variable " + varName(guard), branch);
        break;
      case 0:
        msg->debug("taking (only) true type branch on sexp guard variable " + varName(guard), branch);
        break;
      case 1:
        msg->debug("taking (only) false type branch on sexp guard variable " + varName(guard), branch);
        break;
    }
  }
  if (succIndex != 1) {
    // true branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(0)); // FIXME: capture that something is a symbol even if we don't know which one
      if (gs != SGS_SYMBOL && gs != SGS_VECTOR && isVectorType(testedType) && positive) {
        SEXPGuardTy newGS(SGS_VECTOR);
        state->sexpGuards[guard] = newGS; // added information from that the true branch was taken (e.g. if it is a String, it is definitely a vector)
      }      
      if (state->add()) {
        msg->trace("added true type branch on sexp guard of branch at", branch);
      }
    }
  }
  if (succIndex != 0) {
    // false branch is possible
    {
      StateWithGuardsTy* state = s.clone(branch->getSuccessor(1)); // FIXME: capture that something is a symbol even if we don't know which one
      if (state->add()) {
        msg->trace("added false type branch on sexp guard of branch at", branch);
      }
    }
  }
  return true;  
  
}

bool SEXPGuardsChecker::handleTypeSwitch(TerminatorInst* t, StateWithGuardsTy& s) {
  AllocaInst *var;
  BasicBlock *defaultSucc;
  TypeSwitchInfoTy info;
  
  if (!isTypeSwitch(t, var, defaultSucc, info) || !isGuard(var)) {
    return false;
  }

  // add default case
  { 
    // FIXME: sometimes could infer something about types
    StateWithGuardsTy* state = s.clone(defaultSucc);
    if (state->add()) {
      msg->trace("added default case for type switch", t);
    }
  }  
  
  // add type cases
  for(TypeSwitchInfoTy::iterator ii = info.begin(), ie = info.end(); ii != ie; ++ii) {
    BasicBlock *succ = ii->first;
    unsigned type = ii->second;
    
    SEXPGuardState gs = getGuardState(s.sexpGuards, var);

    // rule out impossible successors
        
    if (gs == SGS_SYMBOL && type != RT_SYMBOL) {
      continue;
    } else if (gs == SGS_VECTOR && !isVectorType(type)) {
      continue;
    } else if (gs == SGS_NONNIL && type == RT_NIL) {
      continue;
    } else if (gs == SGS_NIL && type != RT_NIL) {
      continue;
    }

    // compute new guard state
    
    SEXPGuardState newgs = gs;
    if (gs == SGS_SYMBOL) {
      // keep it
    } else if (isVectorType(type)) {
      newgs = SGS_VECTOR;
    } else if (type == RT_NIL) {
      newgs = SGS_NIL;
    }
    
    { 
      StateWithGuardsTy* state = s.clone(succ);
      if (newgs != gs) {
        SEXPGuardTy ng(newgs);
        state->sexpGuards[var] = ng;
      }

      if (state->add()) {
        msg->trace("added case " + std::to_string(type) + " for switch", t);
      }
    }      
  }
  
  return true;  
}

bool SEXPGuardsChecker::handleForTerminator(TerminatorInst* t, StateWithGuardsTy& s) {

  // handle (inlined) type switch
  if (handleTypeSwitch(t, s)) {
    return true;
  }
  
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
  if (isTypeCheck(branch->getCondition(), tcPositive, tcVar, tcType) && isGuard(tcVar)) {
    return handleTypeCheck(tcPositive, tcType, getGuardState(s.sexpGuards, tcVar), tcVar, branch, s);
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
        
        if (LoadInst::classof(cs.getArgument(0))) {
          Value *loadOp = cast<LoadInst>(cs.getArgument(0))->getPointerOperand();
          if (AllocaInst::classof(loadOp)) {
            guard = cast<AllocaInst>(loadOp);
          }
        }
        if (guard) {
          tcType = g->getTypeForTypeTest(f);
        }
      }
    }
    
    if (!guard || !isGuard(guard) || !f) {
      return false;
    }
    SEXPGuardState gs = getGuardState(s.sexpGuards, guard);
    
    if (tcType != RT_UNKNOWN) { // a simple type check (like isSymbol)
      return handleTypeCheck(ci->isTrueWhenEqual(), tcType, gs, guard, branch, s);
    } else if (isVectorGuard(f)) {
      // handle complex type checks that check for multiple types at once
      //   (now the only supported ones check for vectors)
      
      // either I know whether var is a vector, and then I know which branch to take
      
      // or I don't know whether var is a vector, and then I will add information depending
      //   on which branch is taken
      
      bool guardIsVector = (gs == SGS_VECTOR);
      bool guardIsNonVector = (gs == SGS_SYMBOL || gs == SGS_NIL);

      bool onlyTrueBranch = (guardIsVector && trueForVector(f)) || (guardIsNonVector && trueForNonVector(f));
      bool onlyFalseBranch = (guardIsVector && falseForVector(f)) || (guardIsNonVector && falseForNonVector(f));
      
      myassert(!onlyTrueBranch || !onlyFalseBranch);
      
      if (onlyTrueBranch) {
        {
          StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
          if (state->add()) {
            msg->trace("added *only* true branch on sexp guard (vector) of branch at", branch);
          }
        }
        return true;
      }

      if (onlyFalseBranch) {
        {
          StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
          if (state->add()) {
            msg->trace("added *only* false branch on sexp guard (vector) of branch at", branch);
          }
        }
        return true;
      }
      
      // add both branches, but with added information
      {
        StateWithGuardsTy* state = s.clone(branch->getSuccessor(0));
        if (gs != SGS_SYMBOL && impliesVectorWhenTrue(f)) {
          SEXPGuardTy newGS(SGS_VECTOR);
          state->sexpGuards[guard] = newGS; // added information from that the true branch was taken
        }
        if (state->add()) {
          msg->trace("added (also) true branch on sexp guard (vector) of branch at", branch);
        }
      }
      {
        StateWithGuardsTy* state = s.clone(branch->getSuccessor(1));
        if (gs != SGS_SYMBOL && impliesVectorWhenFalse(f)) {
          SEXPGuardTy newGS(SGS_VECTOR);
          state->sexpGuards[guard] = newGS; // added information from that the true branch was taken
        }
        if (state->add()) {
          msg->trace("added (also) false branch on sexp guard (vector) of branch at", branch);
        }
      }      
      return true;
    } else {
      return false;
    }
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

  if (!guard || !gv || !isGuard(guard)) {
    return false;
  }
  
  std::string guardSymbolName;
  SEXPGuardState gs = getGuardState(s.sexpGuards, guard, guardSymbolName);
  int succIndex = -1;

  if (gv == g->nilVariable) {

    // handle comparisons with R_NilValue
                  
    // if (x == R_NilValue) ...
    // if (x != R_NilValue) ...
    
    return handleNullCheck(ci->isTrueWhenEqual(), gs, guard, branch, s);
  }
  // handle comparisons with symbols
  
  if (!symbolsMap) {
    return false;
  }
  
  auto sfind = symbolsMap->find(gv);
  if (sfind == symbolsMap->end()) {
    return false;
  }
      
  const std::string& constSymbolName = sfind->second;

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
  if (gs == SGS_NIL || gs == SGS_VECTOR) {  // SGS_NIL and SGS_VECTOR cannot be a symbol
    if (ci->isTrueWhenEqual()) {
      // guard == R_XSymbol
      succIndex = 1;
    } else {
      // guard != R_XSymbol
      succIndex = 0;
    }
  }

  if (msg->debug()) {
    switch(succIndex) {
      case -1:
        msg->debug("undecided symbol branch on sexp guard variable " + varName(guard), branch);
        break;
      case 0:
        msg->debug("taking (only) true symbol branch on sexp guard variable " + varName(guard), branch);
        break;
      case 1:
        msg->debug("taking (only) false symbol branch on sexp guard variable " + varName(guard), branch);
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
        msg->trace("added true branch on sexp guard of symbol branch at", branch);
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
        msg->trace("added false branch on sexp guard of branch at", branch);
      }
    }
  }
  return true;  
}
  
PackedSEXPGuardsTy SEXPGuardsChecker::pack(const SEXPGuardsTy& sexpGuards) {

  // note we first have to call indexOf on each variable to make sure
  // it is indexed [later this should be perhaps done alreading when manipulating guards]
  
  for(SEXPGuardsTy::const_iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; ++gi) {
    AllocaInst* var = gi->first;
    varIndex.indexOf(var);
  }  
  
  PackedSEXPGuardsTy packed(varIndex.size());
  
  const VarIndexTy::Index& vars = varIndex.getIndex();
  unsigned idx = 0;
  
  // store variables in the order of varIndex, so that symbol names in the list of symbols
  //   can be mapped back to variables
  for(VarIndexTy::Index::const_iterator vi = vars.begin(), ve = vars.end(); vi != ve; ++vi, ++idx) {
    AllocaInst *var = *vi;
    
    auto vfind = sexpGuards.find(var);
    if (vfind == sexpGuards.end()) {
      continue;
    }
    const SEXPGuardTy& guard = vfind->second;
    SEXPGuardState gs = guard.state;
    
    unsigned base = idx * SGS_BITS;
    switch(gs) {
      case SGS_NIL:    packed.bits[base] = true; break;     // 1 0 0
      case SGS_NONNIL: packed.bits[base + 1] = true; break; // 0 1 0
      case SGS_SYMBOL: packed.bits[base] = true; packed.bits[base + 1] = true; // 1 1 0
                       packed.symbols.push_back(guard.symbolName);
                       break;
      case SGS_VECTOR: packed.bits[base + 2] = true; break;     // 0 0 1
      case SGS_UNKNOWN: break; // 0 0 0
    }
  }

  return packed;
}

SEXPGuardsTy SEXPGuardsChecker::unpack(const PackedSEXPGuardsTy& sexpGuards) {
  SEXPGuardsTy unpacked;
  unsigned nvars = sexpGuards.bits.size() / SGS_BITS;
  
  myassert(nvars * SGS_BITS == sexpGuards.bits.size());
  unsigned symbolIdx = 0;
  
  for(unsigned idx = 0; idx < nvars; idx++) {
    unsigned base = idx * SGS_BITS;
    SEXPGuardState gs = SGS_UNKNOWN;
    std::string symbolName;
    
    bool bit2 = sexpGuards.bits[base];
    bool bit1 = sexpGuards.bits[base + 1];
    bool bit0 = sexpGuards.bits[base + 2];
    
    if (bit2) {
      if (bit1) {
        gs = SGS_SYMBOL;
        symbolName = sexpGuards.symbols[symbolIdx];
        symbolIdx++;
      } else {
        gs = SGS_NIL;
      }
    } else {
      if (bit1) {
        gs = SGS_NONNIL;
      } else if (bit0) {
        gs = SGS_VECTOR;
      }
    }
    
    if (gs != SGS_UNKNOWN) {
      unpacked.insert({varIndex.at(idx), SEXPGuardTy(gs, symbolName)});
    }
  }
  return unpacked;
}
  
void SEXPGuardsChecker::hash(size_t& res, const SEXPGuardsTy& sexpGuards) {
  hash_combine(res, sexpGuards.size());
  for(SEXPGuardsTy::const_iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; ++gi) {
    AllocaInst* var = gi->first;
    const SEXPGuardTy& g = gi->second;
    hash_combine(res, (void *) var);
    hash_combine(res, (size_t) g.state);
    hash_combine(res, g.symbolName);
  } // ordered map
}

// common

void StateWithGuardsTy::dump(bool verbose) {
  
  errs() << "=== integer guards: " << &intGuards << "\n";
  for(IntGuardsTy::iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; ++gi) {
    AllocaInst *i = gi->first;
    IntGuardState s = gi->second;
    errs() << "   " << varName(i) << " ";
    if (verbose) {
      errs() << *i << " ";
    }
    errs() << " state: " << igs_name(s) << "\n";
  }

  errs() << "=== sexp guards: " << &sexpGuards << "\n";
  for(SEXPGuardsTy::iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; ++gi) {
    AllocaInst *i = gi->first;
    SEXPGuardTy &g = gi->second;
    
    errs() << "   " << varName(i) << " ";
    if (verbose) {
      errs() << *i << " ";
    }
    errs() << " state: " << sgs_name(g) << "\n";
  }  
}
