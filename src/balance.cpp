
#include "balance.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/GlobalVariable.h>

#include <llvm/Support/raw_ostream.h>

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
  cache.insert({var, true});
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
      CallSite cs(cast<Value>(l->user_back()));
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
    b.depth++;
    msg.debug("protect call", in);
    return;
  }
  if (targetFunc == g.unprotectFunction) {
    Value* unprotectValue = cs.getArgument(0);
    if (ConstantInt::classof(unprotectValue)) { // e.g. UNPROTECT(3)
      uint64_t arg = (cast<ConstantInt>(unprotectValue))->getZExtValue();
      b.depth -= (int) arg;
      msg.debug("unprotect call using constant", in);              
      if (b.countState != CS_DIFF && b.depth < 0) {
        msg.info("has negative depth", in);
        refinableInfos++;
      }
      return;
    }
    if (LoadInst::classof(unprotectValue)) { // e.g. UNPROTECT(numProtects)
      Value *varValue = const_cast<Value*>(cast<LoadInst>(unprotectValue)->getPointerOperand());
      if (AllocaInst::classof(varValue)) {
        AllocaInst* var = cast<AllocaInst>(varValue);
        if (!isProtectionCounterVariable(var, g.unprotectFunction, counterVarsCache)) {
          msg.info("has an unsupported form of unprotect with a variable (results will be incorrect)", in);
          return;
        }
        if (!b.counterVar) {
          b.counterVar = var;
        } else if (b.counterVar != var) {
          msg.info("has an unsupported form of unprotect with a variable - multiple counter variables (results will be incorrect)", in);
          return;
        }
        if (b.countState == CS_NONE) {
          msg.info("passes uninitialized counter of protects in a call to unprotect", in);
          refinableInfos++;
          return;
        }
        if (b.countState == CS_EXACT) {
          b.depth -= b.count;
          msg.debug("unprotect call using counter in exact state", in);                
          if (b.depth < 0) {
            msg.info("has negative depth", in);
            refinableInfos++;
          }
          return;
        }
        // countState == CS_DIFF
        assert(b.countState == CS_DIFF);
        msg.debug("unprotect call using counter in diff state", in);
        b.countState = CS_NONE;
        // depth keeps its value - it now becomes exact depth again
        if (b.depth < 0) {
          msg.info("has negative depth after UNPROTECT(<counter>)", in);
          refinableInfos++;
        }
      }
    }
    return;
  }
  if (targetFunc == g.unprotectPtrFunction) {  // UNPROTECT_PTR(x)
    msg.debug("unprotect_ptr call", in);
    b.depth--;
    if (b.countState != CS_DIFF && b.depth < 0) {
      msg.info("has negative depth", in);
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
    if (li->hasOneUse()) {
      User* user = li->user_back();
      if (StoreInst::classof(user)) {
        StoreInst* topStoreInst = cast<StoreInst>(user);
        if (AllocaInst::classof(topStoreInst->getPointerOperand())) {
          AllocaInst* topStore = cast<AllocaInst>(topStoreInst->getPointerOperand());
          if (isProtectionStackTopSaveVariable(topStore, g.ppStackTopVariable, saveVarsCache)) {
            // topStore is the alloca instruction for the local variable where R_PPStack is saved to
            // e.g. %save = alloca i32, align 4
            if (b.countState == CS_DIFF) {
              msg.info("saving value of PPStackTop while in differential count state (results will be incorrect)", in);
              refinableInfos++;
              return;
            }
            b.savedDepth = b.depth;
            msg.debug("saving value of PPStackTop", in);
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
          msg.info("restoring value of PPStackTop while in differential count state (results will be incorrect)", in);
          return;
        }
        msg.debug("restoring value of PPStackTop", in);
        if (b.savedDepth < 0) {
          msg.info("restores PPStackTop from uninitialized local variable", in);
          refinableInfos++;
        } else {
          b.depth = b.savedDepth;
        }
        return;
      }
    }
    msg.info("manipulates PPStackTop directly (results will be incorrect)", in);
    return;  
  }
  if (AllocaInst::classof(storePointerOp) && 
    isProtectionCounterVariable(cast<AllocaInst>(storePointerOp), g.unprotectFunction, counterVarsCache)) { // nprotect = ... 
              
    AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
    if (!b.counterVar) {
      b.counterVar = storePointerVar;
    } else if (b.counterVar != storePointerVar) {
      msg.info("uses multiple pointer protection counters (results will be incorrect)", in);
      return;
    }
    if (ConstantInt::classof(storeValueOp)) {
      // nprotect = 3
      if (b.countState == CS_DIFF) {
        msg.info("setting counter value while in differential mode (forgetting protects)?", in);
        refinableInfos++;
        return;
      }
      int64_t arg = (cast<ConstantInt>(storeValueOp))->getSExtValue();
      b.count = arg;
      b.countState = CS_EXACT;
      msg.debug("setting counter to a constant", in);              
      if (b.count < 0) {
        msg.info("protection counter set to a negative value", in);
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
            msg.info("adds a constant to an uninitialized counter variable", in);
            refinableInfos++;
            return;
          }
          int64_t arg = (cast<ConstantInt>(constOp))->getSExtValue();
          msg.debug("adding a constant to counter", in);
          if (b.countState == CS_EXACT) {
            b.count += arg;
            if (b.count < 0) {
              msg.info("protection counter went negative after add", in);
              refinableInfos++;
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

  handleCall(in, b, g, counterVarsCache, msg, refinableInfos);
  handleLoad(in, b, g, saveVarsCache, msg, refinableInfos);
  handleStore(in, b, g, saveVarsCache, counterVarsCache, msg, refinableInfos);
}


std::string cs_name(CountState cs) {
  switch(cs) {
    case CS_NONE: return "uninitialized (none)";
    case CS_EXACT: return "exact";
    case CS_DIFF: return "differential";
  }
}

void StateWithBalanceTy::dump(bool verbose) {

  errs() << "=== depth: " << balance.depth << "\n";
  errs() << "=== savedDepth: " << balance.savedDepth << "\n";
  errs() << "=== count: " << balance.count << "\n";
  errs() << "=== countState: " << cs_name(balance.countState) << "\n";
  errs() << "=== counterVar: " << balance.counterVar->getName() << "\n";
}
