
/*
  Check protection stack balance for individual functions.
  
  Note that some functions have protection imbalance by design, most notable
  functions that manipulate the pointer protection stack and functions that
  are part of the parsers.
*/

#include <map>
#include <set>
#include <stack>
#include <unordered_set>
#include <unordered_map>

#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/Analysis/CFG.h>
#include <llvm/Analysis/CallGraph.h>

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "errors.h"

using namespace llvm;

const bool DEBUG = false;
const bool TRACE = false;

const bool DUMP_STATES = false;
const std::string DUMP_STATES_FUNCTION = "Rf_substituteList"; // only dump states in this function
const bool VERBOSE_DUMP = false;

// -------------------------
const bool UNIQUE_MSG = true;
  // Do not write more than one identical messages per source line of code. 
  // This should be enable unless debugging.  When enabled, messages are
  // delayed until the next function, possibly even dropped in case of some
  // kind of adaptive checking.

// -------------------------------- basic block state -----------------------------------

const int MAX_DEPTH = 64;	// maximum supported protection stack depth
const int MAX_COUNT = 32;	// maximum supported protection counter value (before turning to differential)
const int MAX_STATES = 3000000;	// maximum number of states visited per function

// integer variable used as a guard
enum IntGuardState {
  IGS_ZERO = 0,
  IGS_NONZERO,
  IGS_UNKNOWN
};


std::string igs_name(IntGuardState gs) {
  switch(gs) {
    case IGS_ZERO: return "zero";
    case IGS_NONZERO: return "nonzero";
    case IGS_UNKNOWN: return "unknown";
  }
}

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

// SEXP - an "R pointer" used as a guard
enum SEXPGuardState {
  SGS_NIL = 0, // R_NilValue
  SGS_NONNIL,
  SGS_UNKNOWN
};

std::string sgs_name(SEXPGuardState sgs) {
  switch(sgs) {
    case SGS_NIL: return "nil (R_NilValue)";
    case SGS_NONNIL: return "non-nil (not R_NilValue)";
    case SGS_UNKNOWN: return "unknown";
  }
}

typedef std::map<AllocaInst*,IntGuardState> IntGuardsTy;
typedef std::map<AllocaInst*,SEXPGuardState> SEXPGuardsTy;

IntGuardState getIntGuardState(IntGuardsTy& intGuards, AllocaInst* var) {

  auto gsearch = intGuards.find(var);
  if (gsearch == intGuards.end()) {
    return IGS_UNKNOWN;
  } else {
    return gsearch->second;
  }
}

SEXPGuardState getSEXPGuardState(SEXPGuardsTy& sexpGuards, AllocaInst* var) {

  auto gsearch = sexpGuards.find(var);
  if (gsearch == sexpGuards.end()) {
    return SGS_UNKNOWN;
  } else {
    return gsearch->second;
  }
}

struct StateTy {
  BasicBlock *bb;
  int depth;		// number of pointers "currently" on the protection stack
  int savedDepth;	// number of pointers on the protection stack when saved to a local store variable (e.g. savestack = R_PPStackTop)
  int count;		// value of a local counter for the number of protected pointers (or -1 when not used) (e.g. nprotect)
  CountState countState;
  IntGuardsTy intGuards;
  SEXPGuardsTy sexpGuards;
  
  public:
    StateTy(BasicBlock *bb, int depth, int savedDepth, int count, CountState countState): 
      bb(bb), depth(depth), savedDepth(savedDepth), count(count), countState(countState), intGuards(), sexpGuards() {};

    StateTy(BasicBlock *bb, int depth, int savedDepth, int count, CountState countState, IntGuardsTy& intGuards, SEXPGuardsTy& sexpGuards): 
      bb(bb), depth(depth), savedDepth(savedDepth), count(count), countState(countState), intGuards(intGuards), sexpGuards(sexpGuards) {};
      
    void dump() {
      errs() << "\n ###################### STATE DUMP ######################\n";
      errs() << "=== Function: " << bb->getParent()->getName() << "\n";
      if (VERBOSE_DUMP) {
        errs() << "=== Basic block: \n" << *bb << "\n";
      }
      
      Instruction *in = bb->begin();
      DebugLoc debugLoc = in->getDebugLoc();
      DILocation loc(debugLoc.getScopeNode(bb->getContext()));  
      errs() << "=== Basic block src: " << loc.getDirectory() << "/" << loc.getFilename() << ":" << debugLoc.getLine() << "\n";

      errs() << "=== depth: " << depth << "\n";
      errs() << "=== savedDepth: " << savedDepth << "\n";
      errs() << "=== count: " << count << "\n";
      errs() << "=== countState: " << cs_name(countState) << "\n";
      errs() << "=== integer guards: \n";
      for(IntGuardsTy::iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; *gi++) {
        AllocaInst *i = gi->first;
        IntGuardState s = gi->second;
        errs() << "   " << i->getName() << " ";
        if (VERBOSE_DUMP) {
          errs() << *i << " ";
        }
        errs() << " state: " << igs_name(s) << "\n";
      }
      errs() << "=== sexp guards: \n";
      for(SEXPGuardsTy::iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; *gi++) {
        AllocaInst *i = gi->first;
        SEXPGuardState s = gi->second;
        errs() << "   " << i->getName() << " ";
        if (VERBOSE_DUMP) {
          errs() << *i << " ";
        }
        errs() << " state: " << sgs_name(s) << "\n";
      }
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

  size_t operator()(const StateTy& t) const {
    size_t res = 0;
    hash_combine(res, t.bb);
    hash_combine(res, t.depth);
    hash_combine(res, t.count);
    hash_combine(res, t.intGuards.size());
    for(IntGuardsTy::const_iterator gi = t.intGuards.begin(), ge = t.intGuards.end(); gi != ge; *gi++) {
      hash_combine(res, (int) gi->second);
    }
    hash_combine(res, t.sexpGuards.size());
    for(SEXPGuardsTy::const_iterator gi = t.sexpGuards.begin(), ge = t.sexpGuards.end(); gi != ge; *gi++) {
      hash_combine(res, (int) gi->second);
    }
    return res;
  }
};

struct StateTy_equal {
  bool operator() (const StateTy& lhs, const StateTy& rhs) const {
    return lhs.bb == rhs.bb && lhs.depth == rhs.depth && lhs.savedDepth == rhs.savedDepth &&
      lhs.count == rhs.count && lhs.countState == rhs.countState && 
      lhs.intGuards == rhs.intGuards && lhs.sexpGuards == rhs.sexpGuards;
  }
};

typedef std::stack<StateTy> WorkListTy;
typedef std::unordered_set<StateTy, StateTy_hash, StateTy_equal> DoneSetTy;

// -------------------------------- support for printing messages -----------------------------------

struct LineInfoTy {
  std::string kind;
  std::string message;
  std::string path;
  unsigned line;
  
  public:
  LineInfoTy(std::string kind, std::string message, std::string path, unsigned line): 
    kind(kind), message(message), path(path), line(line) {}
    
  void print() const {
    errs() << "  ";
    if (!kind.empty()) {
      errs()  << kind << ": ";
    }
    errs() << message << " " << path << ":" << line << "\n";
  }

};

struct LineInfoTy_compare {
  bool operator() (const LineInfoTy& lhs, const LineInfoTy& rhs) const {
    int cmp;
    cmp = lhs.path.compare(rhs.path);
    if (cmp) {
      return cmp < 0;
    }
    if (lhs.line != rhs.line) {
      return lhs.line < rhs.line;
    }
    cmp = lhs.message.compare(rhs.message);
    if (cmp) {
      return cmp < 0;
    }
    cmp = lhs.kind.compare(rhs.kind);
    return cmp < 0;
  }
};

typedef std::set<LineInfoTy, LineInfoTy_compare> LineBufferTy;
//typedef std::set<LineInfoTy> LineBufferTy;

Function *lineInfoLastFunction = NULL;
LineBufferTy lineBuffer;

void flushLineInfo() {
  if (lineInfoLastFunction != NULL && !lineBuffer.empty()) {
    errs() << "\nFunction " << lineInfoLastFunction->getName() << "\n";
    for(LineBufferTy::iterator liBuf = lineBuffer.begin(), liEbuf = lineBuffer.end(); liBuf != liEbuf; ++liBuf) {
      liBuf->print();
    }
    lineBuffer.clear();
  }
  lineInfoLastFunction = NULL;
}

void lineInfo(std::string kind, std::string message, Instruction *in, Function *func, LLVMContext& context) {

  if (kind == "DEBUG" && !DEBUG) {
    return;
  }
  if (kind == "TRACE" && !TRACE) {
    return;
  }
  DebugLoc debugLoc = in->getDebugLoc();
  DILocation loc(debugLoc.getScopeNode(context));  
  LineInfoTy li(kind, message, (loc.getDirectory() + "/" + loc.getFilename()).str(), debugLoc.getLine());

  if (!UNIQUE_MSG) {
    if (lineInfoLastFunction != func) {
      errs() << "\nFunction " << func->getName() << "\n";
      lineInfoLastFunction = func;
    }
    li.print();
  } else {
    if (lineInfoLastFunction != func) {
      flushLineInfo();
      lineInfoLastFunction = func;
    }
    lineBuffer.insert(li);
  }
}

void lineInfoClearForFunction(Function *func) {
  if (!UNIQUE_MSG) {
    if (lineInfoLastFunction == func) {
      errs() << " ---- restarting checking for function " << func->getName() << " (previous messages for it to be ignored) ----\n";
    }
    return;
  }
  if (lineInfoLastFunction == func) {
    lineBuffer.clear();  
  }
}

void line_debug(std::string msg, Instruction *in, Function *func, LLVMContext& context) {
  if (DEBUG) {
    lineInfo("DEBUG", msg, in, func, context);
  }
}

void line_trace(std::string msg, Instruction *in, Function *func, LLVMContext& context) {
  if (TRACE) {
    lineInfo("TRACE", msg, in, func, context);
  }
}

void line_info(std::string msg, Instruction *in, Function *func, LLVMContext& context) {
  lineInfo(DEBUG ? "INFO " : "", msg, in, func, context);
}

void line_error(std::string msg, Instruction *in, Function *func, LLVMContext& context) {
  lineInfo("ERROR", msg, in, func, context);
}

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
      GlobalVariable *v  = m->getGlobalVariable(name, true);
      if (!v) {
        errs() << "  Variable " << name << " not found in module (won't check its use).\n";
      }
      return v;
    }
};


// -------------------------------- identifying special local variables (helper functions)  -----------------------------------

struct VarCacheTy_hash {

  size_t operator()(const AllocaInst* i) const {
    return (size_t) i;
  }
};

typedef std::unordered_map<AllocaInst*,bool,VarCacheTy_hash> VarCacheTy;

// protection stack top "save variable" is a local variable
//   - which can be assigned the value of R_PPStackTop (typically at start of function)
//   - which can be assigned to R_PPStackTop (typically at end of function)
//   - it must have at least one load/store of R_PPStackTop

bool isProtectionStackTopSaveVariable(AllocaInst* var, GlobalVariable* ppStackTopVariable, VarCacheTy& cache) {

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

bool isProtectionCounterVariable(AllocaInst* var, Function* unprotectFunction, VarCacheTy& cache) {

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

// integer guard is a local variable
//   which is compared at least once against a constant zero, but never compared against anything else
//   which may be stored to and loaded from
//   which is not used for anything else (e.g. an address of it is not taken)
//
// there has to be either at least two comparisons using the guard, 
//   or there has to be one comparison and at least one assignment of a constant
//   [in other cases, we would gain nothing by tracking the guard]
//
// these heuristics are important because the keep the state space small(er)
bool isIntegerGuardVariable(AllocaInst* var) {

  if (!IntegerType::classof(var->getAllocatedType()) || var->isArrayAllocation()) {
    return false;
  }
  
  unsigned nComparisons = 0;
  unsigned nConstantAssignments = 0;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (!l->hasOneUse()) {
        continue;
      }
      User *uu = l->user_back();
      if (CmpInst::classof(uu)) {
        CmpInst *ci = cast<CmpInst>(uu);
        if (!ci->isEquality()) {
          continue;
        }
        if (Constant::classof(ci->getOperand(0))) {
          ci->swapOperands();
        }
        if (ConstantInt::classof(ci->getOperand(1))) {
          ConstantInt *constOp = cast<ConstantInt>(ci->getOperand(1));
          if (constOp->isZero()) {
            nComparisons++;
          } else {
            return false;
          }
        }
      }
      continue;
    }
    if (StoreInst::classof(u)) {
      Value *v = (cast<StoreInst>(u))->getValueOperand();
      if (ConstantInt::classof(v)) {
        // guard = TRUE;
        nConstantAssignments++;
      }
      continue;
    }
    // this can e.g. be a call (taking address of the variable, which we do not support)
    return false;
  } 
  return nComparisons >= 2 || (nComparisons == 1 && nConstantAssignments > 0);
}

bool isIntegerGuardVariable(AllocaInst* var, VarCacheTy& cache) {
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }

  bool res = isIntegerGuardVariable(var);
  
  cache.insert({var, res});
  return res;
}

bool isSEXP(AllocaInst* var) {
  if (!PointerType::classof(var->getAllocatedType()) || var->isArrayAllocation() /* need to check this? */) {
    return false;
  }
  Type *etype = (cast<PointerType>(var->getAllocatedType()))->getPointerElementType();
  if (!StructType::classof(etype)) {
    return false;
  }
  StructType *estr = cast<StructType>(etype);
  if (!estr->hasName() || estr->getName() != "struct.SEXPREC") {
    return false;
  }
  return true;
}

// SEXP guard is a local variable of type SEXP
//   which is compared at least once against R_NilValue
//   which may be stored to and loaded from
//   which is not used for anything else (e.g. an address of it is not taken)
//
// there has to be either at least two comparisons using the guard, 
//   or there has to be one comparison and 
//     either at least one assignment of a constant
//     or at least one copy of that guard into another variable
//   [in other cases, we would gain nothing by tracking the guard]
//
// these heuristics are important because the keep the state space small(er)

bool isSEXPGuardVariable(AllocaInst* var, GlobalVariable* nilVariable, Function* isNullFunction) {

  if (!isSEXP(var)) {
    return false;
  }
  unsigned nComparisons = 0;
  unsigned nNilAssignments = 0;
  unsigned nCopies = 0;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (LoadInst::classof(u)) {
      LoadInst *l = cast<LoadInst>(u);
      if (!l->hasOneUse()) {
        continue;
      }
      User *uu = l->user_back();
      if (CmpInst::classof(uu)) {
        CmpInst *ci = cast<CmpInst>(uu);
        if (!ci->isEquality()) {
          continue;
        }
        Value *other;
        if (ci->getOperand(0) == l) {
          other = ci->getOperand(1);
        } else {
          other = ci->getOperand(0);
        }
        if (LoadInst::classof(other)) {
          LoadInst *ol = cast<LoadInst>(other);
          if (ol->getPointerOperand() == nilVariable) {
            nComparisons++;
            continue;
          }
        }
        continue;
      }
      CallSite cs(cast<Value>(uu));
      if (cs && cs.getCalledFunction() == isNullFunction) {
        // isNull(guard);
        nComparisons++;
        continue;
      }      
      if (StoreInst::classof(uu)) {
        nCopies++;
        continue;
      }
      continue;
    }
    if (StoreInst::classof(u)) {
      Value *v = (cast<StoreInst>(u))->getValueOperand();
      if (LoadInst::classof(v)) {
        // guard = R_NilValue;
        LoadInst *l = cast<LoadInst>(v);
        if (l->getPointerOperand() == nilVariable) {
          nNilAssignments++;
        }
      }
      continue;
    }
    // this can e.g. be a call (taking address of the variable, which we do not support)
    return false;
  } 
  
  return nComparisons >= 2 || (nComparisons == 1 && nNilAssignments > 0) || (nComparisons == 1 && nCopies > 0);
}

bool isSEXPGuardVariable(AllocaInst* var, GlobalVariable* nilVariable, Function* isNullFunction, VarCacheTy& cache) {
  auto csearch = cache.find(var);
  if (csearch != cache.end()) {
    return csearch->second;
  }

  bool res = isSEXPGuardVariable(var, nilVariable, isNullFunction);
  
  cache.insert({var, res});
  return res;
}

// ------------- helper functions --------------

void addState(DoneSetTy& doneSet, WorkListTy& workList, StateTy& state) {
  auto sinsert = doneSet.insert(state);
  if (sinsert.second) {
    workList.push(state);
  }
}

// -------------------------------- main  -----------------------------------

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  GlobalsTy gl(m);
  
  FunctionsSetTy errorFunctions;
  findErrorFunctions(m, errorFunctions);

  unsigned nAnalyzedFunctions = 0;
  for(FunctionsOrderedSetTy::iterator FI = functionsOfInterest.begin(), FE = functionsOfInterest.end(); FI != FE; ++FI) {
    Function *fun = *FI;

    if (!fun) continue;
    if (!fun->size()) continue;
    
    nAnalyzedFunctions++;
    
    BasicBlocksSetTy errorBasicBlocks;
    findErrorBasicBlocks(fun, errorFunctions, errorBasicBlocks);

    bool intGuardsEnabled = false;
    bool sexpGuardsEnabled = false;

    VarCacheTy saveVarsCache;
    VarCacheTy counterVarsCache;
    VarCacheTy intGuardVarsCache;
    VarCacheTy sexpGuardVarsCache;
    
  retry_function:
  
    unsigned refinableInfos = 0;
    bool restartable = !intGuardsEnabled || !sexpGuardsEnabled;
    DoneSetTy doneSet;
    WorkListTy workList;   
    workList.push(StateTy(&fun->getEntryBlock(), 0, -1, -1, CS_NONE));
    
    while(!workList.empty()) {
      StateTy todo = workList.top();
      int depth = todo.depth;
      int savedDepth = todo.savedDepth;
      int count = todo.count;
      CountState countState = todo.countState;
      BasicBlock *bb = todo.bb;
      IntGuardsTy intGuards = todo.intGuards; // FIXME: could easily avoid copy?
      SEXPGuardsTy sexpGuards = todo.sexpGuards;
      
      if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
        line_trace("going to work on this state:", bb->begin(), fun, context);
        todo.dump();
      }
      workList.pop();
      
      if (errorBasicBlocks.find(bb) != errorBasicBlocks.end()) {
        line_debug("ignoring basic block on error path", bb->begin(), fun, context);
        continue;
      }
      
      if (doneSet.size() > MAX_STATES) {
        line_error("too many states (abstraction error?)", bb->begin(), fun, context);
        goto abort_from_function;
      }      
      
      AllocaInst* counterVar = NULL;
      
      // process a single basic block
      for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); in != ine; ++in) {
        line_trace("visiting", in, fun, context);
        CallSite cs(cast<Value>(in));
        if (cs) {
          // invoke or call
          const Function* targetFunc = cs.getCalledFunction();
          if (!targetFunc) continue;
          
          if (targetFunc == gl.protectFunction || targetFunc == gl.protectWithIndexFunction) { // PROTECT(x)
            depth++;
            line_debug("protect call", in, fun, context);
            continue;
          }

          if (targetFunc == gl.unprotectFunction) {
            Value* unprotectValue = cs.getArgument(0);
            if (ConstantInt::classof(unprotectValue)) { // e.g. UNPROTECT(3)
              uint64_t arg = (cast<ConstantInt>(unprotectValue))->getZExtValue();
              depth -= (int) arg;
              line_debug("unprotect call using constant", in, fun, context);              
              if (countState != CS_DIFF && depth < 0) {
                line_info("has negative depth", in, fun, context);
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
                  line_info("has an unsupported form of unprotect with a variable (results will be incorrect)", in, fun, context);
                  continue;
                }
                if (!counterVar) {
                  counterVar = var;
                } else if (counterVar != var) {
                  line_info("has an unsupported form of unprotect with a variable - multiple counter variables (results will be incorrect)", in, fun, context);
                  continue;
                }
                if (countState == CS_NONE) {
                  line_info("passes uninitialized counter of protects in a call to unprotect", in, fun, context);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                  continue;
                }
                if (countState == CS_EXACT) {
                  depth -= count;
                  line_debug("unprotect call using counter in exact state", in, fun, context);                
                  if (depth < 0) {
                    line_info("has negative depth", in, fun, context);
                    refinableInfos++;
                    goto abort_from_function;
                  }
                  continue;
                }
                // countState == CS_DIFF
                assert(countState == CS_DIFF);
                line_debug("unprotect call using counter in diff state", in, fun, context);
                countState = CS_NONE;
                // depth keeps its value - it now becomes exact depth again
                if (depth < 0) {
                  line_info("has negative depth after UNPROTECT(<counter>)", in, fun, context);
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
                    IntGuardState g = getIntGuardState(intGuards, cast<AllocaInst>(guardValue));
                    
                    if (g != IGS_UNKNOWN) {
                      uint64_t arg; 
                      if ( (g == IGS_ZERO && ci->isTrueWhenEqual()) || (g == IGS_NONZERO && ci->isFalseWhenEqual()) ) {
                        arg = cast<ConstantInt>(si->getTrueValue())->getZExtValue();
                      } else {
                        arg = cast<ConstantInt>(si->getFalseValue())->getZExtValue();
                      }
                      depth -= (int) arg;
                      line_debug("unprotect call using constant in conditional expression on integer guard", in, fun, context);              
                      if (countState != CS_DIFF && depth < 0) {
                        line_info("has negative depth", in, fun, context);
                        refinableInfos++;
                        goto abort_from_function;
                      }
                      continue;                      
                    }
                  }
                }
              }
            }
            line_info("has unsupported form of unprotect", in, fun, context);
            continue;
          }
          
          if (targetFunc == gl.unprotectPtrFunction) {  // UNPROTECT_PTR(x, idx)
            line_debug("unprotect_ptr call", in, fun, context);
            depth--;
            if (countState != CS_DIFF && depth < 0) {
                line_info("has negative depth", in, fun, context);
                refinableInfos++;
                goto abort_from_function;
              }
            continue;
          }
          continue;
        }
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
                    if (countState == CS_DIFF) {
                      line_info("saving value of PPStackTop while in differential count state (results will be incorrect)", in, fun, context);
                      continue;
                    }
                    savedDepth = depth;
                    line_debug("saving value of PPStackTop", in, fun, context);
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

                if (countState == CS_DIFF) {
                  line_info("restoring value of PPStackTop while in differential count state (results will be incorrect)", in, fun, context);
                  continue;
                }
                line_debug("restoring value of PPStackTop", in, fun, context);
                if (savedDepth < 0) {
                  line_info("restores PPStackTop from uninitialized local variable", in, fun, context);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                } else {
                  depth = savedDepth;
                }
                continue;
              }
            }
            line_info("manipulates PPStackTop directly (results will be incorrect)", in, fun, context);
            continue;  
          }
          if (AllocaInst::classof(storePointerOp) && 
            isProtectionCounterVariable(cast<AllocaInst>(storePointerOp), gl.unprotectFunction, counterVarsCache)) { // nprotect = ... 
              
            AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
            if (!counterVar) {
              counterVar = storePointerVar;
            } else if (counterVar != storePointerVar) {
              line_info("uses multiple pointer protection counters (results will be incorrect)", in, fun, context);
              continue;
            }
            if (ConstantInt::classof(storeValueOp)) {
              // nprotect = 3
              if (countState == CS_DIFF) {
                line_info("setting counter value while in differential mode (forgetting protects)?", in, fun, context);
                refinableInfos++;
                if (restartable) goto abort_from_function;
                continue;
              }
              int64_t arg = (cast<ConstantInt>(storeValueOp))->getSExtValue();
              count = arg;
              countState = CS_EXACT;
              line_debug("setting counter to a constant", in, fun, context);              
              if (count < 0) {
                line_info("protection counter set to a negative value", in, fun, context);
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
                  
                  if (countState == CS_NONE) {
                    line_info("adds a constant to an uninitialized counter variable", in, fun, context);
                    refinableInfos++;
                    if (restartable) goto abort_from_function;
                    continue;
                  }
                  int64_t arg = (cast<ConstantInt>(constOp))->getSExtValue();
                  line_debug("adding a constant to counter", in, fun, context);
                  if (countState == CS_EXACT) {
                    count += arg;
                    if (count < 0) {
                      line_info("protection counter went negative after add", in, fun, context);
                      refinableInfos++;
                      if (restartable) goto abort_from_function;
                    }
                    continue;
                  }
                  // countState == CS_DIFF
                  assert(countState == CS_DIFF);
                  depth -= arg; // fewer protects on top of counter than before
                  continue;
                }
              }
            }
            line_info("unsupported use of protection counter (internal error?)", in, fun, context);
            continue;
          }
          if (intGuardsEnabled && AllocaInst::classof(storePointerOp) && 
            isIntegerGuardVariable(cast<AllocaInst>(storePointerOp), intGuardVarsCache)) { // intguard = ...
              
            AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
            IntGuardState newState;
            if (ConstantInt::classof(storeValueOp)) {
              ConstantInt* constOp = cast<ConstantInt>(storeValueOp);
              if (constOp->isZero()) {
                newState = IGS_ZERO;
                line_debug("integer guard variable " + storePointerVar->getName().str() + " set to zero", in, fun, context);
              } else {
                newState = IGS_NONZERO;
                line_debug("integer guard variable " + storePointerVar->getName().str() + " set to nonzero", in, fun, context);
              }
            } else {
              // FIXME: could add support for intguarda = intguardb, if needed
              newState = IGS_UNKNOWN;
              line_debug("integer guard variable " + storePointerVar->getName().str() + " set to unknown", in, fun, context);
            }
            intGuards[storePointerVar] = newState;
            continue;
          }
          if (sexpGuardsEnabled && AllocaInst::classof(storePointerOp) && 
            isSEXPGuardVariable(cast<AllocaInst>(storePointerOp), gl.nilVariable, gl.isNullFunction, sexpGuardVarsCache)) { // sexpguard = ...
              
            AllocaInst* storePointerVar = cast<AllocaInst>(storePointerOp);
            SEXPGuardState newState = SGS_UNKNOWN;
            
            if (LoadInst::classof(storeValueOp)) {
              Value *src = cast<LoadInst>(storeValueOp)->getPointerOperand();
              if (src == gl.nilVariable) {
                newState = SGS_NIL;
                line_debug("sexp guard variable " + storePointerVar->getName().str() + " set to nil", in, fun, context);
              } else if (AllocaInst::classof(src) && 
                isSEXPGuardVariable(cast<AllocaInst>(src), gl.nilVariable, gl.isNullFunction, sexpGuardVarsCache)) {
                  
                newState = getSEXPGuardState(sexpGuards, cast<AllocaInst>(src));
                line_debug("sexp guard variable " + storePointerVar->getName().str() + " set to state of " +
                    cast<AllocaInst>(src)->getName().str() + ", which is " + sgs_name(newState), in, fun, context);
              } else {
                line_debug("sexp guard variable " + storePointerVar->getName().str() + " set to unknown (unsupported loadinst source)", in, fun, context);
              }
            } else {
              line_debug("sexp guard variable " + storePointerVar->getName().str() + " set to unknown", in, fun, context);
            }
            sexpGuards[storePointerVar] = newState;
            continue;            
          }
          continue;
        }
      }
      
      TerminatorInst *t = bb->getTerminator();
      if (ReturnInst::classof(t)) {
        if (countState == CS_DIFF || depth != 0) {
          line_info("has possible protection stack imbalance", t, fun, context);
          refinableInfos++;
          if (restartable) goto abort_from_function;
        }
        continue;
      }
      
      if (count > MAX_COUNT) {
        assert(countState == CS_EXACT);
        countState = CS_DIFF;
        depth -= count;
        count = -1;
      }
      
      if (depth > MAX_DEPTH) {
        line_info("has too high protection stack depth", t, fun, context);
        refinableInfos++;
        if (restartable) goto abort_from_function;
        continue;
      }
      
      if (countState != CS_DIFF && depth < 0) {
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
          if (sexpGuardsEnabled && ci->isEquality() && LoadInst::classof(ci->getOperand(0)) && LoadInst::classof(ci->getOperand(1))) {
            Value *lo = cast<LoadInst>(ci->getOperand(0))->getPointerOperand();
            Value *ro = cast<LoadInst>(ci->getOperand(1))->getPointerOperand();
            
            Value *guard = NULL;
            if (lo == gl.nilVariable) {
              guard = ro;
            } else {
              guard = lo;
            }
            
            if (guard && AllocaInst::classof(guard) && 
              isSEXPGuardVariable(cast<AllocaInst>(guard), gl.nilVariable, gl.isNullFunction, sexpGuardVarsCache)) {
              
              // if (x == R_NilValue) ...
              // if (x != R_NilValue) ...
              
              AllocaInst* var = cast<AllocaInst>(guard);
              SEXPGuardState g = getSEXPGuardState(sexpGuards, var);
              int succIndex = -1;

              if (g != SGS_UNKNOWN) {
                if (ci->isTrueWhenEqual()) {
                  // guard == R_NilValue
                  succIndex = (g == SGS_NIL) ? 0 : 1;
                } else {
                  // guard != R_NilValue
                  succIndex = (g == SGS_NIL) ? 1 : 0;
                }
              }
              if (DEBUG) {
                switch(succIndex) {
                  case -1:
                    line_debug("undecided branch on sexp guard variable " + var->getName().str(), t, fun, context);
                    break;
                  case 0:
                    line_debug("taking true branch on sexp guard variable " + var->getName().str(), t, fun, context);
                    break;
                  case 1:
                    line_debug("taking false branch on sexp guard variable " + var->getName().str(), t, fun, context);
                    break;
                }
              }
              if (succIndex != 1) {
                // true branch is possible
                StateTy state(br->getSuccessor(0), depth, savedDepth, count, countState, intGuards, sexpGuards);
                state.sexpGuards[var] = ci->isTrueWhenEqual() ? SGS_NIL : SGS_NONNIL;
                addState(doneSet, workList, state);
                if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
                  line_trace("added true branch on sexp guard, the following state", t, fun, context);
                  state.dump();
                }
              }
              if (succIndex != 0) {
                // false branch is possible
                StateTy state(br->getSuccessor(1), depth, savedDepth, count, countState, intGuards, sexpGuards);
                state.sexpGuards[var] = ci->isTrueWhenEqual() ? SGS_NONNIL : SGS_NIL;
                addState(doneSet, workList, state);
                if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
                  line_trace("added false branch on sexp guard, the following state", t, fun, context);
                  state.dump();
                }
              }
              continue;            
            }
          }
          
          // comparison with constant
          if (Constant::classof(ci->getOperand(0)) && LoadInst::classof(ci->getOperand(1))) {
            ci->swapOperands(); // have the variable first
          }
          
          if (LoadInst::classof(ci->getOperand(0)) && Constant::classof(ci->getOperand(1))) {
            LoadInst *li = cast<LoadInst>(ci->getOperand(0));
            
            if (AllocaInst::classof(li->getPointerOperand())) {
              AllocaInst *var = cast<AllocaInst>(li->getPointerOperand());

              if (intGuardsEnabled && ConstantInt::classof(ci->getOperand(1)) && cast<ConstantInt>(ci->getOperand(1))->isZero() && 
                ci->isEquality() && isIntegerGuardVariable(var, intGuardVarsCache) && !isProtectionCounterVariable(var, gl.unprotectFunction, counterVarsCache)) {
                // if (intguard) ... 
                
                // supported comparison with integer guard
                IntGuardState g = getIntGuardState(intGuards, var);
                int succIndex = -1;
                if (g != IGS_UNKNOWN) {
                  if (ci->isTrueWhenEqual()) {
                    // guard == 0
                    succIndex = (g == IGS_ZERO) ? 0 : 1;
                  } else {
                    // guard != 0
                    succIndex = (g == IGS_ZERO) ? 1 : 0;
                  }
                } 
                
                if (DEBUG) {
                  switch(succIndex) {
                    case -1:
                      line_debug("undecided branch on integer guard variable " + var->getName().str(), t, fun, context);
                      break;
                    case 0:
                      line_debug("taking true branch on integer guard variable " + var->getName().str(), t, fun, context);
                      break;
                    case 1:
                      line_debug("taking false branch on integer guard variable " + var->getName().str(), t, fun, context);
                      break;
                  }
                }
                if (succIndex != 1) {
                  // true branch is possible
                  
                  StateTy state(br->getSuccessor(0), depth, savedDepth, count, countState, intGuards, sexpGuards);
                  state.intGuards[var] = ci->isTrueWhenEqual() ? IGS_ZERO : IGS_NONZERO;
                  addState(doneSet, workList, state);
                  if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
                    line_trace("added true branch on integer guard, the following state", t, fun, context);
                    state.dump();
                  }
                }
                if (succIndex != 0) {
                  // false branch is possible
                  StateTy state(br->getSuccessor(1), depth, savedDepth, count, countState, intGuards, sexpGuards);
                  state.intGuards[var] = ci->isTrueWhenEqual() ? IGS_NONZERO : IGS_ZERO;
                  addState(doneSet, workList, state);
                  if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
                    line_trace("added false branch on integer guard, the following state", t, fun, context);
                    state.dump();
                  }
                }
                continue;
              }

              // if (nprotect) UNPROTECT(nprotect)
              if (isProtectionCounterVariable(var, gl.unprotectFunction, counterVarsCache)) {
                if (!counterVar) {
                  counterVar = var;
                } else if (counterVar != var) {
                  line_info("uses multiple pointer protection counters (results will be incorrect)", t, fun, context);
                  continue;
                }
                if (countState == CS_NONE) {
                  line_info("branches based on an uninitialized value of the protection counter variable", t, fun, context);
                  refinableInfos++;
                  if (restartable) goto abort_from_function;
                  continue;
                }
                if (countState == CS_EXACT) {
                  // we can unfold the branch with general body, and with comparisons against nonzero
                  // as we know the exact value of the counter
                  //
                  // if (nprotect) { .... }
                  
                  Constant *knownLhs = ConstantInt::getSigned(counterVar->getAllocatedType(), count);
                  Constant *res = ConstantExpr::getCompare(ci->getPredicate(), knownLhs, cast<Constant>(ci->getOperand(1)));
                  assert(ConstantInt::classof(res));
                
                  // add only the relevant successor
                  line_debug("folding out branch on counter value", t, fun, context);                
                  BasicBlock *succ;
                  if (!res->isZeroValue()) {
                    succ = br->getSuccessor(0);
                  } else {
                    succ = br->getSuccessor(1);
                  }
                  StateTy state(succ, depth, savedDepth, count, countState, intGuards, sexpGuards);
                  addState(doneSet, workList, state);
                  if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
                    line_trace("added folded successor, the following state", t, fun, context);
                    state.dump();
                  }
                  continue;
                }
                // countState == CS_DIFF
                assert(countState == CS_DIFF);
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
                    line_debug("simplifying unprotect conditional on counter value (diff state)", t, fun, context);                
                    countState = CS_NONE;
                    if (depth < 0) {
                      line_info("has negative depth after UNPROTECT(<counter>)", t, fun, context);
                      refinableInfos++;
                      goto abort_from_function;
                    }
                    // next process the code after the if
                    StateTy state(joinSucc, depth, savedDepth, count, countState, intGuards, sexpGuards);
                    addState(doneSet, workList, state);
                    if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
                      line_trace("added folded successor (diff counter state), the following state", t, fun, context);
                      state.dump();
                    }
                    continue;
                  }
                }
              }
            }
          }
        }
      }
      
      // add conservatively all cfg successors
      for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
        BasicBlock *succ = t->getSuccessor(i);
        
        StateTy state(succ, depth, savedDepth, count, countState, intGuards, sexpGuards);
        addState(doneSet, workList, state);
        if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == fun->getName())) {
          line_trace("added successor", t, fun, context);
          state.dump();
        }
      }
    }

    abort_from_function:
      if (restartable && refinableInfos>0) {
        // retry with more precise checking
        lineInfoClearForFunction(fun);
        if (!intGuardsEnabled) {
          intGuardsEnabled = true;
        } else if (!sexpGuardsEnabled) {
          sexpGuardsEnabled = true;
        }
        goto retry_function;        
      }
  }
  flushLineInfo();

  errs() << "Analyzed " << nAnalyzedFunctions << " functions\n";
  return 0;
}
