
#include "freshvars.h"
#include "guards.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>

const bool REPORT_FRESH_ARGUMENTS = false;
 // now disabled as this is a common source of false alarms (many functions are callee-protect)

using namespace llvm;

static void handleCall(Instruction *in, CalledModuleTy *cm, SEXPGuardsTy *sexpGuards, FreshVarsTy& freshVars,
    LineMessenger& msg, unsigned& refinableInfos) {
  
  const CalledFunctionTy *tgt = cm->getCalledFunction(in, sexpGuards);
  if (!tgt || !cm->isCAllocating(tgt)) {
    return;
  }
  
  CallSite cs(cast<Value>(in));
  assert(cs);
  assert(cs.getCalledFunction());

  if (REPORT_FRESH_ARGUMENTS) {
    for(CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg_end(); ai != ae; ++ai) {
      Value *arg = *ai;
      const CalledFunctionTy *src = cm->getCalledFunction(arg, sexpGuards);
      if (!src || !cm->isPossibleCAllocator(src)) {
        continue;
      }
      msg.info("calling allocating function " + funName(tgt) + " with argument allocated using " + funName(src), in);
      refinableInfos++;
    }
  }
  
  if (freshVars.vars.size() > 0) {
  
    // compute all variables passed to the call
    //   (if a fresh variable is passed to a function, it is not to be reported here as error)
    
    VarsSetTy passedVars;
    
    for(CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg_end(); ai != ae; ++ai) {
      Value *arg = *ai;
      
      if (LoadInst *li = dyn_cast<LoadInst>(arg)) {
        if (AllocaInst *lvar = dyn_cast<AllocaInst>(li->getPointerOperand())) {
          passedVars.insert(lvar);
        }
      }
    }
  
    for (FreshVarsVarsTy::iterator fi = freshVars.vars.begin(), fe = freshVars.vars.end(); fi != fe; ++fi) {
      AllocaInst *var = *fi;
      
      if (passedVars.find(var) != passedVars.end()) {
        // this fresh variable is in fact being passed to the function, so don't report it
        continue;
      }
      std::string message = "unprotected variable " + varName(var) + " while calling allocating function " + funName(tgt);
    
      // prepare a conditional message
      auto vsearch = freshVars.condMsgs.find(var);
      if (vsearch == freshVars.condMsgs.end()) {
        DelayedLineMessenger dmsg(&msg);
        dmsg.info(message, in);
        freshVars.condMsgs.insert({var, dmsg});
        if (msg.debug()) msg.debug("created conditional message \"" + message + "\" first for variable " + varName(var), in);
      } else {
        DelayedLineMessenger& dmsg = vsearch->second;
        dmsg.info(message, in);
        if (msg.debug()) msg.debug("added conditional message \"" + message + "\" for variable " + varName(var) + "(size " + std::to_string(dmsg.size()) + ")", in);
      }
    }
  }
}

static void handleLoad(Instruction *in, CalledModuleTy *cm, SEXPGuardsTy *sexpGuards, FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {
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
    vsearch->second.flush();
    refinableInfos++;
    freshVars.condMsgs.erase(vsearch);
    if (msg.debug()) msg.debug("printed conditional messages on use of variable " + varName(var), in);
  }
  
  if (freshVars.vars.find(var) == freshVars.vars.end()) { 
    return;
  }
  // a fresh variable is being loaded

  if (0) {
    // this is rather restrictive, many loads are benign or, benign with a good probability
    
    if (msg.debug()) msg.debug("fresh variable " + varName(var) + " loaded and thus no longer fresh", in);
    freshVars.vars.erase(var);
  }
  
  for(Value::user_iterator ui = li->user_begin(), ue = li->user_end(); ui != ue; ++ui) {
    User *u = *ui;
    
    CallSite cs(u);  // variable passed to a call as argument
    if (cs) {
      Function *tgt = cs.getCalledFunction();
      if (tgt) {
        // FIXME: could these and similar functions be discovered automatically?
        if (tgt->getName() == "Rf_protect" || tgt->getName() == "R_ProtectWithIndex" || tgt->getName() == "R_PreserveObject") {
          if (msg.debug()) msg.debug("fresh variable " + varName(var) + " passed to known protecting function " + funName(tgt) + " and thus no longer fresh" , in);  
          freshVars.vars.erase(var);
          break;
        }
      }
      continue;
    }
    
    if (StoreInst *sinst = dyn_cast<StoreInst>(u)) { // variable stored
      if (sinst->getValueOperand() == u) {
        if (!AllocaInst::classof(sinst->getPointerOperand())) {
          // variable stored into a non-local variable (e.g. into a global or into a location derived from a local variable, e.g. setting an attribute
          // of an SEXP in a local variable)
          
          // the heuristic is that these stores are usually implicitly protecting
          if (msg.debug()) msg.debug("fresh variable " + varName(var) + " stored into a global or derived local, and thus no longer fresh" , in);
          freshVars.vars.erase(var);
          break;
        }
      }
      continue;
    }
  }

  if (REPORT_FRESH_ARGUMENTS) {
    if (!li->hasOneUse()) { // too restrictive? should look at other uses too?
      return;
    }
    const CalledFunctionTy* tgt = cm->getCalledFunction(li->user_back(), sexpGuards);
    if (!tgt || !cm->isCAllocating(tgt)) {
      return;
    }
  
    // fresh variable passed to an allocating function - but this may be ok if it is callee-protect function
    //   or if the function allocates only after the fresh argument is no longer needed
    std::string nameSuffix = "";
    if (var->getName().str().empty()) {
      unsigned i;
      CallSite cs(cast<Value>(li->user_back()));
      assert(cs);
      for(i = 0; i < cs.arg_size(); i++) {
        if (cs.getArgument(i) == li) {
          nameSuffix = " <arg " + std::to_string(i+1) + ">";
          break;
        }
      }
    }
    msg.info("calling allocating function " + funName(tgt) + " with a fresh pointer (" + varName(var) + nameSuffix + ")", in);
    refinableInfos++;
  }
}

static void handleStore(Instruction *in, CalledModuleTy *cm, SEXPGuardsTy *sexpGuards, FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {
  if (!StoreInst::classof(in)) {
    return;
  }
  Value* storePointerOp = cast<StoreInst>(in)->getPointerOperand();
  Value* storeValueOp = cast<StoreInst>(in)->getValueOperand();

  if (!AllocaInst::classof(storePointerOp)) {
    return;
  }
  AllocaInst *var = cast<AllocaInst>(storePointerOp);
  
  // a variable is being killed by the store, erase conditional messages if any
  if (freshVars.condMsgs.erase(var)) {
    if (msg.debug()) msg.debug("removed conditional messages as variable " + varName(var) + " is rewritten.", in);
  }
  
  const CalledFunctionTy *srcFun = cm->getCalledFunction(storeValueOp, sexpGuards);
  if (srcFun && storeValueOp->hasOneUse()) { 
    // only allowing single use, the other use can be and often is PROTECT
    
    if (cm->isPossibleCAllocator(srcFun)) { // FIXME: this is very approximative -- we would rather need to know guaranteed allocators
      // the store (re-)creates a fresh variable
      freshVars.vars.insert(var);
      if (msg.debug()) msg.debug("initialized fresh SEXP variable " + varName(var), in);
      return;
    }
  }
  
  // the store turns a variable into non-fresh  
  if (freshVars.vars.find(var) != freshVars.vars.end()) {
    freshVars.vars.erase(var);
    if (msg.debug()) msg.debug("fresh variable " + varName(var) + " rewritten and thus no longer fresh", in);
  }
}

void handleFreshVarsForNonTerminator(Instruction *in, CalledModuleTy *cm, SEXPGuardsTy *sexpGuards,
    FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos) {

  handleCall(in, cm, sexpGuards, freshVars, msg, refinableInfos);
  handleLoad(in, cm, sexpGuards, freshVars, msg, refinableInfos);
  handleStore(in, cm, sexpGuards, freshVars, msg, refinableInfos);
}

void StateWithFreshVarsTy::dump(bool verbose) {

  errs() << "=== fresh vars: " << &freshVars << "\n";
  for(FreshVarsVarsTy::iterator fi = freshVars.vars.begin(), fe = freshVars.vars.end(); fi != fe; ++fi) {
    AllocaInst *var = *fi;
    errs() << "   " << var->getName();
    if (verbose) {
      errs() << " " << *var;
    }
    
    auto vsearch = freshVars.condMsgs.find(var);
    if (vsearch != freshVars.condMsgs.end()) {
      errs() << " conditional messages: \n";
      DelayedLineMessenger& dmsg = vsearch->second;
      dmsg.print("    ");
    }
    
    errs() << "\n";
  }
}
