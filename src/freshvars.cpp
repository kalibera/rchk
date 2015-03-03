
#include "freshvars.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>

using namespace llvm;

static void handleCall(Instruction *in, FunctionsSetTy& possibleAllocators, FunctionsSetTy& allocatingFunctions, FreshVarsTy& freshVars,
    LineMessenger& msg, unsigned& refinableInfos) {
  
  CallSite cs(cast<Value>(in));
  if (!cs) {
    return;
  }
  const Function* targetFunc = cs.getCalledFunction();
  if (!targetFunc) {
    return;
  }
  
  if (allocatingFunctions.find(const_cast<Function*>(targetFunc)) == allocatingFunctions.end()) {
    return;
  }

  for(CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg_end(); ai != ae; ++ai) {
    Value *arg = *ai;
    CallSite csa(arg);
    if (csa) {
      Function *srcFun = csa.getCalledFunction();
      if (srcFun && possibleAllocators.find(srcFun) != possibleAllocators.end()) {
        msg.info("calling allocating function " + targetFunc->getName().str() + " with argument allocated using " + srcFun->getName().str(), in);
        refinableInfos++;
      }
    }
  }
  for (FreshVarsTy::iterator fi = freshVars.begin(), fe = freshVars.end(); fi != fe; ++fi) {
    AllocaInst *var = *fi;
    msg.info("unprotected variable " + var->getName().str() + " while calling allocating function " + targetFunc->getName().str(), in);
    refinableInfos++;
  }

}

static void handleLoad(Instruction *in, FunctionsSetTy& allocatingFunctions, FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {
  if (!LoadInst::classof(in)) {
    return;
  }
  LoadInst *li = cast<LoadInst>(in);
  if (!AllocaInst::classof(li->getPointerOperand())) {
    return;
  }
  AllocaInst *var = cast<AllocaInst>(li->getPointerOperand());
  if (freshVars.find(var) == freshVars.end()) { 
    return;
  }
  // a fresh variable is being loaded

  msg.debug("fresh variable " + var->getName().str() + " loaded and thus no longer fresh", in);
  freshVars.erase(var);

  if (!li->hasOneUse()) { // too restrictive? should look at other uses too?
    return;
  }
  CallSite cs(li->user_back());
  if (!cs) {
    return;
  }
  Function *targetFun = cs.getCalledFunction();
  
  // fresh variable passed to an allocating function - but this may be ok if it is callee-protect function
  //   or if the function allocates only after the fresh argument is no longer needed
  if (allocatingFunctions.find(targetFun) != allocatingFunctions.end()) {
    std::string varName = var->getName().str();
    if (varName.empty()) {
      unsigned i;
      for(i = 0; i < cs.arg_size(); i++) {
        if (cs.getArgument(i) == li) {
          varName = "arg " + std::to_string(i+1);
          break;
        }
      }
    }
    msg.info("calling allocating function " + targetFun->getName().str() + " with a fresh pointer (" + varName + ")", in);
    refinableInfos++;
  }
  return;
}

static void handleStore(Instruction *in, FunctionsSetTy& possibleAllocators, FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {
  if (!StoreInst::classof(in)) {
    return;
  }
  Value* storePointerOp = cast<StoreInst>(in)->getPointerOperand();
  Value* storeValueOp = cast<StoreInst>(in)->getValueOperand();

  if (!AllocaInst::classof(storePointerOp) || !storeValueOp->hasOneUse()) {
    return;
  }
  AllocaInst *var = cast<AllocaInst>(storePointerOp);
  CallSite csv(storeValueOp);
  bool isFresh = false;
  if (csv) {
    Function *vf = csv.getCalledFunction();
    if (vf && possibleAllocators.find(const_cast<Function*>(vf)) != possibleAllocators.end()) {
      freshVars.insert(var);
      msg.debug("initialized fresh SEXP variable " + var->getName().str(), in);
      isFresh = true;
    }
  }
  if (!isFresh) {
    if (freshVars.find(var) != freshVars.end()) {
      freshVars.erase(var);
      msg.debug("fresh variable " + var->getName().str() + " rewritten and thus no longer fresh", in);
    }
  }
}

void handleFreshVarsForNonTerminator(Instruction *in, FunctionsSetTy& possibleAllocators, FunctionsSetTy& allocatingFunctions, FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {

  handleCall(in, possibleAllocators, allocatingFunctions, freshVars, msg, refinableInfos);
  handleLoad(in, allocatingFunctions, freshVars, msg, refinableInfos);
  handleStore(in, possibleAllocators, freshVars, msg, refinableInfos);
}

void StateWithFreshVarsTy::dump(bool verbose) {

  errs() << "=== fresh vars: " << &freshVars << "\n";
  for(FreshVarsTy::iterator fi = freshVars.begin(), fe = freshVars.end(); fi != fe; ++fi) {
    AllocaInst *var = *fi;
    errs() << "   " << var->getName();
    if (verbose) {
      errs() << " " << *var;
    }
    errs() << "\n";
  }
}
