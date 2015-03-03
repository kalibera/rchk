#ifndef RCHK_BALANCE_H
#define RCHK_BALANCE_H

#include "common.h"
#include "linemsg.h"
#include "state.h"

#include <map>

#include <llvm/IR/Instruction.h>

using namespace llvm;

const int MAX_DEPTH = 64;	// maximum supported protection stack depth
const int MAX_COUNT = 32;	// maximum supported protection counter value (before turning to differential)

// protection counter (like "nprotect")
enum CountState {
  CS_NONE = 0,
  CS_EXACT,
  CS_DIFF // count is unused
          // savedDepth is inaccessible but keeps its value
          // depth is "how many protects on top of counter"
};

std::string cs_name(CountState cs);

struct BalanceStateTy {
  int depth;		// number of pointers "currently" on the protection stack
  int savedDepth;	// number of pointers on the protection stack when saved to a local store variable (e.g. savestack = R_PPStackTop)
  int count;		// value of a local counter for the number of protected pointers (or -1 when not used) (e.g. nprotect)
  CountState countState;
  AllocaInst* counterVar;
  
  BalanceStateTy(int depth, int savedDepth, int count, CountState countState, AllocaInst* counterVar):
    depth(depth), savedDepth(savedDepth), count(count), countState(countState), counterVar(counterVar) {};
};

struct StateWithBalanceTy : virtual public StateBaseTy {
  BalanceStateTy balance;
  
  StateWithBalanceTy(BasicBlock *bb, BalanceStateTy& balance): StateBaseTy(bb), balance(balance) {};
  StateWithBalanceTy(BasicBlock *bb): StateBaseTy(bb), balance(0, -1, -1, CS_NONE, NULL) {};
  
  virtual StateWithBalanceTy* clone(BasicBlock *newBB) = 0;
  
  void dump(bool verbose);  
};

bool isProtectionStackTopSaveVariable(AllocaInst* var, GlobalVariable* ppStackTopVariable, VarBoolCacheTy& cache);
bool isProtectionCounterVariable(AllocaInst* var, Function* unprotectFunction, VarBoolCacheTy& cache);

void handleBalanceForNonTerminator(Instruction *in, BalanceStateTy& b, GlobalsTy& g, VarBoolCacheTy& counterVarsCache, VarBoolCacheTy& saveVarsCache,
    LineMessenger& msg, unsigned& refinableInfos);

bool handleBalanceForTerminator(TerminatorInst* t, StateWithBalanceTy& s, GlobalsTy& g, VarBoolCacheTy& counterVarsCache,
    LineMessenger& msg, unsigned& refinableInfos);

#endif
