/*
  Check protection stack balance for individual functions
  (and look for some other pointer protection bugs).

  Note that some functions have protection imbalance by design, most notable
  functions that manipulate the pointer protection stack and functions that
  are part of the parsers.

  The checking is somewhat path-sensitive and this sensitivity is adaptive.
  It increases when errors are found to validate they are not false alarms.

  The tool also looks for hints that there is an unprotected pointer while
  calling into a function that may allocate.  This is approximate only and
  has a lot of false alarms.
*/

#include <map>
#include <set>
#include <stack>
#include <unordered_set>
#include <unordered_map>

#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Constants.h>
#include <llvm/Analysis/CFG.h>
#include <llvm/Analysis/CallGraph.h>

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "errors.h"
#include "allocators.h"
#include "balance.h"
#include "freshvars.h"
#include "guards.h"
#include "linemsg.h"

using namespace llvm;

const bool DEBUG = false;
const bool TRACE = false;

const bool DUMP_STATES = false;
const std::string DUMP_STATES_FUNCTION = "modelmatrix"; // only dump states in this function
const bool ONLY_FUNCTION = true; // only check one function (named ONLY_FUNCTION_NAME)
const std::string ONLY_FUNCTION_NAME = "modelmatrix";
const bool VERBOSE_DUMP = false;

const bool PROGRESS_MARKS = true;
const unsigned PROGRESS_STEP = 1000;

const bool USE_ALLOCATOR_DETECTION = true;
  // use allocator detection to set SEXP guard variables to non-nill on allocation
  // this is optional, because it is not correct
  //   a function that would sometimes return a non-nill pointer, but at
  //   other times nil, will still be detected as an allocator [it would
  //   have been better to have a specific analysis for nullability]

// -------------------------
const bool UNIQUE_MSG = true;
  // Do not write more than one identical messages per source line of code. 
  // This should be enable unless debugging.  When enabled, messages are
  // delayed until the next function, possibly even dropped in case of some
  // kind of adaptive checking.

bool EXCLUDE_PROTECTION_FUNCTIONS = true;
  // currently this is set below to true for the case when checking modules
  // if set to true, functions like protect, unprotect are not being checked (because they indeed cause imbalance)


// -------------------------------- basic block state -----------------------------------

const int MAX_STATES = 3000000;        // maximum number of states visited per function

struct StateTy : public StateWithGuardsTy, StateWithFreshVarsTy, StateWithBalanceTy {
  
  public:
    StateTy(BasicBlock *bb): 
      StateBaseTy(bb), StateWithGuardsTy(bb), StateWithFreshVarsTy(bb), StateWithBalanceTy(bb) {};

    StateTy(BasicBlock *bb, BalanceStateTy& balance, IntGuardsTy& intGuards, SEXPGuardsTy& sexpGuards, FreshVarsTy& freshVars):
      StateBaseTy(bb), StateWithGuardsTy(bb, intGuards, sexpGuards), StateWithFreshVarsTy(bb, freshVars), StateWithBalanceTy(bb, balance) {};
      
    virtual StateTy* clone(BasicBlock *newBB) {
      return new StateTy(newBB, balance, intGuards, sexpGuards, freshVars);
    }
    
    virtual bool add();

    void dump() {
      StateBaseTy::dump(VERBOSE_DUMP);
      StateWithGuardsTy::dump(VERBOSE_DUMP);
      StateWithFreshVarsTy::dump(VERBOSE_DUMP);
      StateWithBalanceTy::dump(VERBOSE_DUMP);
      errs() << " ######################            ######################\n";
    }

};

struct StateTy_hash {
  size_t operator()(const StateTy* t) const {

    size_t res = 0;
    hash_combine(res, t->bb);
    hash_combine(res, t->balance.depth);
    hash_combine(res, t->balance.count);
    hash_combine(res, t->intGuards.size());
    for(IntGuardsTy::const_iterator gi = t->intGuards.begin(), ge = t->intGuards.end(); gi != ge; *gi++) {
      hash_combine(res, (int) gi->second);
    } // ordered map
    hash_combine(res, t->sexpGuards.size());
    for(SEXPGuardsTy::const_iterator gi = t->sexpGuards.begin(), ge = t->sexpGuards.end(); gi != ge; *gi++) {
      hash_combine(res, (int) gi->second);
    } // ordered map
    hash_combine(res, t->freshVars.vars.size());
    // do not hash the content of freshVars.vars (it doesn't pay off and currently the set is unordered)
    hash_combine(res, t->freshVars.condMsgs.size());
    // condMsgs is unordered
    return res;
  }
};

struct StateTy_equal {
  bool operator() (const StateTy* lhs, const StateTy* rhs) const {

    if (lhs == rhs) {
      return true;
    }
    return lhs->bb == rhs->bb && 
      lhs->balance.depth == rhs->balance.depth && lhs->balance.savedDepth == rhs->balance.savedDepth && lhs->balance.count == rhs->balance.count &&
      lhs->balance.countState == rhs->balance.countState && lhs->balance.counterVar == rhs->balance.counterVar &&
      lhs->intGuards == rhs->intGuards && lhs->sexpGuards == rhs->sexpGuards &&
      lhs->freshVars.vars == rhs->freshVars.vars && lhs->freshVars.condMsgs == rhs->freshVars.condMsgs;
  }
};

typedef std::stack<StateTy*> WorkListTy;
typedef std::unordered_set<StateTy*, StateTy_hash, StateTy_equal> DoneSetTy;

// ------------- helper functions --------------

DoneSetTy doneSet;
WorkListTy workList;   

bool StateTy::add() {
  auto sinsert = doneSet.insert(this);
  if (sinsert.second) {
    if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == bb->getParent()->getName())) {
      errs() << " -- dumping a new state being added -- \n";
      dump();
    }
    workList.push(this);
    return true;
  } else {
    delete this; // NOTE: state suicide
    return false;
  }
}

void clearStates() {
  // clear the worklist and the doneset
  for(DoneSetTy::iterator ds = doneSet.begin(), de = doneSet.end(); ds != de; ++ds) {
    StateTy *old = *ds;
    delete old;
  }
  doneSet.clear();
  WorkListTy empty;
  std::swap(workList, empty);
  // all elements in worklist are also in doneset, so no need to call destructors
}

void handleUnprotectWithIntGuard(Instruction *in, StateTy& s, GlobalsTy& g, VarBoolCacheTy& intGuardVarsCache, LineMessenger& msg, unsigned& refinableInfos) { 
  // UNPROTECT(intguard ? 3 : 4)
  
  CallSite cs(cast<Value>(in));
  if (!cs) {
    return;
  }
  const Function* targetFunc = cs.getCalledFunction();
  if (!targetFunc || targetFunc != g.unprotectFunction) return;
          
  Value* unprotectValue = cs.getArgument(0);
  if (!SelectInst::classof(unprotectValue)) {
    return;
  } 
  
  SelectInst *si = cast<SelectInst>(unprotectValue);
              
  if (!CmpInst::classof(si->getCondition()) || !ConstantInt::classof(si->getTrueValue()) || !ConstantInt::classof(si->getFalseValue())) {
    return;
  }
                
  CmpInst *ci = cast<CmpInst>(si->getCondition());
  if (!ci->isEquality()) {
    return;
  }
  
  LoadInst *guardOp;
  ConstantInt *constOp;
  
  if (LoadInst::classof(ci->getOperand(0)) && ConstantInt::classof(ci->getOperand(1))) {
    guardOp = cast<LoadInst>(ci->getOperand(0));
    constOp = cast<ConstantInt>(ci->getOperand(1));
  } else if (ConstantInt::classof(ci->getOperand(0)) && LoadInst::classof(ci->getOperand(1))) {
    constOp = cast<ConstantInt>(ci->getOperand(0));
    guardOp = cast<LoadInst>(ci->getOperand(1));
  } else {
    return;
  }
  
  if (!constOp->isZero()) {
    return;
  }
  
  Value *guardValue = guardOp->getPointerOperand();
  if (!AllocaInst::classof(guardValue) || !isIntegerGuardVariable(cast<AllocaInst>(guardValue), intGuardVarsCache)) {
    return;
  }
                  
  IntGuardState gs = getIntGuardState(s.intGuards, cast<AllocaInst>(guardValue));
                    
  if (gs != IGS_UNKNOWN) {
    uint64_t arg; 
    if ( (gs == IGS_ZERO && ci->isTrueWhenEqual()) || (gs == IGS_NONZERO && ci->isFalseWhenEqual()) ) {
      arg = cast<ConstantInt>(si->getTrueValue())->getZExtValue();
    } else {
      arg = cast<ConstantInt>(si->getFalseValue())->getZExtValue();
    }
    s.balance.depth -= (int) arg;
    msg.debug("unprotect call using constant in conditional expression on integer guard", in);              
    if (s.balance.countState != CS_DIFF && s.balance.depth < 0) {
      msg.info("has negative depth", in);
      refinableInfos++;
    }
  }
}


// -------------------------------- main  -----------------------------------

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  EXCLUDE_PROTECTION_FUNCTIONS = (argc == 3); // exclude when checking modules
  GlobalsTy gl(m);
  LineMessenger msg(context, DEBUG, TRACE, UNIQUE_MSG);
  
  FunctionsSetTy errorFunctions;
  findErrorFunctions(m, errorFunctions);

  FunctionsSetTy possibleAllocators;
  findPossibleAllocators(m, possibleAllocators);

  FunctionsSetTy allocatingFunctions;
  findAllocatingFunctions(m, allocatingFunctions);

  unsigned nAnalyzedFunctions = 0;
  for(FunctionsOrderedSetTy::iterator FI = functionsOfInterest.begin(), FE = functionsOfInterest.end(); FI != FE; ++FI) {
    Function *fun = *FI;

    if (!fun) continue;
    if (!fun->size()) continue;
    
    if (EXCLUDE_PROTECTION_FUNCTIONS &&
      (fun == gl.protectFunction ||
      fun == gl.protectWithIndexFunction ||
      fun == gl.unprotectFunction ||
      fun == gl.unprotectPtrFunction)) {
      
      continue;
    }
    
    nAnalyzedFunctions++;
    
    BasicBlocksSetTy errorBasicBlocks;
    findErrorBasicBlocks(fun, errorFunctions, errorBasicBlocks);

    bool intGuardsEnabled = false;
    bool sexpGuardsEnabled = false;

    VarBoolCacheTy saveVarsCache;
    VarBoolCacheTy counterVarsCache;
    VarBoolCacheTy intGuardVarsCache;
    VarBoolCacheTy sexpGuardVarsCache;
    
    msg.newFunction(fun);

  retry_function:
  
    unsigned refinableInfos = 0;
    bool restartable = !intGuardsEnabled || !sexpGuardsEnabled;
    clearStates();
    {
      StateTy* initState = new StateTy(&fun->getEntryBlock());
      initState->add();
    }
    while(!workList.empty()) {
      StateTy s(*workList.top());
      workList.pop();
      
      if (ONLY_FUNCTION && ONLY_FUNCTION_NAME != fun->getName()) {
        continue;
      }
      if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
        msg.trace("going to work on this state:", s.bb->begin());
        s.dump();
      }
      
      if (errorBasicBlocks.find(s.bb) != errorBasicBlocks.end()) {
        msg.debug("ignoring basic block on error path", s.bb->begin());
        continue;
      }
      
      if (doneSet.size() > MAX_STATES) {
        msg.error("too many states (abstraction error?)", s.bb->begin());
        goto abort_from_function;
      }
      
      if (PROGRESS_MARKS) {
        if (doneSet.size() % PROGRESS_STEP == 0) {
          outs() << std::to_string(workList.size()) << "/" << std::to_string(doneSet.size()) << "\n";
        }
      }      
      
      // process a single basic block
      for(BasicBlock::iterator in = s.bb->begin(), ine = s.bb->end(); in != ine; ++in) {
        msg.trace("visiting", in);
   
        handleFreshVarsForNonTerminator(in, possibleAllocators, allocatingFunctions, s.freshVars, msg, refinableInfos);
        handleBalanceForNonTerminator(in, s.balance, gl, counterVarsCache, saveVarsCache, msg, refinableInfos);
 
        if (intGuardsEnabled) {
          handleIntGuardsForNonTerminator(in, intGuardVarsCache, s.intGuards, msg);
          handleUnprotectWithIntGuard(in, s, gl, intGuardVarsCache, msg, refinableInfos);
        }
        if (sexpGuardsEnabled) {
          handleSEXPGuardsForNonTerminator(in, sexpGuardVarsCache, s.sexpGuards, gl, msg, possibleAllocators, USE_ALLOCATOR_DETECTION);
        }
      }
      
      TerminatorInst *t = s.bb->getTerminator();

      if (handleBalanceForTerminator(t, s, gl, counterVarsCache, msg, refinableInfos)) {
        // ignore successors in case important errors were already found, and hence further
        // errors found will just confuse the user
        continue;
      }

      if (sexpGuardsEnabled && handleSEXPGuardsForTerminator(t, sexpGuardVarsCache, s, gl, msg)) {
        continue;
      }

        // int guards have to be after balance, so that "if (nprotect) UNPROTECT(nprotect)"
        // is handled in preference of int guard
      if (intGuardsEnabled && handleIntGuardsForTerminator(t, intGuardVarsCache, s, msg)) {
        continue;
      }
      
      // add conservatively all cfg successors
      for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
        BasicBlock *succ = t->getSuccessor(i);
        {
          StateTy* state = s.clone(succ);
          if (state->add()) {
            msg.trace("added successor of", t);
          }
        }
      }
    }

    abort_from_function:
      if (restartable && refinableInfos>0) {
        // retry with more precise checking
        msg.clear();
        if (!intGuardsEnabled) {
          intGuardsEnabled = true;
        } else if (!sexpGuardsEnabled) {
          sexpGuardsEnabled = true;
        }
        goto retry_function;        
      }
  }
  msg.flush();
  clearStates();
  delete m;

  errs() << "Analyzed " << nAnalyzedFunctions << " functions\n";
  return 0;
}
