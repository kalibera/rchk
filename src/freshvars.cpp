
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
  for (VarsSetTy::iterator fi = freshVars.vars.begin(), fe = freshVars.vars.end(); fi != fe; ++fi) {
    AllocaInst *var = *fi;
    std::string message = "unprotected variable " + var->getName().str() + " while calling allocating function " + targetFunc->getName().str();
    
    // prepare a conditional message
    auto vsearch = freshVars.condMsgs.find(var);
    if (vsearch == freshVars.condMsgs.end()) {
      DelayedLineMessenger dmsg(msg.debug(), msg.trace(), msg.uniqueMsg());
      dmsg.info(message, in);
      freshVars.condMsgs.insert({var, dmsg});  
    } else {
      vsearch->second.info(message, in);
    }
    
    msg.debug("created a conditional message \"" + message + "\"", in);
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
  // a variable is being loaded
  
  // check for conditional messages
  auto vsearch = freshVars.condMsgs.find(var);
  if (vsearch != freshVars.condMsgs.end()) {
    vsearch->second.flushTo(msg, in->getParent()->getParent());
    freshVars.condMsgs.erase(vsearch);
    msg.debug("Printed conditional messages on use of variable " + var->getName().str(), in);
  }
  
  if (freshVars.vars.find(var) == freshVars.vars.end()) { 
    return;
  }
  // a fresh variable is being loaded

  msg.debug("fresh variable " + var->getName().str() + " loaded and thus no longer fresh", in);
  freshVars.vars.erase(var);

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
  
  // a variable is being killed by the store, erase conditional messages if any
  if (freshVars.condMsgs.erase(var)) {
    msg.debug("removed conditional messages as variable " + var->getName().str() + " is rewritten.", in);
  }
  
  CallSite csv(storeValueOp);
  if (csv) {
    Function *vf = csv.getCalledFunction();
    if (vf && possibleAllocators.find(const_cast<Function*>(vf)) != possibleAllocators.end()) {
      // the store (re-)creates a fresh variable
      freshVars.vars.insert(var);
      msg.debug("initialized fresh SEXP variable " + var->getName().str(), in);
      return;
    }
  }
  
  // the store turns a variable into non-fresh  
  if (freshVars.vars.find(var) != freshVars.vars.end()) {
    freshVars.vars.erase(var);
    msg.debug("fresh variable " + var->getName().str() + " rewritten and thus no longer fresh", in);
  }
}

void handleFreshVarsForNonTerminator(Instruction *in, FunctionsSetTy& possibleAllocators, FunctionsSetTy& allocatingFunctions, 
    FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {

  handleCall(in, possibleAllocators, allocatingFunctions, freshVars, msg, refinableInfos);
  handleLoad(in, allocatingFunctions, freshVars, msg, refinableInfos);
  handleStore(in, possibleAllocators, freshVars, msg, refinableInfos);
}

void StateWithFreshVarsTy::dump(bool verbose) {

  errs() << "=== fresh vars [conditional messages not dumped]: " << &freshVars << "\n";
  for(VarsSetTy::iterator fi = freshVars.vars.begin(), fe = freshVars.vars.end(); fi != fe; ++fi) {
    AllocaInst *var = *fi;
    errs() << "   " << var->getName();
    if (verbose) {
      errs() << " " << *var;
    }
    errs() << "\n";
  }
}
