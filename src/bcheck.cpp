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
const std::string DUMP_STATES_FUNCTION = "do_eval"; // only dump states in this function
const bool ONLY_FUNCTION = false; // only check one function (named ONLY_FUNCTION_NAME)
const std::string ONLY_FUNCTION_NAME = "do_eval";
const bool VERBOSE_DUMP = false;

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

const int MAX_STATES = 3000000;	// maximum number of states visited per function

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

// from Boost
template <class T>
inline void hash_combine(std::size_t& seed, const T& v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

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
    hash_combine(res, t->freshVars.size());
    // do not hash the content of freshVars (it doesn't pay off and currently the set is unordered)
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
      lhs->freshVars == rhs->freshVars;
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
      
      // process a single basic block
      for(BasicBlock::iterator in = s.bb->begin(), ine = s.bb->end(); in != ine; ++in) {
        msg.trace("visiting", in);
   
        handleFreshVarsForNonTerminator(in, possibleAllocators, allocatingFunctions, s.freshVars, msg, refinableInfos);
        handleBalanceForNonTerminator(in, s.balance, gl, counterVarsCache, saveVarsCache, msg, refinableInfos);
 
        if (intGuardsEnabled) {
          handleIntGuardsForNonTerminator(in, intGuardVarsCache, s.intGuards, msg);
        }
        if (sexpGuardsEnabled) {
          handleSEXPGuardsForNonTerminator(in, sexpGuardVarsCache, s.sexpGuards, gl, msg, possibleAllocators, USE_ALLOCATOR_DETECTION);
        }
        
        CallSite cs(cast<Value>(in));
        if (cs) {
          // invoke or call
          const Function* targetFunc = cs.getCalledFunction();
          if (!targetFunc) continue;
          
          if (targetFunc == gl.unprotectFunction) {
            Value* unprotectValue = cs.getArgument(0);
            if (intGuardsEnabled && SelectInst::classof(unprotectValue)) { // UNPROTECT(intguard ? 3 : 4)
              SelectInst *si = cast<SelectInst>(unprotectValue);
              
              if (CmpInst::classof(si->getCondition()) && 
                ConstantInt::classof(si->getTrueValue()) && ConstantInt::classof(si->getFalseValue())) {
                
                CmpInst *ci = cast<CmpInst>(si->getCondition());
                if (Constant::classof(ci->getOperand(0))) {
                  ci->swapOperands();
                }
                if (LoadInst::classof(ci->getOperand(0)) && ConstantInt::classof(ci->getOperand(1)) &&
                  cast<ConstantInt>(ci->getOperand(1))->isZero() && ci->isEquality()) {
                  Value *guardValue = cast<LoadInst>(ci->getOperand(0))->getPointerOperand();
                  
                  if (AllocaInst::classof(guardValue) && isIntegerGuardVariable(cast<AllocaInst>(guardValue), intGuardVarsCache)) {
                    IntGuardState g = getIntGuardState(s.intGuards, cast<AllocaInst>(guardValue));
                    
                    if (g != IGS_UNKNOWN) {
                      uint64_t arg; 
                      if ( (g == IGS_ZERO && ci->isTrueWhenEqual()) || (g == IGS_NONZERO && ci->isFalseWhenEqual()) ) {
                        arg = cast<ConstantInt>(si->getTrueValue())->getZExtValue();
                      } else {
                        arg = cast<ConstantInt>(si->getFalseValue())->getZExtValue();
                      }
                      s.balance.depth -= (int) arg;
                      msg.debug("unprotect call using constant in conditional expression on integer guard", in);              
                      if (s.balance.countState != CS_DIFF && s.balance.depth < 0) {
                        msg.info("has negative depth", in);
                        refinableInfos++;
                        goto abort_from_function;
                      }
                      continue;                      
                    }
                  }
                }
              }
            }
          }
        }
      }
      
      TerminatorInst *t = s.bb->getTerminator();
      if (ReturnInst::classof(t)) {
        if (s.balance.countState == CS_DIFF || s.balance.depth != 0) {
          msg.info("has possible protection stack imbalance", t);
          refinableInfos++;
          if (restartable) goto abort_from_function;
        }
        continue;
      }
      
      if (s.balance.count > MAX_COUNT) { // turn the counter to differential state
        assert(s.balance.countState == CS_EXACT);
        s.balance.countState = CS_DIFF;
        s.balance.depth -= s.balance.count;
        s.balance.count = -1;
      }
      
      if (s.balance.depth > MAX_DEPTH) {
        msg.info("has too high protection stack depth", t);
        refinableInfos++;
        if (restartable) goto abort_from_function;
        continue;
      }
      
      if (s.balance.countState != CS_DIFF && s.balance.depth < 0) {
        if (restartable) goto abort_from_function;
        continue; 
        // do not propagate negative depth to successors
        // can't do this for count, because -1 means count not initialized
      }
      
      if (sexpGuardsEnabled && handleSEXPGuardsForTerminator(t, sexpGuardVarsCache, s, gl, msg)) {
        continue;
      }

      if (BranchInst::classof(t)) {
        // (be smarter when adding successors)
        
        BranchInst* br = cast<BranchInst>(t);
        if (br->isConditional() && CmpInst::classof(br->getCondition())) {
          CmpInst* ci = cast<CmpInst>(br->getCondition());
          
          // if (x == y) ... [comparison of two variables]
          
          // comparison with constant
          if (Constant::classof(ci->getOperand(0)) && LoadInst::classof(ci->getOperand(1))) {
            ci->swapOperands(); // have the variable first
          }
          
          if (LoadInst::classof(ci->getOperand(0)) && Constant::classof(ci->getOperand(1))) {
            LoadInst *li = cast<LoadInst>(ci->getOperand(0));
            
            if (AllocaInst::classof(li->getPointerOperand())) {
              AllocaInst *var = cast<AllocaInst>(li->getPointerOperand());

              // if (nprotect) UNPROTECT(nprotect)
              if (isProtectionCounterVariable(var, gl.unprotectFunction, counterVarsCache)) {
                if (!s.balance.counterVar) {
                  s.balance.counterVar = var;
                } else if (s.balance.counterVar != var) {
                  msg.info("uses multiple pointer protection counters (results will be incorrect)", t);
                  continue;
                }
                if (s.balance.countState == CS_NONE) {
                  msg.info("branches based on an uninitialized value of the protection counter variable", t);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                  continue;
                }
                if (s.balance.countState == CS_EXACT) {
                  // we can unfold the branch with general body, and with comparisons against nonzero
                  // as we know the exact value of the counter
                  //
                  // if (nprotect) { .... }
                  
                  Constant *knownLhs = ConstantInt::getSigned(s.balance.counterVar->getAllocatedType(), s.balance.count);
                  Constant *res = ConstantExpr::getCompare(ci->getPredicate(), knownLhs, cast<Constant>(ci->getOperand(1)));
                  assert(ConstantInt::classof(res));
                
                  // add only the relevant successor
                  msg.debug("folding out branch on counter value", t);                
                  BasicBlock *succ;
                  if (!res->isZeroValue()) {
                    succ = br->getSuccessor(0);
                  } else {
                    succ = br->getSuccessor(1);
                  }
                  {
                    StateTy *state = s.clone(succ);
                    if (state->add()) {
                      msg.trace("added folded successor of", t);
                    }
                  }
                  continue;
                }
                // s.countState == CS_DIFF
                assert(s.balance.countState == CS_DIFF);
                // we don't know if nprotect is zero
                // but if the expression is just "if (nprotect) UNPROTECT(nprotect)", we can
                //   treat it as "UNPROTECT(nprotect)", because UNPROTECT(0) does nothing
                if (ci->isEquality() && ConstantInt::classof(ci->getOperand(1))) {
                  ConstantInt *constOp = cast<ConstantInt>(ci->getOperand(1));
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
                  LoadInst *loadInst = NULL;
                  bool callsUnprotect = false;
                  bool mergesWithJoinSucc = false;
                  if (it != unprotectSucc->end() && LoadInst::classof(it)) {
                    loadInst = cast<LoadInst>(it);
                  }
                  ++it;
                  if (it != unprotectSucc->end()) {
                    CallSite cs(cast<Value>(it));
                    if (cs && cs.getCalledFunction() == gl.unprotectFunction && loadInst && cs.getArgument(0) == loadInst) {
                      callsUnprotect = true;
                    }
                  }
                  ++it;
                  if (it != unprotectSucc->end() && BranchInst::classof(it)) {
                    BranchInst *bi = cast<BranchInst>(it);
                    if (!bi->isConditional() && bi->getSuccessor(0) == joinSucc) {
                      mergesWithJoinSucc = true;
                    }
                  }
                  if (callsUnprotect && mergesWithJoinSucc && loadInst->getPointerOperand() == var) {
                    // if (np) { UNPROTECT(np) ...
                            
                    // FIXME: could there instead be returns in both branches?
                            
                    // interpret UNPROTECT(nprotect)
                    msg.debug("simplifying unprotect conditional on counter value (diff state)", t);                
                    s.balance.countState = CS_NONE;
                    if (s.balance.depth < 0) {
                      msg.info("has negative depth after UNPROTECT(<counter>)", t);
                      refinableInfos++;
                      goto abort_from_function;
                    }
                    // next process the code after the if
                    {
                      StateTy* state = s.clone(joinSucc);
                      if (state->add()) {
                        msg.trace("added folded successor (diff counter state) of", t);
                      }
                    }
                    continue;
                  }
                }
              }
              // int guard
              if (intGuardsEnabled && handleIntGuardsForTerminator(br, intGuardVarsCache, s, msg)) {
                continue;
              }
            }
          }
        }
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
        msg.clearForFunction(fun);
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
