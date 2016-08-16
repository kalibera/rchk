
#include "vectors.h"
#include "patterns.h"
#include "table.h"
#include "callocators.h"
#include "exceptions.h"

#include <unordered_map>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

// #undef NDEBUG
// #include <assert.h>

using namespace llvm;

const bool DEBUG = false;

bool isVectorGuard(Function *f) {
  if (!f) return false;
  return f->getName() == "Rf_isPrimitive" || f->getName() == "Rf_isList" || f->getName() == "Rf_isFunction" ||
    f->getName() == "Rf_isPairList" || f->getName() == "Rf_isLanguage" || f->getName() == "Rf_isVector" ||
    f->getName() == "Rf_isVectorList" || f->getName() == "Rf_isVectorAtomic";
}

bool trueForVector(Function *f) { // myvector => true branch
  return f->getName() == "Rf_isVector";
}

bool trueForNonVector(Function *f) { // !myvector => true branch
  return false;
}

bool falseForVector(Function *f) { // myvector => false branch
  return f->getName() == "Rf_isPrimitive" || f->getName() == "Rf_isList" || f->getName() == "Rf_isFunction" || 
    f->getName() == "Rf_isPairList" || f->getName() == "Rf_isLanguage";
}

bool falseForNonVector(Function *f) { // !myvector => false branch
  return f->getName() == "Rf_isVector" || f->getName() == "Rf_isVectorList" || f->getName() == "Rf_isVectorAtomic";
}

bool impliesVectorWhenTrue(Function *f) {
  return f->getName() == "Rf_isVector" || f->getName() == "Rf_isVectorList" || f->getName() == "Rf_isVectorAtomic";
}

bool impliesVectorWhenFalse(Function *f) {
  return false;
}

bool isVectorType(unsigned type) {
  switch(type) {
    case RT_LOGICAL:
    case RT_INT:
    case RT_REAL:
    case RT_COMPLEX:
    case RT_STRING:
    case RT_VECTOR:
    case RT_INTCHAR:
    case RT_RAW:
    case RT_EXPRESSION:
    case RT_CHAR:
      return true;
    default:
      return false;
  }
}

// if a variable is passed to this operation, it definitely holds a vector
// (under the assumption that the program is correct, otherwise there would
// be a runtime error)

bool isVectorOnlyVarOperation(Value *inst, AllocaInst*& var) {
  AllocaInst* tvar;
  Type *type;
  
  if (!isBitCastOfVar(inst, tvar, type)) {
    return false;
  }

  if (isPointerToStruct(type, "struct.VECTOR_SEXPREC") || isPointerToStruct(type, "union.SEXPREC_ALIGN")) {
    var = tvar;
    return true;
  }
  
  std::string name;
  if (isCallPassingVar(inst, tvar, name)) {
    if (name == "SET_STRING_ELT" || name == "SET_VECTOR_ELT" || name == "XLENGTH" || name == "LENGTH" ||
      name == "VECTOR_ELT" || name == "STRING_ELT") {
      
        var = tvar;
        return true;
    }
  }
  
  return false;
}

// FIXME: the code below is quite similar to that in cprotect.cpp; any way to reduce duplication?
// TODO: need to be somewhat context sensitive
//   integer arguments (whether they represent a vector type, or not)
//   SEXP argumets (whether they are known to be vectors, or not)
//
//   support argument propagation
//   TYPEOF
//   ?? type guards (or this is too much work)
//   ?? guards of form mode == const
//
//  note, the semantics is however quite different, this is context sensitive
//
// FIXME: it would be quite cheap to extend this also to checking which
// functions may return an integer indicating a vector type (yet it is not
// clear whether it would help the checking in practice)
//
// FIXME: should we attempt to do any context discovery?
//        finding about more contexts of (root) functions of interest
//          could certainly use all immediate constants
//          but also could check all functions, even those not returning SEXP, for the purpose
//          of discovering the contexts, and the possibly doing some annotations


typedef std::vector<bool> ArgsTy; // which argument is a vector (SEXP) or representing a vector type (integer)
typedef std::vector<bool> VarsTy;

typedef IndexedTable<Argument> ArgIndexTy;
typedef IndexedTable<AllocaInst> VarIndexTy;

struct FunctionState;
typedef std::unordered_map<Function*, FunctionState> FunctionTableTy;
typedef std::vector<Function*> FunctionListTy;

typedef IndexedCopyingTable<ArgsTy> ContextIndexTy;
typedef std::vector<bool> ReturnsOnlyVectorTy;


bool isNonDefaultContext(ArgsTy& context) {
  unsigned nargs = context.size();
  for(unsigned a = 0; a < nargs; a++) {
    if (context.at(a)) {
      return true;
    }
  }
  return false;
}

std::string funNameWithContext(Function *fun, ArgsTy context) {
  unsigned nargs = context.size();
  std::string res = funName(fun);
  
  if (!isNonDefaultContext(context)) {
    return res;
  }
  
  res += "(";
  for(unsigned a = 0; a < nargs; a++) {
    if (a>0) {
      res += ",";
    }
    if (context.at(a)) {
      res += "V";
    } else {
      res += "?";
    }
  }
  res += ")";
  
  return res;
}

struct FunctionState {
  Function *fun;
  bool dirty; // dirty iff in functions work list

  VarIndexTy varIndex;
  ArgIndexTy argIndex;
  ContextIndexTy contextIndex;
  ReturnsOnlyVectorTy returnsOnlyVector;
  
  FunctionState(Function *fun): fun(fun), dirty(false), varIndex(), argIndex(), contextIndex(), returnsOnlyVector() {
  
    // index variables
    for(inst_iterator ii = inst_begin(*fun), ie = inst_end(*fun); ii != ie; ++ii) {
      Instruction *in = &*ii;
      if (AllocaInst* var = dyn_cast<AllocaInst>(in)) {
        varIndex.indexOf(var);
      }
    }
    
    // index arguments
    for(Function::arg_iterator ai = fun->arg_begin(), ae = fun->arg_end(); ai != ae; ++ai) {
      Argument *a = &*ai;
      argIndex.indexOf(a);
    }
    
    // add default context, it will have index 0
    ArgsTy args(argIndex.size(), false);
    contextIndex.indexOf(args);
    returnsOnlyVector.resize(1);
    returnsOnlyVector.at(0) = false;
  }
  
  void addToWorkList(FunctionListTy& workList) {
    if (!dirty) {
      dirty = true;
      workList.push_back(fun);
    }    
  }
  
  static FunctionState& get(FunctionTableTy& functions, Function *f) {
    auto fsearch = functions.find(f);
    assert(fsearch != functions.end());
    return fsearch->second;
  }  
};

struct VrfStateTy {
  FunctionTableTy functions;
  
  VrfStateTy() : functions() {};
};

struct BlockState {
  VarsTy vars; // which vars are "vector" after the basic block executes
  bool dirty;
  
  BlockState(unsigned nvars): vars(nvars, false), dirty(false) {};
  
  BlockState(VarsTy vars, bool dirty): vars(vars), dirty(dirty) {};
  
  bool merge(BlockState& s) {

    bool updated = false;
    for(unsigned i = 0; i < vars.size(); i++) {
      if (vars.at(i)) {
        if (!s.vars.at(i)) {
          vars.at(i) = false;
          updated = true;          
        }
      }
      
    }
    return updated;
  }

};

typedef std::unordered_map<BasicBlock*, BlockState> BlocksTy;
typedef std::vector<BasicBlock*> BlockWorkListTy;

static bool callReturnsOnlyVector(CallSite& cs, FunctionState& fstate, BlockState& s, ArgsTy& context, FunctionTableTy& functions, FunctionListTy& functionsWorkList, CalledModuleTy *cm) {

  if (!cs) {
    return false;
  }

  Function *tgt = cs.getCalledFunction();
  if (!tgt) {
    return false;
  }
  
  const CalledFunctionTy *ctgt = cm->getCalledFunction(cs.getInstruction(), NULL, NULL, false); // this will infer some uses of symbols
  if (isKnownVectorReturningFunction(ctgt)) {
    return true;
  }
  
  // build arguments (context)
  unsigned tnargs = cs.arg_size();
  ArgsTy targs(tnargs, false);
  
  for(unsigned i = 0; i < tnargs; i++) {
    Value *targ = cs.getArgument(i);
    if (LoadInst *li = dyn_cast<LoadInst>(targ)) {
      if (AllocaInst *avar = dyn_cast<AllocaInst>(li->getPointerOperand())) { // passing a variable
        unsigned avidx = fstate.varIndex.indexOf(avar);
        targs.at(i) = s.vars.at(avidx);
        continue;
      }
    }

    if (Argument *arg = dyn_cast<Argument>(targ)) { // is this possible?
      unsigned aidx = fstate.argIndex.indexOf(arg);
      targs.at(i) = context.at(aidx);
      continue; 
    }
    if (ConstantInt *ci = dyn_cast<ConstantInt>(targ)) { // passing a constant
      targs.at(i) = isVectorType(ci->getZExtValue());
      continue;
    }
    continue;
  }
  
  if (tgt->getName() == "Rf_allocVector") { // must handle this one specially
    if (DEBUG) errs() << " = allocVector [ = " << (targs.at(0) ? "vector" : "unknown") << " ]" << sourceLocation(cs.getInstruction()) << "\n";
    return targs.at(0); // the first argument of allocVector
  }
  
  if (DEBUG) errs() << " [target " << funNameWithContext(tgt, targs) << "]";

  FunctionState& tstate = FunctionState::get(functions, tgt);
  unsigned tcontextIdx = tstate.contextIndex.indexOf(targs);

  if (tcontextIdx < tstate.returnsOnlyVector.size()) {
    if (DEBUG) errs() << " = foo() in context [ = " << (tstate.returnsOnlyVector.at(tcontextIdx) ? "vector" : "unknown") << " ] " << sourceLocation(cs.getInstruction()) << "\n";
    return tstate.returnsOnlyVector.at(tcontextIdx);

  } else {
    // the target function has not yet been explored in this context
    // we have just added that context to that function, so lets mark it dirty
    // we have not expanded tstate.retursOnlyVector, it will be done when tstate is re-visited
    
    tstate.addToWorkList(functionsWorkList);
                
    // and lets use the default context for the function
    if (DEBUG) errs() << " = foo() in DEFAULT context [ = " << (tstate.returnsOnlyVector.at(0) ? "vector" : "unknown") << " ] " << sourceLocation(cs.getInstruction()) << "\n";
    if (DEBUG) {
      errs() << "Function " << funName(tgt) << " now has these contexts: (fstate=" << &tstate << ")\n";
      unsigned ntcontexts = tstate.contextIndex.size();
      for(unsigned i = 0; i < ntcontexts; i++) {
        errs() << "  " << funNameWithContext(tgt, tstate.contextIndex.at(i)) << " ";
        if (i < tstate.returnsOnlyVector.size()) {
          if (tstate.returnsOnlyVector.at(i)) {
            errs() << "returns vector";
          } else {
            errs() << "may return non-vector";
          }
        } else {
          errs() << "yet unknown";
        }
        errs() << "\n";
      }
    }
    return tstate.returnsOnlyVector.at(0);
  }
}

static bool valueIsVector(Value *val, FunctionState& fstate, BlockState& s, ArgsTy& context, FunctionTableTy& functions, FunctionListTy& functionsWorkList, CalledModuleTy *cm) {
          
  if (Argument *arg = dyn_cast<Argument>(val)) {  // = arg
    unsigned aidx = fstate.argIndex.indexOf(arg);
    return context.at(aidx);
  }
          
  if (LoadInst *li = dyn_cast<LoadInst>(val)) { // = srcVar
    if (AllocaInst *srcVar = dyn_cast<AllocaInst>(li->getPointerOperand())) { // var = srcVar
      unsigned svidx = fstate.varIndex.indexOf(srcVar);
      return s.vars.at(svidx);
    }
  }
          
  if (ConstantInt *ci = dyn_cast<ConstantInt>(val)) { // = constint
    unsigned type = ci->getZExtValue();
    return isVectorType(type);
  }
          
  CallSite cs(val);  // = foo()
  if (cs && cs.getCalledFunction()) {
    Function *tgt = cs.getCalledFunction();
    if (isSEXP(tgt->getReturnType())) {
      return callReturnsOnlyVector(cs, fstate, s, context, functions, functionsWorkList, cm);
    }
  }

  return false;
}

static void analyzeFunctionInContext(FunctionState& fstate, unsigned contextIdx, FunctionTableTy& functions, FunctionListTy& functionsWorkList, CalledModuleTy *cm) {

  Function *fun = fstate.fun;
  ArgsTy context = fstate.contextIndex.at(contextIdx);

  bool returnsVector = false; // only look at functions that we see may actually return
  unsigned nvars = fstate.varIndex.size();
  
  BlocksTy blocks;
  BlockWorkListTy workList;
  
  BasicBlock *entryb = &fun->getEntryBlock();
  blocks.insert({entryb, BlockState(nvars)});
  workList.push_back(entryb);
  
  if (DEBUG) errs() << "Analyzing function " << funNameWithContext(fun, context) << "\n";
  
  while(!workList.empty()) {
    BasicBlock *bb = workList.back();
    workList.pop_back();
    
    auto bsearch = blocks.find(bb);
    assert(bsearch != blocks.end());
    bsearch->second.dirty = false;
    BlockState s = bsearch->second; // copy
    
    for(BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
      Instruction *in = ii;
      
      if (StoreInst *si = dyn_cast<StoreInst>(in)) {
        if (AllocaInst *var = dyn_cast<AllocaInst>(si->getPointerOperand())) {

          unsigned vidx = fstate.varIndex.indexOf(var);
          if (DEBUG) errs() << "var " << varName(var) << " ";
          s.vars.at(vidx) = valueIsVector(si->getValueOperand(), fstate, s, context, functions, functionsWorkList, cm);
        }
        continue;
      }
      
      AllocaInst *var;
      if (isVectorOnlyVarOperation(in, var)) { // LENGTH(var) and friends
         unsigned vidx = fstate.varIndex.indexOf(var);
         s.vars.at(vidx) = true;
         if (DEBUG) errs() << "var is vector (vector-only-operation) [" << varName(var) << "] " << sourceLocation(in) << "\n";
         continue;
      }
      // TODO: handle variables with address taken
    }
    
    TerminatorInst *t = bb->getTerminator();

    if (ReturnInst *r = dyn_cast<ReturnInst>(t)) {
    
      if (valueIsVector(r->getReturnValue(), fstate, s, context, functions, functionsWorkList, cm)) {
        returnsVector = true;
        continue;
      }

      // either unsupported return, or supported (above) but one that discovered non-vector
      fstate.returnsOnlyVector.at(contextIdx) = false;
      if (DEBUG) errs() << "Function " << funNameWithContext(fun, context) << " may return non-vector " << sourceLocation(t) << "\n";
      return;
      
    }    

    // TODO: handle guards on types
    
    // conservatively add all successors    
    for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
      BasicBlock *succ = t->getSuccessor(i);
      auto ssearch = blocks.find(succ);
      if (ssearch == blocks.end()) {
      
        // not yet explored block
        BlockState sstate(s.vars, true /* dirty */);
        blocks.insert({succ, sstate});
        workList.push_back(succ);

      } else {
        BlockState& pstate = ssearch->second;
        
        // merge
        if (pstate.merge(s) && !pstate.dirty) {
          pstate.dirty = true;
          workList.push_back(succ);
        }
      }
    }
  }
  fstate.returnsOnlyVector.at(contextIdx) = true;
  if (DEBUG) errs() << "Function " << funNameWithContext(fun, context) << " returns only vectors\n";
}

static void analyzeFunction(FunctionState& fstate, FunctionTableTy& functions, FunctionListTy& functionsWorkList, CalledModuleTy *cm) {

  Function *fun = fstate.fun;
  
  unsigned ncontexts = fstate.contextIndex.size();
  assert(fstate.returnsOnlyVector.size() <= ncontexts);
  fstate.returnsOnlyVector.resize(ncontexts); // add new elements, contexts may have been added in the meantime
  
  ReturnsOnlyVectorTy before = fstate.returnsOnlyVector;
  
  if (DEBUG) errs() << "Analyzing (all " << ncontexts << " contexts of) function " << funName(fun) << " fstate " << &fstate << "\n";
  
  for(unsigned i = 0; i < ncontexts; i++) {
    analyzeFunctionInContext(fstate, i, functions, functionsWorkList, cm);  
  }
  
  if (before != fstate.returnsOnlyVector) {
    // mark dirty all functions calling this function
    
    for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
      User *u = *ui;

      if (Instruction *in = dyn_cast<Instruction>(u)) {
        if (BasicBlock *bb = dyn_cast<BasicBlock>(in->getParent())) {
          Function *pf = bb->getParent();
          if (isSEXP(pf->getReturnType())) {
            FunctionState& pstate = FunctionState::get(functions, pf);
            pstate.addToWorkList(functionsWorkList);
            if (DEBUG) errs() << "Marking dirty affected caller function " << funName(pf) << "\n";
            // NOTE: in case of recursive functions, we may be re-adding fun
          }
        }
      }
    }    
  }
}

void findVectorReturningFunctions(CalledModuleTy *cm) {

  VrfStateTy* res = new VrfStateTy();
  cm->setVrfState(res);
  
  FunctionTableTy &functions = res->functions;
  FunctionListTy workList;   // functions to be re-analyzed
 
  // add some functions to the worklist, with default contexts  
  Module *m = cm->getModule();
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *f = fi;
    if (!isSEXP(f->getReturnType())) {
      continue;
      // if a function does not return an SEXP, it definitely does not return a vector
    }
    FunctionState fstate(f);
    auto finsert = functions.insert({f, fstate});
    assert(finsert.second);

    fstate.addToWorkList(workList);
  }
  
  while(!workList.empty()) {
    FunctionState& fstate = FunctionState::get(functions, workList.back());
    workList.pop_back();
    fstate.dirty = false;

    analyzeFunction(fstate, functions, workList, cm);
  }
}

void printVectorReturningFunctions(FunctionTableTy *functionsPtr) {
  FunctionTableTy& functions = *functionsPtr;
  
  errs() << "Functions returning only vectors:\n";
  
  for(FunctionTableTy::iterator fi = functions.begin(), fe = functions.end(); fi != fe; ++fi) {
    Function* fun = fi->first;
    FunctionState& fstate = fi->second;
    
    unsigned nargs = fstate.argIndex.size();
    unsigned ncontexts = fstate.contextIndex.size();
    assert(ncontexts == fstate.returnsOnlyVector.size());
    
    bool seenTrue = false;
    bool seenFalse = false;
    
    for(unsigned i = 0; i < ncontexts; i++) {
      if (fstate.returnsOnlyVector.at(i)) {
        seenTrue = true;
      } else {
        seenFalse = true;
      }
    }
    
    if (seenTrue) { 
      if (!seenFalse) {
        errs() << "  " << funName(fun) << "\n";
      } else {
        for(unsigned i = 0; i < ncontexts; i++) {
          if (fstate.returnsOnlyVector.at(i)) {
            errs() << "  " << funNameWithContext(fun, fstate.contextIndex.at(i)) << "\n";
          }
        }
      }
    }
  }  
}

void printVectorReturningFunctions(CalledModuleTy *cm) {
  printVectorReturningFunctions(&(cm->getVrfState()->functions));
}

void freeVrfState(VrfStateTy *vrfState) {
  delete vrfState;
}

bool isVectorReturningFunction(Function *fun, ArgsTy context, CalledModuleTy* cm) {

  FunctionTableTy* functionsPtr = &(cm->getVrfState()->functions);
  FunctionListTy workList;
  FunctionTableTy& functions = *functionsPtr;

  if (!isSEXP(fun->getReturnType())) {
    return false;
  }

  unsigned contextIdx = 0;
  
  auto fsearch = functions.find(fun);
  if (fsearch == functions.end()) {
    FunctionState fstate(fun);
    contextIdx = fstate.contextIndex.indexOf(context); // add context
    auto finsert = functions.insert({fun, fstate});
    assert(finsert.second);

    fstate.addToWorkList(workList);    
  } else {
    FunctionState& fstate = fsearch->second;
    contextIdx = fstate.contextIndex.indexOf(context); // add context
    
    fstate.addToWorkList(workList);    
  }
  
  while(!workList.empty()) {
    FunctionState& fstate = FunctionState::get(functions, workList.back());
    workList.pop_back();
    fstate.dirty = false;

    analyzeFunction(fstate, functions, workList, cm);
  }
  
  FunctionState& fstate = FunctionState::get(functions, fun);
  bool res = fstate.returnsOnlyVector.at(contextIdx);

  if (DEBUG) errs() << "isVectorReturningFunction: function " << funNameWithContext(fun, context) << (res ? "returns only vector" : "may return non-vector") << "\n";
  
  return res;
}


bool isVectorProducingCall(Value *inst, CalledModuleTy* cm, SEXPGuardsChecker* sexpGuardsChecker, SEXPGuardsTy *sexpGuards) {
  unsigned type;
  
  if (isAllocVectorOfKnownType(inst, type)) {
    return isVectorType(type);
  }
  
  const CalledFunctionTy *ctgt = cm->getCalledFunction(inst, sexpGuardsChecker, sexpGuards, true /* needed? */); 
  if (!ctgt) {
    return false;
  }
    
  if (isKnownVectorReturningFunction(ctgt)) {
    return true;
  }
  
  CallSite cs(inst);
  if (!cs || !cs.getCalledFunction()) {
    return false;
  }
  
  if (sexpGuards && sexpGuardsChecker) {
    // turn SEXP guards into vector-return-function guards
    unsigned tnargs = cs.arg_size();
    ArgsTy targs(tnargs, false);
  
    for(unsigned i = 0; i < tnargs; i++) {
      Value *targ = cs.getArgument(i);
      if (LoadInst *li = dyn_cast<LoadInst>(targ)) {
        if (AllocaInst *avar = dyn_cast<AllocaInst>(li->getPointerOperand())) { // passing a variable
      
          SEXPGuardState gs = sexpGuardsChecker->getGuardState(*sexpGuards, avar);
          if (gs == SGS_VECTOR) {
            targs.at(i) = true;
            continue;
          }
        }
        continue;
      }
      if (isVectorProducingCall(targ, cm, sexpGuardsChecker, sexpGuards)) {
        // NOTE: this recursion is bounded by how many nested call expressions we have, there cannot be a loop
        targs.at(i) = true;
      }
    }
    
    return isVectorReturningFunction(cs.getCalledFunction(), targs, cm);
  }
  
  return false;
}


