
#include "balance.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/GlobalVariable.h>

#include <llvm/Support/raw_ostream.h>

const std::string MSG_PFX = "[PB] ";

const bool QUIET_WHEN_CONFUSED = true;
  // do not report any messages and don't do any check once confused by the code
  // in such case, in practice, the messages are almost always false alarms

const std::string CONFUSION_DISCLAIMER = QUIET_WHEN_CONFUSED ? "results will be incomplete" : "results will be incorrect";

using namespace llvm;

// protection stack top "save variable" is a local variable
//   - which can be assigned the value of R_PPStackTop (typically at start of function)
//   - which can be assigned to R_PPStackTop (typically at end of function)
//   - it must have at least one load/store of R_PPStackTop

bool isProtectionStackTopSaveVariable(AllocaInst* var, GlobalVariable* ppStackTopVariable, VarBoolCacheTy& cache) {

  if (!ppStackTopVariable) {
    return false;
  }
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }
  
  bool usesPPStackTop = false;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (StoreInst::classof(u)) {
      Value *v = (cast<StoreInst>(u))->getValueOperand();
      if (LoadInst::classof(v) && cast<LoadInst>(v)->getPointerOperand() == ppStackTopVariable && v->hasOneUse()) {
        // savestack = R_PPStackTop
        usesPPStackTop = true;
        continue;
      }
    }

    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (l->hasOneUse() && StoreInst::classof(l->user_back()) &&
        cast<StoreInst>(l->user_back())->getPointerOperand() == ppStackTopVariable) {
        // R_PPStackTop = savestack
        usesPPStackTop = true;
        continue;
      }
    }
    // some other use
    cache.insert({var, false});
    return false;
  }
  cache.insert({var, usesPPStackTop});
  return usesPPStackTop;
}

// protection counter is a local variable
//   - integer type
//   - only modified by
//       assigning a constant to it (store instruction)
//       adding a constant to it
//         load
//         add
//	   store
//   - used as an argument to Rf_unprotect at least once
//	  load
//        call
//   - not used for anything but load, store
//

bool isProtectionCounterVariable(AllocaInst* var, Function* unprotectFunction) {

  if (!unprotectFunction) {
    return false;
  }

  if (!IntegerType::classof(var->getAllocatedType()) || var->isArrayAllocation()) {
    return false;
  }
  
  bool passedToUnprotect = false;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (StoreInst::classof(u)) {
      Value *v = (cast<StoreInst>(u))->getValueOperand();
      if (ConstantInt::classof(v)) {
        // nprotect = 3
        continue;
      }
      if (BinaryOperator::classof(v)) {
        // nprotect += 3;
        BinaryOperator *o = cast<BinaryOperator>(v);
        if (o->getOpcode() != Instruction::Add) {
          return false;
        }
        Value *nonConst;
        if (ConstantInt::classof(o->getOperand(0))) {
          nonConst = o->getOperand(1);
        } else if (ConstantInt::classof(o->getOperand(1))) {
          nonConst = o->getOperand(0);
        } else {
          return false;
        }
        
        if (LoadInst::classof(nonConst) && cast<LoadInst>(nonConst)->getPointerOperand() == var) {
          continue;
        }
      }
      return false;
    }
    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (!l->hasOneUse()) {
        return false;
      }
      Value* unprotectArg = l->user_back(); // the tentative argument to UNPROTECT
      
      if (BinaryOperator *bo = dyn_cast<BinaryOperator>(unprotectArg)) {  // support UNPROTECT(nprotect + 2)
        if (bo->getOpcode() == Instruction::Add) {  // FIXME: hasOneUse may be too restrictive
          if (unprotectArg->hasOneUse() && (ConstantInt::classof(bo->getOperand(0)) || ConstantInt::classof(bo->getOperand(1)))) {
            unprotectArg = unprotectArg->user_back();
          }
        }
      }
      
      CallSite cs(cast<Value>(unprotectArg));
      if (cs && cs.getCalledFunction() == unprotectFunction) {
        passedToUnprotect = true;
      }
      continue;
    }
    return false;
  }  
  return passedToUnprotect;
}

bool isProtectionCounterVariable(AllocaInst* var, Function* unprotectFunction, VarBoolCacheTy& cache) {

  if (!unprotectFunction) {
    return false;
  }
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }

  bool res = isProtectionCounterVariable(var, unprotectFunction);
  
  cache.insert({var, res});
  return res;
}

static void handleCall(Instruction *in, BalanceStateTy& b, GlobalsTy& g, VarBoolCacheTy& counterVarsCache, LineMessenger& msg, unsigned& refinableInfos) {
  
  CallSite cs(cast<Value>(in));
  if (!cs) {
    return;
  }
  const Function* targetFunc = cs.getCalledFunction();
  if (!targetFunc) {
    return;
  }
  if (targetFunc == g.protectFunction || targetFunc == g.protectWithIndexFunction) { // PROTECT(x)
    if (msg.debug()) msg.debug(MSG_PFX + "protect call", in);
    refinableInfos++;
    if (b.depth > MAX_DEPTH) {
      msg.info(MSG_PFX + "has too high protection stack depth " + CONFUSION_DISCLAIMER, NULL);
      if (QUIET_WHEN_CONFUSED) {
        b.confused = true;
        if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
      }
    } else {
      b.depth++;
    }
    return;
  }
  if (targetFunc == g.unprotectFunction) {
    Value* unprotectValue = cs.getArgument(0);
    if (ConstantInt::classof(unprotectValue)) { // e.g. UNPROTECT(3)
      uint64_t arg = (cast<ConstantInt>(unprotectValue))->getZExtValue();
      b.depth -= (int) arg;
      if (msg.debug()) msg.debug(MSG_PFX + "unprotect call using constant", in);
      if (b.countState != CS_DIFF && b.depth < 0) {
        msg.info(MSG_PFX + "has negative depth", in);
        refinableInfos++;
      }
      return;
    }
    // UNPROTECT(nprotect), UNPROTECT(nprotect + 3)
    
    int64_t npadd = 0;   // UNPROTECT(npvar + npadd)
    Value *npvar = unprotectValue;
    
    if (BinaryOperator *bo = dyn_cast<BinaryOperator>(unprotectValue)) {
      if (bo->getOpcode() == Instruction::Add) {
        if (ConstantInt *cint = dyn_cast<ConstantInt>(bo->getOperand(0))) {
          npadd = cint->getSExtValue();
          npvar = bo->getOperand(1);
        } else if (ConstantInt *cint = dyn_cast<ConstantInt>(bo->getOperand(1))) {
          npadd = cint->getSExtValue();
          npvar = bo->getOperand(0);
        } else {
          msg.info(MSG_PFX + "has an unsupported form of unprotect with a variable - binary add " + CONFUSION_DISCLAIMER, in);
          if (QUIET_WHEN_CONFUSED) {
            b.confused = true;
            if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
          }
          return;
        }
        if (msg.debug()) msg.debug(MSG_PFX + "unprotect call using counter and constant UNPROTECT(nprotect+" + std::to_string(npadd) + ")", in);        
      } else {
        msg.info(MSG_PFX + "has an unsupported form of unprotect with a variable - binary operation " + CONFUSION_DISCLAIMER, in);
        if (QUIET_WHEN_CONFUSED) {
          b.confused = true;
          if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
        }
        return;      
      }
    }
    
    if (LoadInst::classof(npvar)) { // e.g. UNPROTECT(numProtects), UNPROTECT(numProtects + 3)
      Value *varValue = const_cast<Value*>(cast<LoadInst>(npvar)->getPointerOperand());
      if (AllocaInst::classof(varValue)) {
        AllocaInst* var = cast<AllocaInst>(varValue);
        if (!isProtectionCounterVariable(var, g.unprotectFunction, counterVarsCache)) {
          msg.info(MSG_PFX + "has an unsupported form of unprotect with a variable " + CONFUSION_DISCLAIMER, in);
          if (QUIET_WHEN_CONFUSED) {
            b.confused = true;
            if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
          }
          return;
        }
        if (!b.counterVar) {
          b.counterVar = var;
        } else if (b.counterVar != var) {
          msg.info(MSG_PFX + "has an unsupported form of unprotect with a variable - multiple counter variables " + CONFUSION_DISCLAIMER, in);
          if (QUIET_WHEN_CONFUSED) {
            b.confused = true;
            if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
          }
          return;
        }
        if (b.countState == CS_NONE) {
          msg.info(MSG_PFX + "passes uninitialized counter of protects in a call to unprotect", in);
          refinableInfos++;
          return;
        }
        if (b.countState == CS_EXACT) {
          b.depth -= b.count + npadd;
          if (msg.debug()) msg.debug(MSG_PFX + "unprotect call using counter in exact state", in);
          if (b.depth < 0) {
            msg.info(MSG_PFX + "has negative depth", in);
            refinableInfos++;
          }
          return;
        }
        // countState == CS_DIFF
        assert(b.countState == CS_DIFF);
        if (msg.debug()) msg.debug(MSG_PFX + "unprotect call using counter in diff state", in);
        b.countState = CS_NONE;
        // depth keeps its value - it now becomes exact depth again
        
        b.depth -= npadd;
        if (b.depth < 0) {
          msg.info(MSG_PFX + "has negative depth after UNPROTECT(<counter>)", in);
          refinableInfos++;
        }
        return;
      }
    }
    msg.info(MSG_PFX + "has an unsupported form of unprotect (not constant, not variable " + CONFUSION_DISCLAIMER, in);
    if (QUIET_WHEN_CONFUSED) {
      b.confused = true;
      if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
    }
    return;
  }
  if (targetFunc == g.unprotectPtrFunction) {  // UNPROTECT_PTR(x)
    if (msg.debug()) msg.debug(MSG_PFX + "unprotect_ptr call", in);
    b.depth--;
    if (b.countState != CS_DIFF && b.depth < 0) {
      msg.info(MSG_PFX + "has negative depth", in);
      refinableInfos++;
    }
  }
}

static void handleLoad(Instruction *in, BalanceStateTy& b, GlobalsTy& g, VarBoolCacheTy& saveVarsCache, LineMessenger& msg, unsigned& refinableInfos) {

  if (!LoadInst::classof(in)) {
    return;
  }
  LoadInst *li = cast<LoadInst>(in);
  if (li->getPointerOperand() == g.ppStackTopVariable) { // savestack = R_PPStackTop
    if (li->hasOneUse()) { // this will hold whenever it turns out to be a store to topsave variable
      User* user = li->user_back();
      if (StoreInst::classof(user)) {
        StoreInst* topStoreInst = cast<StoreInst>(user);
        if (AllocaInst::classof(topStoreInst->getPointerOperand())) {
          AllocaInst* topStore = cast<AllocaInst>(topStoreInst->getPointerOperand());
          if (isProtectionStackTopSaveVariable(topStore, g.ppStackTopVariable, saveVarsCache)) {
            // topStore is the alloca instruction for the local variable where R_PPStack is saved to
            // e.g. %save = alloca i32, align 4
            if (b.countState == CS_DIFF) {
              msg.info(MSG_PFX + "saving value of PPStackTop while in differential count state " + CONFUSION_DISCLAIMER, in);
              if (QUIET_WHEN_CONFUSED) {
                b.confused = true;
                if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
              }
              refinableInfos++;
              return;
            }
            b.savedDepth = b.depth;
            if (msg.debug()) msg.debug(MSG_PFX + "saving value of PPStackTop", in);
          }
        }
      }
    }
  }
}

static void handleStore(Instruction *in, BalanceStateTy& b, GlobalsTy& g, VarBoolCacheTy& saveVarsCache, VarBoolCacheTy& counterVarsCache,
    LineMessenger& msg, unsigned& refinableInfos) {
    
  if (!StoreInst::classof(in)) {
    return;
  }
  Value* storePointerOp = cast<StoreInst>(in)->getPointerOperand();
  Value* storeValueOp = cast<StoreInst>(in)->getValueOperand();
  
  if (storePointerOp == g.ppStackTopVariable) { // R_PPStackTop = savestack
    if (LoadInst::classof(storeValueOp)) {          
      Value *varValue = cast<LoadInst>(storeValueOp)->getPointerOperand();
      if (AllocaInst::classof(varValue) && 
        isProtectionStackTopSaveVariable(cast<AllocaInst>(varValue), g.ppStackTopVariable, saveVarsCache)) {

        if (b.countState == CS_DIFF) {
          msg.info(MSG_PFX + "restoring value of PPStackTop while in differential count state " + CONFUSION_DISCLAIMER, in);
          if (QUIET_WHEN_CONFUSED) {
            b.confused = true;
            if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
          }
          return;
        }
        if (msg.debug()) msg.debug(MSG_PFX + "restoring value of PPStackTop", in);
        if (b.savedDepth < 0) {
          msg.info(MSG_PFX + "restores PPStackTop from uninitialized local variable", in);
          refinableInfos++;
        } else {
          b.depth = b.savedDepth;
        }
        return;
      }
    }
    msg.info(MSG_PFX + "manipulates PPStackTop directly " + CONFUSION_DISCLAIMER, in);
    if (QUIET_WHEN_CONFUSED) {
      b.confused = true;
      if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
    }
    return;  
  }
  if (AllocaInst::classof(storePointerOp) && 
    isProtectionCounterVariable(cast<AllocaInst>(storePointerOp), g.unprotectFunction, counterVarsCache)) { // nprotect = ... 
              
    AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
    if (!b.counterVar) {
      b.counterVar = storePointerVar;
    } else if (b.counterVar != storePointerVar) {
      msg.info(MSG_PFX + "uses multiple pointer protection counters " + CONFUSION_DISCLAIMER, NULL);
      if (QUIET_WHEN_CONFUSED) {
        b.confused = true;
        if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", in);
      }
      return;
    }
    if (ConstantInt::classof(storeValueOp)) {
      // nprotect = 3
      if (b.countState == CS_DIFF) {
        msg.info(MSG_PFX + "setting counter value while in differential mode (forgetting protects)?", in);
        refinableInfos++;
        return;
      }
      int64_t arg = (cast<ConstantInt>(storeValueOp))->getSExtValue();
      b.count = arg;
      if (b.count > MAX_COUNT) {
        // turn the counter to differential state
        if (msg.debug()) msg.debug(MSG_PFX + "setting counter to a large constant, switching to differential state", in);
        assert(s.balance.countState == CS_EXACT);
        b.countState = CS_DIFF;
        b.depth -= b.count;
        b.count = -1;
      } else {
        b.countState = CS_EXACT;
        if (msg.debug()) msg.debug(MSG_PFX + "setting counter to a constant", in);
        if (b.count < 0) {
          msg.info(MSG_PFX + "protection counter set to a negative value", in);
        }
      }
      return;
    }
    if (BinaryOperator::classof(storeValueOp)) {
      // nprotect += 3;
      BinaryOperator *o = cast<BinaryOperator>(storeValueOp);
      if (o->getOpcode() == Instruction::Add) {
        Value *nonConstOp = NULL;
        Value *constOp = NULL;

        if (ConstantInt::classof(o->getOperand(0))) {
          constOp = o->getOperand(0);
          nonConstOp = o->getOperand(1);
        } else if (ConstantInt::classof(o->getOperand(1))) {
          constOp = o->getOperand(1);
          nonConstOp = o->getOperand(0);
        } 
        
        if (nonConstOp && LoadInst::classof(nonConstOp) && cast<LoadInst>(nonConstOp)->getPointerOperand() == b.counterVar &&
          constOp && ConstantInt::classof(constOp)) {
                  
          if (b.countState == CS_NONE) {
            msg.info(MSG_PFX + "adds a constant to an uninitialized counter variable", in);
            refinableInfos++;
            return;
          }
          int64_t arg = (cast<ConstantInt>(constOp))->getSExtValue();
          if (msg.debug()) msg.debug(MSG_PFX + "adding a constant to counter", in);
          if (b.countState == CS_EXACT) {
            b.count += arg;
            if (b.count < 0) {
              msg.info(MSG_PFX + "protection counter went negative after add", in);
              refinableInfos++;
            } else if (b.count > MAX_COUNT) {
              // turn the counter to differential state
              b.countState = CS_DIFF;
              b.depth -= b.count;
              b.count = -1;
            }
            return;
          }
          // countState == CS_DIFF
          assert(b.countState == CS_DIFF);
          b.depth -= arg; // fewer protects on top of counter than before
        }
      }
    }
  }  
}

void handleBalanceForNonTerminator(Instruction *in, BalanceStateTy& b, GlobalsTy& g, VarBoolCacheTy& counterVarsCache, VarBoolCacheTy& saveVarsCache,
    LineMessenger& msg, unsigned& refinableInfos) {

  if (b.countState != CS_DIFF && b.depth < 0) {
    if (msg.trace()) msg.trace(MSG_PFX + "skipping instruction as depth is already negative in non-diff state", in);
    return;
      // do not print more errors that caused by a problem already reported
  }

  if (!QUIET_WHEN_CONFUSED || !b.confused) {
    handleCall(in, b, g, counterVarsCache, msg, refinableInfos);
  } else {
    if (msg.trace()) msg.trace(MSG_PFX + "not handling instruction as (already) confused", in);
    return;
  }

  if (!QUIET_WHEN_CONFUSED || !b.confused) {
    handleLoad(in, b, g, saveVarsCache, msg, refinableInfos);
  } else {
    if (msg.trace()) msg.trace(MSG_PFX + "not handling instruction as (already) confused", in);
    return;
  }

  if (!QUIET_WHEN_CONFUSED || !b.confused) {
    handleStore(in, b, g, saveVarsCache, counterVarsCache, msg, refinableInfos);
  } else {
    if (msg.trace()) msg.trace(MSG_PFX + "not handling instruction as (already) confused", in);
    return;
  }
}

bool handleBalanceForTerminator(TerminatorInst* t, StateWithBalanceTy& s, GlobalsTy& g, VarBoolCacheTy& counterVarsCache, 
    LineMessenger& msg, unsigned& refinableInfos) {

  if (QUIET_WHEN_CONFUSED && s.balance.confused) {
    if (msg.trace()) msg.trace(MSG_PFX + "not interpreting terminator because (already) confused", t);
    return false; // generate more states - other currently used tools may not be all confused
  }

  if (ReturnInst::classof(t)) {
    if (s.balance.countState == CS_DIFF || s.balance.depth != 0) {
      msg.info(MSG_PFX + "has possible protection stack imbalance", t);
      refinableInfos++;
    }
    return true; // no successors
  }
      
  if (!BranchInst::classof(t)) {
    return false;
  }
        
  BranchInst* br = cast<BranchInst>(t);
  if (!br->isConditional() || !CmpInst::classof(br->getCondition())) {
    return false;
  }
  
  CmpInst* ci = cast<CmpInst>(br->getCondition());
  // if (x == y) ... [comparison of two variables]
          
  // comparison with constant
  LoadInst *li;
  Constant *constOp;
  
  if (Constant::classof(ci->getOperand(0)) && LoadInst::classof(ci->getOperand(1))) {
    constOp = cast<Constant>(ci->getOperand(0));
    li = cast<LoadInst>(ci->getOperand(1));
  } else if (LoadInst::classof(ci->getOperand(0)) && Constant::classof(ci->getOperand(1))) {
    li = cast<LoadInst>(ci->getOperand(0));
    constOp = cast<Constant>(ci->getOperand(1));
  } else {
    return false;
  }
  
  if (!AllocaInst::classof(li->getPointerOperand())) {
    return false;
  }
  AllocaInst *var = cast<AllocaInst>(li->getPointerOperand());

  // if (nprotect) UNPROTECT(nprotect)
  if (!isProtectionCounterVariable(var, g.unprotectFunction, counterVarsCache)) {
    return false;
  }
  if (!s.balance.counterVar) {
    s.balance.counterVar = var;
  } else if (s.balance.counterVar != var) {
    msg.info(MSG_PFX + "uses multiple pointer protection counters " + CONFUSION_DISCLAIMER, NULL);
    if (QUIET_WHEN_CONFUSED) {
      s.balance.confused = true;
      if (msg.trace()) msg.trace(MSG_PFX + "confused, will not print more messages", t);
    }
    refinableInfos++;
    return false;
  }
  if (s.balance.countState == CS_NONE) {
    msg.info(MSG_PFX + "branches based on an uninitialized value of the protection counter variable", t);
    refinableInfos++;
    return false;
  }
  if (s.balance.countState == CS_EXACT) {
    // we can unfold the branch with general body, and with comparisons against nonzero
    // as we know the exact value of the counter
    //
    // if (nprotect??const) { .... }
                  
    Constant *knownLhs = ConstantInt::getSigned(s.balance.counterVar->getAllocatedType(), s.balance.count);
    Constant *res = ConstantExpr::getCompare(ci->getPredicate(), knownLhs, constOp);
    assert(ConstantInt::classof(res));
                
    // add only the relevant successor
    if (msg.debug()) msg.debug(MSG_PFX + "folding out branch on counter value", t);
    BasicBlock *succ;
    if (!res->isZeroValue()) {
      succ = br->getSuccessor(0);
    } else {
      succ = br->getSuccessor(1);
    }
    {
      StateWithBalanceTy *state = s.clone(succ);
      if (state->add()) {
        if (msg.trace()) msg.trace(MSG_PFX + "added folded successor of", t);
      }
    }
    return true;
  }
  // countState == CS_DIFF
  assert(s.balance.countState == CS_DIFF);
  // we don't know if nprotect is zero
  // but if the expression is just "if (nprotect) UNPROTECT(nprotect)", we can
  //   treat it as "UNPROTECT(nprotect)", because UNPROTECT(0) does nothing
  if (!ci->isEquality() || !ConstantInt::classof(constOp) || !cast<ConstantInt>(constOp)->isZero()) {
    return false;
  }
    
  BasicBlock *unprotectSucc; // the successor that would have to be UNPROTECT(nprotect)
  BasicBlock *joinSucc; // the other successor (where unprotectSucc would have to jump to)
  if (ci->isTrueWhenEqual()) {
    unprotectSucc = br->getSuccessor(1);
    joinSucc = br->getSuccessor(0);
  } else {
    unprotectSucc = br->getSuccessor(0);
    joinSucc = br->getSuccessor(1);
  }
                  
  BasicBlock::iterator it = unprotectSucc->begin();

  // ... loads a protection counter variable first
  LoadInst *loadInst = NULL;
  if (it != unprotectSucc->end() && LoadInst::classof(it)) {
    loadInst = cast<LoadInst>(it);
  } else {
    return false;
  }
  if (loadInst->getPointerOperand() != var) {
    return false;
  }
  ++it;
    
  // ... calls UNPROTECT with it
  if (it == unprotectSucc->end()) {
    return false;
  }
  CallSite cs(cast<Value>(it));
  if (!cs || cs.getCalledFunction() != g.unprotectFunction || cs.getArgument(0) != loadInst) {
    return false;
  }
  ++it;

  // ... and then merges back from the branch    
  if (it == unprotectSucc->end() || !BranchInst::classof(it)) {
    return false;
  }
  BranchInst *bi = cast<BranchInst>(it);
  if (bi->isConditional() || bi->getSuccessor(0) != joinSucc) {
    return false;
  }

  // yes, now we know we have "if (np) { UNPROTECT(np) ..."
                            
  // FIXME: could there instead be returns in both branches?
                            
  
  if (msg.debug()) msg.debug(MSG_PFX + "simplifying unprotect conditional on counter value (diff state)", t);
  
  /*
  
  it is fine to interpret UNPROTECT(nprotect) right away if doing just balance checking,
    but if freshvars are also active, they'd miss the UNPROTECT(nprotect), so it is 
    better to continue only with the unprotectSucc
  
  // interpret UNPROTECT(nprotect)
  s.balance.countState = CS_NONE;
  if (s.balance.depth < 0) {
    msg.info(MSG_PFX + "has negative depth after UNPROTECT(<counter>)", t);
    refinableInfos++;
    return false;
  }
  // next process the code after the if
  {
    StateWithBalanceTy* state = s.clone(joinSucc);
    if (state->add()) {
      if (msg.trace()) msg.trace(MSG_PFX + "added folded successor (diff counter state) of", t);
    }
  }
  */
  
  {
    StateWithBalanceTy* state = s.clone(unprotectSucc);
    if (state->add()) {
      if (msg.trace()) msg.trace(MSG_PFX + "added UNPROTECT(nprotect) successor of if (nprotect) UNPROTECT(nprotect)", t);
    }
  }  
  return true;
}

std::string cs_name(CountState cs) {
  switch(cs) {
    case CS_NONE: return "uninitialized (none)";
    case CS_EXACT: return "exact";
    case CS_DIFF: return "differential";
  }
}

void StateWithBalanceTy::dump(bool verbose) {

  errs() << "=== balance confused: " << balance.confused << "\n";
  errs() << "=== depth: " << balance.depth << "\n";
  if (balance.savedDepth != -1) {
    errs() << "=== savedDepth: " << balance.savedDepth << "\n";
  }
  if (balance.count != -1) {
    errs() << "=== count: " << balance.count << "\n";
  }
  if (balance.countState != CS_NONE) {
    errs() << "=== countState: " << cs_name(balance.countState) << "\n";
  }
  if (balance.counterVar != NULL) {
    errs() << "=== counterVar: " << varName(balance.counterVar) << "\n";
  }
}
