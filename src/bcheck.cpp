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

const int MAX_DEPTH = 64;	// maximum supported protection stack depth
const int MAX_COUNT = 32;	// maximum supported protection counter value (before turning to differential)
const int MAX_STATES = 3000000;	// maximum number of states visited per function


// protection counter (like "nprotect")
enum CountState {
  CS_NONE = 0,
  CS_EXACT,
  CS_DIFF // count is unused
          // savedDepth is inaccessible but keeps its value
          // depth is "how many protects on top of counter"
};

std::string cs_name(CountState cs) {
  switch(cs) {
    case CS_NONE: return "uninitialized (none)";
    case CS_EXACT: return "exact";
    case CS_DIFF: return "differential";
  }
}

struct StateTy : public StateWithGuardsTy, StateWithFreshVarsTy {
  int depth;		// number of pointers "currently" on the protection stack
  int savedDepth;	// number of pointers on the protection stack when saved to a local store variable (e.g. savestack = R_PPStackTop)
  int count;		// value of a local counter for the number of protected pointers (or -1 when not used) (e.g. nprotect)
  CountState countState;
  
  public:
    StateTy(BasicBlock *bb, int depth, int savedDepth, int count, CountState countState): 
      StateBaseTy(bb), StateWithGuardsTy(bb), StateWithFreshVarsTy(bb), depth(depth), savedDepth(savedDepth), count(count), countState(countState) {};

    StateTy(BasicBlock *bb, int depth, int savedDepth, int count, CountState countState, IntGuardsTy& intGuards, SEXPGuardsTy& sexpGuards, FreshVarsTy& freshVars):
      StateBaseTy(bb), StateWithGuardsTy(bb, intGuards, sexpGuards), StateWithFreshVarsTy(bb, freshVars),
      depth(depth), savedDepth(savedDepth), count(count), countState(countState) {};
      
    virtual StateTy* clone(BasicBlock *newBB) {
      return new StateTy(newBB, depth, savedDepth, count, countState, intGuards, sexpGuards, freshVars);
    }
    
    virtual bool add();

    void dump() {
      StateBaseTy::dump(VERBOSE_DUMP);
      StateWithGuardsTy::dump(VERBOSE_DUMP);
      StateWithFreshVarsTy::dump(VERBOSE_DUMP);

      errs() << "=== depth: " << depth << "\n";
      errs() << "=== savedDepth: " << savedDepth << "\n";
      errs() << "=== count: " << count << "\n";
      errs() << "=== countState: " << cs_name(countState) << "\n";

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
    hash_combine(res, t->depth);
    hash_combine(res, t->count);
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
    return lhs->bb == rhs->bb && lhs->depth == rhs->depth && lhs->savedDepth == rhs->savedDepth &&
      lhs->count == rhs->count && lhs->countState == rhs->countState && 
      lhs->intGuards == rhs->intGuards && lhs->sexpGuards == rhs->sexpGuards &&
      lhs->freshVars == rhs->freshVars;
  }
};

typedef std::stack<StateTy*> WorkListTy;
typedef std::unordered_set<StateTy*, StateTy_hash, StateTy_equal> DoneSetTy;


// -----------------------------------

struct GlobalsTy {
  Function *protectFunction, *protectWithIndexFunction, *unprotectFunction, *unprotectPtrFunction;
  GlobalVariable *ppStackTopVariable;
  
  GlobalVariable *nilVariable;
  Function *isNullFunction;
  
  public:
    GlobalsTy(Module *m) {
    
      protectFunction = getSpecialFunction(m, "Rf_protect");
      protectWithIndexFunction = getSpecialFunction(m, "R_ProtectWithIndex");
      unprotectFunction = getSpecialFunction(m, "Rf_unprotect");
      unprotectPtrFunction = getSpecialFunction(m, "Rf_unprotect_ptr");
      ppStackTopVariable = getSpecialVariable(m, "R_PPStackTop");
      
      nilVariable = getSpecialVariable(m, "R_NilValue");
      isNullFunction = getSpecialFunction(m, "Rf_isNull");
    }
  
  private:
    Function *getSpecialFunction(Module *m, std::string name) {
      Function *f = m->getFunction(name);
      if (!f) {
        errs() << "  Function " << name << " not found in module (won't check its use).\n";
      }
      return f;
    }
    
    GlobalVariable *getSpecialVariable(Module *m, std::string name) {
      GlobalVariable *v = m->getGlobalVariable(name, true);
      if (!v) {
        errs() << "  Variable " << name << " not found in module (won't check its use).\n";
      }
      return v;
    }
};


// -------------------------------- identifying special local variables (helper functions)  -----------------------------------


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
      StateTy* initState = new StateTy(&fun->getEntryBlock(), 0, -1, -1, CS_NONE);
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
      
      AllocaInst* counterVar = NULL;
      
      // process a single basic block
      for(BasicBlock::iterator in = s.bb->begin(), ine = s.bb->end(); in != ine; ++in) {
        msg.trace("visiting", in);
        handleFreshVarsForNonTerminator(in, possibleAllocators, allocatingFunctions, s, msg, refinableInfos);
        
        CallSite cs(cast<Value>(in));
        if (cs) {
          // invoke or call
          const Function* targetFunc = cs.getCalledFunction();
          if (!targetFunc) continue;
          
          if (targetFunc == gl.protectFunction || targetFunc == gl.protectWithIndexFunction) { // PROTECT(x)
            s.depth++;
            msg.debug("protect call", in);
            continue;
          }
          if (targetFunc == gl.unprotectFunction) {
            Value* unprotectValue = cs.getArgument(0);
            if (ConstantInt::classof(unprotectValue)) { // e.g. UNPROTECT(3)
              uint64_t arg = (cast<ConstantInt>(unprotectValue))->getZExtValue();
              s.depth -= (int) arg;
              msg.debug("unprotect call using constant", in);              
              if (s.countState != CS_DIFF && s.depth < 0) {
                msg.info("has negative depth", in);
                refinableInfos++;
                goto abort_from_function;
              }
              continue;
            }
            if (LoadInst::classof(unprotectValue)) { // e.g. UNPROTECT(numProtects)
              Value *varValue = const_cast<Value*>(cast<LoadInst>(unprotectValue)->getPointerOperand());
              if (AllocaInst::classof(varValue)) {
                AllocaInst* var = cast<AllocaInst>(varValue);
                if (!isProtectionCounterVariable(var, gl.unprotectFunction, counterVarsCache)) {
                  msg.info("has an unsupported form of unprotect with a variable (results will be incorrect)", in);
                  continue;
                }
                if (!counterVar) {
                  counterVar = var;
                } else if (counterVar != var) {
                  msg.info("has an unsupported form of unprotect with a variable - multiple counter variables (results will be incorrect)", in);
                  continue;
                }
                if (s.countState == CS_NONE) {
                  msg.info("passes uninitialized counter of protects in a call to unprotect", in);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                  continue;
                }
                if (s.countState == CS_EXACT) {
                  s.depth -= s.count;
                  msg.debug("unprotect call using counter in exact state", in);                
                  if (s.depth < 0) {
                    msg.info("has negative depth", in);
                    refinableInfos++;
                    goto abort_from_function;
                  }
                  continue;
                }
                // countState == CS_DIFF
                assert(s.countState == CS_DIFF);
                msg.debug("unprotect call using counter in diff state", in);
                s.countState = CS_NONE;
                // depth keeps its value - it now becomes exact depth again
                if (s.depth < 0) {
                  msg.info("has negative depth after UNPROTECT(<counter>)", in);
                  refinableInfos++;
                  goto abort_from_function;
                }
                continue;
              }
            }
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
                      s.depth -= (int) arg;
                      msg.debug("unprotect call using constant in conditional expression on integer guard", in);              
                      if (s.countState != CS_DIFF && s.depth < 0) {
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
            msg.info("has unsupported form of unprotect", in);
            continue;
          }
          
          if (targetFunc == gl.unprotectPtrFunction) {  // UNPROTECT_PTR(x)
            msg.debug("unprotect_ptr call", in);
            s.depth--;
            if (s.countState != CS_DIFF && s.depth < 0) {
                msg.info("has negative depth", in);
                refinableInfos++;
                goto abort_from_function;
              }
            continue;
          }
        } /* not invoke or call */
        if (LoadInst::classof(in)) {
          LoadInst *li = cast<LoadInst>(in);
          if (li->getPointerOperand() == gl.ppStackTopVariable) { // savestack = R_PPStackTop
            if (li->hasOneUse()) {
              User* user = li->user_back();
              if (StoreInst::classof(user)) {
                StoreInst* topStoreInst = cast<StoreInst>(user);
                if (AllocaInst::classof(topStoreInst->getPointerOperand())) {
                  AllocaInst* topStore = cast<AllocaInst>(topStoreInst->getPointerOperand());
                  if (isProtectionStackTopSaveVariable(topStore, gl.ppStackTopVariable, saveVarsCache)) {
                    // topStore is the alloca instruction for the local variable where R_PPStack is saved to
                    // e.g. %save = alloca i32, align 4
                    if (s.countState == CS_DIFF) {
                      msg.info("saving value of PPStackTop while in differential count state (results will be incorrect)", in);
                      continue;
                    }
                    s.savedDepth = s.depth;
                    msg.debug("saving value of PPStackTop", in);
                    continue;
                  }
                }
              }
            }
          }
          continue;
        }
        if (StoreInst::classof(in)) {
          Value* storePointerOp = cast<StoreInst>(in)->getPointerOperand();
          Value* storeValueOp = cast<StoreInst>(in)->getValueOperand();

          if (storePointerOp == gl.ppStackTopVariable) { // R_PPStackTop = savestack
            if (LoadInst::classof(storeValueOp)) {          
              Value *varValue = cast<LoadInst>(storeValueOp)->getPointerOperand();
              if (AllocaInst::classof(varValue) && 
                isProtectionStackTopSaveVariable(cast<AllocaInst>(varValue), gl.ppStackTopVariable, saveVarsCache)) {

                if (s.countState == CS_DIFF) {
                  msg.info("restoring value of PPStackTop while in differential count state (results will be incorrect)", in);
                  continue;
                }
                msg.debug("restoring value of PPStackTop", in);
                if (s.savedDepth < 0) {
                  msg.info("restores PPStackTop from uninitialized local variable", in);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                } else {
                  s.depth = s.savedDepth;
                }
                continue;
              }
            }
            msg.info("manipulates PPStackTop directly (results will be incorrect)", in);
            continue;  
          }
          if (AllocaInst::classof(storePointerOp) && 
            isProtectionCounterVariable(cast<AllocaInst>(storePointerOp), gl.unprotectFunction, counterVarsCache)) { // nprotect = ... 
              
            AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
            if (!counterVar) {
              counterVar = storePointerVar;
            } else if (counterVar != storePointerVar) {
              msg.info("uses multiple pointer protection counters (results will be incorrect)", in);
              continue;
            }
            if (ConstantInt::classof(storeValueOp)) {
              // nprotect = 3
              if (s.countState == CS_DIFF) {
                msg.info("setting counter value while in differential mode (forgetting protects)?", in);
                refinableInfos++;
                if (restartable) goto abort_from_function;
                continue;
              }
              int64_t arg = (cast<ConstantInt>(storeValueOp))->getSExtValue();
              s.count = arg;
              s.countState = CS_EXACT;
              msg.debug("setting counter to a constant", in);              
              if (s.count < 0) {
                msg.info("protection counter set to a negative value", in);
              }
              continue;
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
        
                if (nonConstOp && LoadInst::classof(nonConstOp) && cast<LoadInst>(nonConstOp)->getPointerOperand() == counterVar &&
                  constOp && ConstantInt::classof(constOp)) {
                  
                  if (s.countState == CS_NONE) {
                    msg.info("adds a constant to an uninitialized counter variable", in);
                    refinableInfos++;
                    if (restartable) goto abort_from_function;
                    continue;
                  }
                  int64_t arg = (cast<ConstantInt>(constOp))->getSExtValue();
                  msg.debug("adding a constant to counter", in);
                  if (s.countState == CS_EXACT) {
                    s.count += arg;
                    if (s.count < 0) {
                      msg.info("protection counter went negative after add", in);
                      refinableInfos++;
                      if (restartable) goto abort_from_function;
                    }
                    continue;
                  }
                  // countState == CS_DIFF
                  assert(s.countState == CS_DIFF);
                  s.depth -= arg; // fewer protects on top of counter than before
                  continue;
                }
              }
            }
            msg.info("unsupported use of protection counter (internal error?)", in);
            continue;
          }
          if (intGuardsEnabled && handleStoreToIntGuard(cast<StoreInst>(in), intGuardVarsCache, s.intGuards, msg)) {
            continue;
          }
          if (sexpGuardsEnabled &&
            handleStoreToSEXPGuard(cast<StoreInst>(in), sexpGuardVarsCache, s.sexpGuards,
              gl.nilVariable, gl.isNullFunction, msg, possibleAllocators, USE_ALLOCATOR_DETECTION)) {
          
            continue;  
          }
          continue;
        }
      }
      
      TerminatorInst *t = s.bb->getTerminator();
      if (ReturnInst::classof(t)) {
        if (s.countState == CS_DIFF || s.depth != 0) {
          msg.info("has possible protection stack imbalance", t);
          refinableInfos++;
          if (restartable) goto abort_from_function;
        }
        continue;
      }
      
      if (s.count > MAX_COUNT) {
        assert(s.countState == CS_EXACT);
        s.countState = CS_DIFF;
        s.depth -= s.count;
        s.count = -1;
      }
      
      if (s.depth > MAX_DEPTH) {
        msg.info("has too high protection stack depth", t);
        refinableInfos++;
        if (restartable) goto abort_from_function;
        continue;
      }
      
      if (s.countState != CS_DIFF && s.depth < 0) {
        if (restartable) goto abort_from_function;
        continue; 
        // do not propagate negative depth to successors
        // can't do this for count, because -1 means count not initialized
      }
      
      if (BranchInst::classof(t)) {
        // (be smarter when adding successors)
        
        BranchInst* br = cast<BranchInst>(t);
        if (br->isConditional() && CmpInst::classof(br->getCondition())) {
          CmpInst* ci = cast<CmpInst>(br->getCondition());
          
          // if (x == y) ... [comparison of two variables]
          if (sexpGuardsEnabled &&
            handleBranchOnSEXPGuard(br, sexpGuardVarsCache, s, gl.nilVariable, gl.isNullFunction, msg)) {
            
            continue;
          }
          
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
                if (!counterVar) {
                  counterVar = var;
                } else if (counterVar != var) {
                  msg.info("uses multiple pointer protection counters (results will be incorrect)", t);
                  continue;
                }
                if (s.countState == CS_NONE) {
                  msg.info("branches based on an uninitialized value of the protection counter variable", t);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                  continue;
                }
                if (s.countState == CS_EXACT) {
                  // we can unfold the branch with general body, and with comparisons against nonzero
                  // as we know the exact value of the counter
                  //
                  // if (nprotect) { .... }
                  
                  Constant *knownLhs = ConstantInt::getSigned(counterVar->getAllocatedType(), s.count);
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
                assert(s.countState == CS_DIFF);
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
                    s.countState = CS_NONE;
                    if (s.depth < 0) {
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
              if (intGuardsEnabled && handleBranchOnIntGuard(br, intGuardVarsCache, s, msg)) {
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
