#ifndef RCHK_FRESHVARS_H
#define RCHK_FRESHVARS_H

#include "common.h"

#include "linemsg.h"
#include "state.h"
#include "guards.h"
#include "liveness.h"

#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>

using namespace llvm;

const int MAX_PSTACK_SIZE = 64;

typedef std::map<AllocaInst*, int> FreshVarsVarsTy;
typedef std::map<AllocaInst*, DelayedLineMessenger> ConditionalMessagesTy;
typedef std::vector<AllocaInst*> VarsVectorTy;

struct FreshVarsTy {
  FreshVarsVarsTy vars;
    // variables known to hold newly allocated pointers (SEXPs)
    // attempts to include only reliably unprotected pointers,

    // the int is the number of protects of this variable
    // 0 no protection
    // 1 protected once
    // ... 
    //   (implicitly protected variables are treated as non-fresh, hence
    //    they are not in this map)

  VarsVectorTy pstack;
    // protection stack
    // contains variables passed to PROTECT
    //   interprets UNPROTECT(const)
    //   zeroed on unsupported unprotect  

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

void handleFreshVarsForNonTerminator(Instruction *in, CalledModuleTy *cm, SEXPGuardsTy *sexpGuards,
  FreshVarsTy& freshVars, LineMessenger& msg, unsigned& refinableInfos, LiveVarsTy& liveVars);

void handleFreshVarsForTerminator(Instruction *in, FreshVarsTy& freshVars, LiveVarsTy& liveVars);

#endif
