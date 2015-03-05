#ifndef RCHK_FRESHVARS_H
#define RCHK_FRESHVARS_H

#include "common.h"
#include "linemsg.h"
#include "state.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>

using namespace llvm;

typedef std::map<AllocaInst*, DelayedLineMessenger> ConditionalMessagesTy;

struct FreshVarsTy {
  VarsSetTy vars;
    // variables known to hold newly allocated pointers (SEXPs)
    // attempts to include only reliably unprotected pointers,
    // so as of now, any use of a variable removes it from the set

  ConditionalMessagesTy condMsgs;
    // info messages to be printed if a particular variable (key) 
};

struct StateWithFreshVarsTy : virtual public StateBaseTy {
  FreshVarsTy freshVars;
  
  StateWithFreshVarsTy(BasicBlock *bb, FreshVarsTy& freshVars): StateBaseTy(bb), freshVars(freshVars) {};
  StateWithFreshVarsTy(BasicBlock *bb): StateBaseTy(bb), freshVars() {};
  
  virtual StateWithFreshVarsTy* clone(BasicBlock *newBB) = 0;
  
  void dump(bool verbose);
};

void handleFreshVarsForNonTerminator(Instruction *in, FunctionsSetTy& possibleAllocators, FunctionsSetTy& allocatingFunctions,
  FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos);

#endif
