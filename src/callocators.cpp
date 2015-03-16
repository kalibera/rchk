
#include "callocators.h"
#include "errors.h"
#include "guards.h"
#include "symbols.h"
#include "linemsg.h"
#include "state.h"

#include <map>
#include <stack>
#include <unordered_set>

#include <llvm/IR/CallSite.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

#include <llvm/Support/InstIterator.h>

using namespace llvm;

  // FIXME: could reduce copy-paste vs. bcheck?
const bool DEBUG = false;
const bool TRACE = false;
const bool UNIQUE_MSG = true;
const int MAX_STATES = 1000000;
const bool VERBOSE_DUMP = false;

const bool DUMP_STATES = false;
const std::string DUMP_STATES_FUNCTION = "getAttrib0(?,S:class)"; // only dump states in this function
const bool ONLY_FUNCTION = false; // only check one function (named ONLY_FUNCTION_NAME)
const std::string ONLY_FUNCTION_NAME = "getAttrib0(?,S:class)";
const bool ONLY_DEBUG_ONLY_FUNCTION = true;
const bool ONLY_TRACE_ONLY_FUNCTION = true;

std::string CalledFunctionTy::getName() const {
  std::string res = fun->getName();
  std::string suff;
  unsigned nKnown = 0;

  for(ArgInfosTy::const_iterator ai = argInfo->begin(), ae = argInfo->end(); ai != ae; ++ai) {
    const ArgInfoTy *a = *ai;
    if (ai != argInfo->begin()) {
      suff += ",";
    }
    if (a && a->isSymbol()) {
      suff += "S:" + cast<SymbolArgInfoTy>(a)->symbolName;
      nKnown++;
    } else {
      suff += "?";
    }
  }
  
  if (nKnown > 0) {
    res += "(" + suff + ")";
  }
  return res;
}

size_t CalledFunctionPtrTy_hash::operator()(const CalledFunctionTy* t) const {
  size_t res = 0;
  hash_combine(res, t->fun);
  hash_combine(res, t->argInfo); // argInfos are interned
  return res;
}

bool CalledFunctionPtrTy_equal::operator() (const CalledFunctionTy* lhs, const CalledFunctionTy* rhs) const {
  return lhs->fun == rhs->fun && lhs->argInfo == rhs->argInfo && lhs->module == rhs->module;  // argInfos are interned
}

size_t ArgInfosPtrTy_hash::operator()(const ArgInfosTy* t) const {
  size_t res = 0;
  size_t cnt = 0;
  for(ArgInfosTy::const_iterator ai = t->begin(), ae = t->end(); ai != ae; ++ai) {
    const ArgInfoTy *a = *ai;
    if (a && a->isSymbol()) {
      hash_combine(res, cast<SymbolArgInfoTy>(a)->symbolName);
      cnt++;
    }
  }
  hash_combine(res, cnt);
  return res;
}

bool ArgInfosPtrTy_equal::operator() (const ArgInfosTy* lhs, const ArgInfosTy* rhs) const {
  
  size_t nelems = lhs->size();
  if (nelems != rhs->size()) {
    return false;
  }
  for (size_t i = 0; i < nelems; i++) {
    ArgInfoTy* la = (*lhs)[i];
    ArgInfoTy* ra = (*rhs)[i];
    
    if (la == ra) {
      continue;
    }
    if (!la || !ra) {
      return false;
    }
    if (cast<SymbolArgInfoTy>(la)->symbolName != cast<SymbolArgInfoTy>(ra)->symbolName) {
      return false;
    }
  }
  return true;
}

size_t ArgInfoPtrTy_hash::operator()(const ArgInfoTy* t) const {
  size_t res = 0;
  if (!t || !t->isSymbol()) {
    return res;
  }
  const SymbolArgInfoTy *ai = cast<SymbolArgInfoTy>(t);
  hash_combine(res, ai->symbolName);
  return res;
}

bool ArgInfoPtrTy_equal::operator() (const ArgInfoTy* lhs, const ArgInfoTy* rhs) const {
  
  if (!lhs || !rhs || !lhs->isSymbol() || !rhs->isSymbol()) {
    return lhs == rhs;
  }
  const SymbolArgInfoTy *li = cast<SymbolArgInfoTy>(lhs);
  const SymbolArgInfoTy *ri = cast<SymbolArgInfoTy>(rhs);
  return li->symbolName == ri->symbolName;
}

// FIXME: template-based interning

ArgInfosTy* CalledModuleTy::intern(ArgInfosTy& argInfos) { // arg may be stack allocated

  ArgInfosTy *ai;
  auto asearch = argInfosTable.find(&argInfos);
  if (asearch != argInfosTable.end()) {
    ai = *asearch; // found in intern table
  } else {
    ai = new ArgInfosTy(argInfos); // copy to heap
    argInfosTable.insert(ai);
  }
  return ai;
}

SymbolArgInfoTy* CalledModuleTy::intern(SymbolArgInfoTy& argInfo) { // arg may be stack allocated

  SymbolArgInfoTy *ai;

  auto asearch = argInfoTable.find(&argInfo);
  if (asearch != argInfoTable.end()) {
    ai = cast<SymbolArgInfoTy>(*asearch); // found in intern table
  } else {
    ai = new SymbolArgInfoTy(argInfo); // copy to heap
    argInfoTable.insert(ai);
  }
  return ai;  
}

CalledFunctionTy* CalledModuleTy::intern(CalledFunctionTy& calledFunction) { // arg may be stack allocated
  CalledFunctionTy *cfun;
  auto fsearch = calledFunctionsTable.find(&calledFunction);
  if (fsearch != calledFunctionsTable.end()) {
    cfun = *fsearch;  // found in intern table
  } else {
    cfun = new CalledFunctionTy(calledFunction); // copy to heap
    calledFunctionsTable.insert(cfun);
    cfun->idx = calledFunctionsVector.size();
    calledFunctionsVector.push_back(cfun);
  }
  return cfun;
}

CalledFunctionTy* CalledModuleTy::getCalledFunction(Function *f) {
  size_t nargs = f->arg_size();
  ArgInfosTy argInfos(nargs, NULL);
  CalledFunctionTy calledFunction(f, intern(argInfos), this);
  return intern(calledFunction);
}

CalledFunctionTy* CalledModuleTy::getCalledFunction(Value *inst, bool registerCallSite) {
  return getCalledFunction(inst, NULL, registerCallSite);
}

CalledFunctionTy* CalledModuleTy::getCalledFunction(Value *inst, SEXPGuardsTy *sexpGuards, bool registerCallSite) {
  // FIXME: this is quite inefficient, does a lot of allocation
  
  CallSite cs (inst);
  if (!cs) {
    return NULL;
  }
  Function *fun = cs.getCalledFunction();
  if (!fun) {
    return NULL;
  }
      
  // build arginfo
      
  unsigned nargs = cs.arg_size();
  ArgInfosTy argInfo(nargs, NULL);

  for(unsigned i = 0; i < nargs; i++) {
    Value *arg = cs.getArgument(i);
    if (LoadInst::classof(arg)) { // R_XSymbol
      Value *src = cast<LoadInst>(arg)->getPointerOperand();
      if (GlobalVariable::classof(src)) {
        auto ssearch = symbolsMap->find(cast<GlobalVariable>(src));
        if (ssearch != symbolsMap->end()) {
          SymbolArgInfoTy ai(ssearch->second);
          argInfo[i] = intern(ai);
          continue;
        }
      }
      if (sexpGuards && AllocaInst::classof(src)) {
        AllocaInst *var = cast<AllocaInst>(src);
        auto gsearch = sexpGuards->find(var);
        if (gsearch != sexpGuards->end()) {
          std::string symbolName;
          SEXPGuardState gs = getSEXPGuardState(*sexpGuards, var, symbolName);
          if (gs == SGS_SYMBOL) {
            SymbolArgInfoTy ai(symbolName);
            argInfo[i] = intern(ai);
            continue;
          }
        }
      }
    }
    std::string symbolName;  // install("X")
    if (isInstallConstantCall(arg, symbolName)) {
      argInfo[i] = new SymbolArgInfoTy(symbolName);
      continue;
    }
    // not a symbol, leave argInfo as NULL
  }
      
  CalledFunctionTy calledFunction(fun, intern(argInfo), this);
  CalledFunctionTy* cf = intern(calledFunction);
  
  if (registerCallSite) {
    auto csearch = callSiteTargets.find(inst);
    if (csearch == callSiteTargets.end()) {
      CalledFunctionsSetTy newSet;
      newSet.insert(cf);
      callSiteTargets.insert({inst, newSet});
    } else {
      CalledFunctionsSetTy& existingSet = csearch->second;
      existingSet.insert(cf);
    }
  }
  
  return cf;
}

CalledModuleTy::CalledModuleTy(Module *m, SymbolsMapTy *symbolsMap, FunctionsSetTy* errorFunctions, GlobalsTy* globals, 
  FunctionsSetTy* possibleAllocators, FunctionsSetTy* allocatingFunctions):
  
  m(m), symbolsMap(symbolsMap), errorFunctions(errorFunctions), globals(globals), possibleAllocators(possibleAllocators), allocatingFunctions(allocatingFunctions),
  callSiteTargets() {

  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *fun = fi;

    getCalledFunction(fun); // make sure each function has a context-function counterpart
    for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
      User *u = *ui;
      getCalledFunction(cast<Value>(u)); // NOTE: this only gets contexts that are constant, more are gotten during allocators computation
    }
  }  
  gcFunction = getCalledFunction(getGCFunction(m));
  
    // only compute on demand - it takes a bit of time
  possibleCAllocators = NULL;
  allocatingCFunctions = NULL;
}

CalledModuleTy::~CalledModuleTy() {

  // delete dynamically allocated elements in intern tables
  for(CalledFunctionsTableTy::iterator cfi = calledFunctionsTable.begin(), cfe = calledFunctionsTable.end(); cfi != cfe; ++cfi) {
    CalledFunctionTy *cfun = *cfi;
    delete cfun;
  }
  for(ArgInfosSetTy::iterator ai = argInfosTable.begin(), ae = argInfosTable.end(); ai != ae; ++ai) {
    ArgInfosTy *a = *ai;
    delete a;
  }
  if (possibleCAllocators) {
    delete possibleCAllocators;
  }
  if (allocatingCFunctions) {
    delete allocatingCFunctions;
  }
}

CalledModuleTy* CalledModuleTy::create(Module *m) {
  SymbolsMapTy *symbolsMap = new SymbolsMapTy();
  findSymbols(m, symbolsMap);

  FunctionsSetTy *errorFunctions = new FunctionsSetTy();
  findErrorFunctions(m, *errorFunctions);
  
  GlobalsTy *globals = new GlobalsTy(m);
  
  FunctionsSetTy *possibleAllocators = new FunctionsSetTy();
  findPossibleAllocators(m, *possibleAllocators);

  FunctionsSetTy *allocatingFunctions = new FunctionsSetTy();
  findAllocatingFunctions(m, *allocatingFunctions);
      
  return new CalledModuleTy(m, symbolsMap, errorFunctions, globals, possibleAllocators, allocatingFunctions);
}

void CalledModuleTy::release(CalledModuleTy *cm) {
  delete cm->getAllocatingFunctions();
  delete cm->getPossibleAllocators();
  delete cm->getGlobals();
  delete cm->getErrorFunctions();
  delete cm->getSymbolsMap();
  delete cm;
}

typedef std::map<AllocaInst*,CalledFunctionsOrderedSetTy> VarOriginsTy;
  // for a local variable, a list of functions whose return values may have
  // been assigned, possibly indirectly, to that variable


struct CAllocStateTy : public StateWithGuardsTy {
  size_t hashcode;
  CalledFunctionsOrderedSetTy called;
  VarOriginsTy varOrigins;

  CAllocStateTy(BasicBlock *bb): StateBaseTy(bb), StateWithGuardsTy(bb), hashcode(0), called(), varOrigins() {};

  CAllocStateTy(BasicBlock *bb, IntGuardsTy& intGuards, SEXPGuardsTy& sexpGuards, CalledFunctionsOrderedSetTy& called, VarOriginsTy& varOrigins):
      StateBaseTy(bb), StateWithGuardsTy(bb, intGuards, sexpGuards), hashcode(0), called(called), varOrigins(varOrigins) {};
      
  virtual CAllocStateTy* clone(BasicBlock *newBB) {
    return new CAllocStateTy(newBB, intGuards, sexpGuards, called, varOrigins);
  }
    
  void hash() {
    size_t res = 0;
    hash_combine(res, bb);
    hash_combine(res, intGuards.size()); // FIXME: avoid copy-paste
    for(IntGuardsTy::const_iterator gi = intGuards.begin(), ge = intGuards.end(); gi != ge; *gi++) {
      AllocaInst* var = gi->first;
      IntGuardState s = gi->second;
      hash_combine(res, (void *)var);
      hash_combine(res, (char) s);
    } // ordered map

    hash_combine(res, sexpGuards.size()); // FIXME: avoid copy-paste
    for(SEXPGuardsTy::const_iterator gi = sexpGuards.begin(), ge = sexpGuards.end(); gi != ge; *gi++) {
      AllocaInst* var = gi->first;
      const SEXPGuardTy& g = gi->second;
      hash_combine(res, (void *) var);
      hash_combine(res, (char) g.state);
      hash_combine(res, g.symbolName);
    } // ordered map

    hash_combine(res, called.size());
    for(CalledFunctionsOrderedSetTy::iterator fi = called.begin(), fe = called.end(); fi != fe; ++fi) {
      CalledFunctionTy *f = *fi;
      hash_combine(res, (void *) f);
    } // ordered set

    hash_combine(res, varOrigins.size());
    for(VarOriginsTy::iterator oi = varOrigins.begin(), oe = varOrigins.end(); oi != oe; ++oi) {
      AllocaInst* var = oi->first;
      CalledFunctionsOrderedSetTy& srcs = oi->second;
        
      hash_combine(res, (void *)var);
      hash_combine(res, srcs.size());
        
      for(CalledFunctionsOrderedSetTy::iterator fi = srcs.begin(), fe = srcs.end(); fi != fe; ++fi) {
        CalledFunctionTy *f = *fi;
        hash_combine(res, (void *) f);
      } // ordered set
    } // ordered map
    
    hashcode = res;
  }

  void dump() {
    StateBaseTy::dump(VERBOSE_DUMP);
    StateWithGuardsTy::dump(VERBOSE_DUMP);

    errs() << "=== called (allocating):\n";
    for(CalledFunctionsOrderedSetTy::iterator fi = called.begin(), fe = called.end(); fi != fe; *fi++) {
      CalledFunctionTy* f = *fi;
      errs() << "   " << f->getName() << "\n";
    }
    errs() << "=== origins (allocators):\n";
    for(VarOriginsTy::iterator oi = varOrigins.begin(), oe = varOrigins.end(); oi != oe; ++oi) {
      AllocaInst* var = oi->first;
      CalledFunctionsOrderedSetTy& srcs = oi->second;

      errs() << "   " << varName(var) << ":";
        
      for(CalledFunctionsOrderedSetTy::iterator fi = srcs.begin(), fe = srcs.end(); fi != fe; ++fi) {
        CalledFunctionTy *f = *fi;
        errs() << " " << f->getName();
      }
      errs() << "\n";
    }
    errs() << " ######################            ######################\n";
  }
    
  virtual bool add();
};

// the hashcode is cached at the time of first hashing
//   (and indeed is not copied)

struct CAllocStatePtrTy_hash {
  size_t operator()(const CAllocStateTy* t) const {
    return t->hashcode;
  }
};

struct CAllocStatePtrTy_equal {
  bool operator() (const CAllocStateTy* lhs, const CAllocStateTy* rhs) const {

    bool res;
    if (lhs == rhs) {
      res = true;
    } else {
      res = lhs->bb == rhs->bb && 
        lhs->intGuards == rhs->intGuards && lhs->sexpGuards == rhs->sexpGuards &&
        lhs->called == rhs->called && lhs->varOrigins == rhs->varOrigins;
    }
    
    return res;
  }
};

typedef std::stack<CAllocStateTy*> WorkListTy;
typedef std::unordered_set<CAllocStateTy*, CAllocStatePtrTy_hash, CAllocStatePtrTy_equal> DoneSetTy;

static WorkListTy workList; // FIXME: avoid these "globals"
static DoneSetTy doneSet;   // FIXME: avoid these "globals"

bool CAllocStateTy::add() { // FIXME: avoid copy paste (vs. bcheck)
  hash(); // precompute hashcode
  auto sinsert = doneSet.insert(this);
  if (sinsert.second) {
    workList.push(this);
    return true;
  } else {
    delete this; // NOTE: state suicide
    return false;
  }
}

static void clearStates() { // FIXME: avoid copy paste (vs. bcheck)
  // clear the worklist and the doneset
  for(DoneSetTy::iterator ds = doneSet.begin(), de = doneSet.end(); ds != de; ++ds) {
    CAllocStateTy *old = *ds;
    delete old;
  }
  doneSet.clear();
  WorkListTy empty;
  std::swap(workList, empty);
  // all elements in worklist are also in doneset, so no need to call destructors
}

static void getCalledAndWrappedFunctions(CalledFunctionTy *f, LineMessenger& msg, 
  CalledFunctionsOrderedSetTy& called, CalledFunctionsOrderedSetTy& wrapped) {

    if (!f->fun || !f->fun->size()) {
      return;
    }
    CalledModuleTy *cm = f->module;
    
    VarBoolCacheTy intGuardVarsCache;
    VarBoolCacheTy sexpGuardVarsCache;

    BasicBlocksSetTy errorBasicBlocks;
    findErrorBasicBlocks(f->fun, cm->getErrorFunctions(), errorBasicBlocks); // FIXME: this could be remembered in CalledFunction
    
    VarsSetTy possiblyReturnedVars; 
    findPossiblyReturnedVariables(f->fun, possiblyReturnedVars); // to restrict origin tracking
    
    bool trackOrigins = isSEXP(f->fun->getReturnType());
    
    if (DEBUG && ONLY_DEBUG_ONLY_FUNCTION) {
      if (ONLY_FUNCTION_NAME == f->getName()) {
        msg.debug(true);
      } else {
        msg.debug(false);
      }
    }

    if (TRACE && ONLY_TRACE_ONLY_FUNCTION) {
      if (ONLY_FUNCTION_NAME == f->getName()) {
        msg.trace(true);
      } else {
        msg.trace(false);
      }
    }
    
    msg.newFunction(f->fun, " - " + f->getName());

    clearStates();
    {
      CAllocStateTy* initState = new CAllocStateTy(&f->fun->getEntryBlock());
      initState->add();
    }
    while(!workList.empty()) {
      CAllocStateTy s(*workList.top());
      workList.pop();    

      if (ONLY_FUNCTION && ONLY_FUNCTION_NAME != f->getName()) {
        continue;
      }      
      if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == f->getName())) {
        msg.trace("going to work on this state:", s.bb->begin());
        s.dump();
      }
      
      if (errorBasicBlocks.find(s.bb) != errorBasicBlocks.end()) {
        msg.debug("ignoring basic block on error path", s.bb->begin());
        continue;
      }
      
      if (doneSet.size() > MAX_STATES) {
        msg.error("too many states (abstraction error?) - returning path-insensitive allocation info", s.bb->begin());
        
        // NOTE: some callsites may have already been registered to more specific called functions
        
        for(inst_iterator ini = inst_begin(*f->fun), ine = inst_end(*f->fun); ini != ine; ++ini) {
          Instruction *in = &*ini;
          CallSite cs(in);
          CalledFunctionTy *ct = cm->getCalledFunction(in, true);
          if (cs) {
            assert(ct);

            Function *t = cs.getCalledFunction();
            if (cm->isAllocating(t)) {
              called.insert(ct);
            }
            if (cm->isPossibleAllocator(t)) {
              wrapped.insert(ct);
            }
          }
        }
        return;
      }
      
      // process a single basic block
      // FIXME: phi nodes
      
      for(BasicBlock::iterator in = s.bb->begin(), ine = s.bb->end(); in != ine; ++in) {
        msg.trace("visiting", in);
   
        handleIntGuardsForNonTerminator(in, intGuardVarsCache, s.intGuards, msg);
        handleSEXPGuardsForNonTerminator(in, sexpGuardVarsCache, s.sexpGuards, cm->getGlobals(), f->argInfo, cm->getSymbolsMap(), msg, NULL);
        
        // handle stores
        if (trackOrigins && StoreInst::classof(in)) {
          StoreInst *st = cast<StoreInst>(in);
          
          if (AllocaInst::classof(st->getPointerOperand())) {
            AllocaInst *dst = cast<AllocaInst>(st->getPointerOperand());
            if (possiblyReturnedVars.find(dst) != possiblyReturnedVars.end()) {
            
              // dst is a variable to be tracked
              if (LoadInst::classof(st->getValueOperand())) {
                Value *src = cast<LoadInst>(st->getValueOperand())->getPointerOperand();
                if (AllocaInst::classof(src)) {
                  // copy all var origins of src into dst
                  if (msg.debug()) msg.debug("propagating origins on assignment of " + varName(cast<AllocaInst>(src)) + " to " + varName(dst), in); 
                  auto sorig = s.varOrigins.find(cast<AllocaInst>(src));
                  if (sorig != s.varOrigins.end()) {
                    CalledFunctionsOrderedSetTy& srcOrigs = sorig->second;
                    
                    auto dorig = s.varOrigins.find(dst);
                    if (dorig == s.varOrigins.end()) {
                      s.varOrigins.insert({dst, srcOrigs}); // set origins
                    } else {
                      CalledFunctionsOrderedSetTy& dstOrigs = dorig->second;
                      dstOrigs.insert(srcOrigs.begin(), srcOrigs.end()); // copy origins
                    }
                  }
                }
                continue;
              }
              CalledFunctionTy *tgt = cm->getCalledFunction(st->getValueOperand(), &s.sexpGuards, true);
              if (tgt && cm->isPossibleAllocator(tgt->fun)) {
                // storing a value gotten from a (possibly allocating) function
                if (msg.debug()) msg.debug("adding origin " + tgt->getName() + " of " + varName(dst), in); 
                auto orig = s.varOrigins.find(dst);
                if (orig == s.varOrigins.end()) {
                  CalledFunctionsOrderedSetTy newOrigins;
                  newOrigins.insert(tgt);
                  s.varOrigins.insert({dst, newOrigins});
                } else {
                  CalledFunctionsOrderedSetTy& existingOrigins = orig->second;
                  existingOrigins.insert(tgt);
                }
                continue;
              }
            }
          }
        }
        
        // handle calls
        CalledFunctionTy *tgt = cm->getCalledFunction(in, &s.sexpGuards, true);
        if (tgt && cm->isAllocating(tgt->fun)) {
          msg.debug("recording call to " + tgt->getName(), in); 
          s.called.insert(tgt);
        }
      }
      
      TerminatorInst *t = s.bb->getTerminator();
      
      if (ReturnInst::classof(t)) { // handle return statement

        if (msg.debug()) msg.debug("collecting " + std::to_string(s.called.size()) + " calls at function return", t);
        called.insert(s.called.begin(), s.called.end());      

        if (trackOrigins) {
          if (s.called.find(cm->getCalledGCFunction()) != s.called.end()) {
            // the GC function is an exception
            //   even though it does not return SEXP, any function that calls it and returns an SEXP is regarded as wrapping it
            //   (this is a heuristic)
            wrapped.insert(cm->getCalledGCFunction());
          }
          Value *returnOperand = cast<ReturnInst>(t)->getReturnValue();
          if (LoadInst::classof(returnOperand)) { // return(var)
            Value *src = cast<LoadInst>(returnOperand)->getPointerOperand();
            if (AllocaInst::classof(src)) {
              
              auto origins = s.varOrigins.find(cast<AllocaInst>(src));
              size_t nOrigins = 0;
              if (origins != s.varOrigins.end()) {
                CalledFunctionsOrderedSetTy& knownOrigins = origins->second;
                wrapped.insert(knownOrigins.begin(), knownOrigins.end()); // copy origins as result
                nOrigins = knownOrigins.size();
              }
              if (msg.debug()) msg.debug("collecting " + std::to_string(nOrigins) + " at function return, variable " + varName(cast<AllocaInst>(src)), t); 
            }
          }
          CalledFunctionTy *tgt = cm->getCalledFunction(returnOperand, &s.sexpGuards, true);
          if (tgt && cm->isPossibleAllocator(tgt->fun)) { // return(foo())
            msg.debug("collecting immediate origin " + tgt->getName() + " at function return", t); 
           wrapped.insert(tgt);
          }   
        }
      }

      if (handleSEXPGuardsForTerminator(t, sexpGuardVarsCache, s, cm->getGlobals(), f->argInfo, cm->getSymbolsMap(), msg)) {
        continue;
      }

      if (handleIntGuardsForTerminator(t, intGuardVarsCache, s, msg)) {
        continue;
      }
      
      // add conservatively all cfg successors
      for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
        BasicBlock *succ = t->getSuccessor(i);
        {
          CAllocStateTy* state = s.clone(succ);
          if (state->add()) {
          msg.trace("added successor of", t);
          }
        }
      }
    }
}

typedef std::vector<std::vector<bool>> BoolMatrixTy;
typedef std::vector<unsigned> AdjacencyListRow;
typedef std::vector<AdjacencyListRow> AdjacencyListTy;

static void resize(AdjacencyListTy& list, unsigned n) {
  list.resize(n);
}

static void resize(BoolMatrixTy& matrix, unsigned n) {
  unsigned oldn = matrix.size();
  if (n <= oldn) {
    return;
  }
  matrix.resize(n);
  for (int i = 0; i < n; i++) {
    matrix[i].resize(n);
  }
}

static void buildClosure(BoolMatrixTy& mat, AdjacencyListTy& list, unsigned n) {

  bool added = true;
  while(added) {
    added = false;
    
    for(unsigned i = 0; i < n; i++) {
      for(unsigned jidx = 0; jidx < list[i].size(); jidx++) {
        unsigned j = list[i][jidx];
        if (i == j) {
          continue;
        }
        for(unsigned kidx = 0; kidx < list[j].size(); kidx++) {
          unsigned k = list[j][kidx];
          if (j == k) {
            continue;
          }
          if (!mat[i][k]) {
            mat[i][k] = true;
            list[i].push_back(k);
            added = true;
          }
        }
      }
    }
  }
}

void CalledModuleTy::computeCalledAllocators() {

  // find calls and variable origins for each called function
  // then create a "callgraph" out of these
  // and then compute call graph closure
  //
  // for performance, restrict variable origins to possible allocators
  // and restrict calls to possibly allocating functions
  
  if (possibleCAllocators && allocatingCFunctions) {
    return;
  }
  
  possibleCAllocators = new CalledFunctionsSetTy();
  allocatingCFunctions = new CalledFunctionsSetTy();
  
  LineMessenger msg(m->getContext(), DEBUG, TRACE, UNIQUE_MSG);
  
  unsigned nfuncs = calledFunctionsVector.size(); // NOTE: nfuncs can increase during the checking

  BoolMatrixTy callsMat(nfuncs, std::vector<bool>(nfuncs));  // calls[i][j] - function i calls function j
  AdjacencyListTy callsList(nfuncs, AdjacencyListRow()); // calls[i] - list of functions called by i
  BoolMatrixTy wrapsMat(nfuncs, std::vector<bool>(nfuncs));  // wraps[i][j] - function i wraps function j
  AdjacencyListTy wrapsList(nfuncs, AdjacencyListRow()); // wraps[i] - list of functions wrapped by i
  
  for(unsigned i = 0; i < calledFunctionsVector.size(); i++) {

    CalledFunctionTy *f = calledFunctionsVector[i];
    if (!f->fun || !f->fun->size()) {
      continue;
    }
    
    CalledFunctionsOrderedSetTy called;
    CalledFunctionsOrderedSetTy wrapped;
    getCalledAndWrappedFunctions(f, msg, called, wrapped);
    
    if (DEBUG && called.size()) {
      errs() << "\nDetected (possible allocators) called by function " << f->getName() << ":\n";
      for(CalledFunctionsOrderedSetTy::iterator cfi = called.begin(), cfe = called.end(); cfi != cfe; ++cfi) {
        CalledFunctionTy *cf = *cfi;
        errs() << "   " << cf->getName() << "\n";
      }
    }
    if (DEBUG && wrapped.size()) {
      errs() << "\nDetected (possible allocators) wrapped by function " << f->getName() << ":\n";
      for(CalledFunctionsOrderedSetTy::iterator cfi = wrapped.begin(), cfe = wrapped.end(); cfi != cfe; ++cfi) {
        CalledFunctionTy *cf = *cfi;
        errs() << "   " << cf->getName() << "\n";
      }
    }
    
    nfuncs = calledFunctionsVector.size(); // get the current size
    resize(callsList, nfuncs);
    resize(wrapsList, nfuncs);
    resize(callsMat, nfuncs);
    resize(wrapsMat, nfuncs);
    
    for(CalledFunctionsOrderedSetTy::iterator cfi = called.begin(), cfe = called.end(); cfi != cfe; ++cfi) {
      CalledFunctionTy *cf = *cfi;
      callsMat[f->idx][cf->idx] = true;
      callsList[f->idx].push_back(cf->idx);
    }

    for(CalledFunctionsOrderedSetTy::iterator wfi = wrapped.begin(), wfe = wrapped.end(); wfi != wfe; ++wfi) {
      CalledFunctionTy *wf = *wfi;
      wrapsMat[f->idx][wf->idx] = true;
      wrapsList[f->idx].push_back(wf->idx);
    }    
  }
  
  // calculate transitive closure

  buildClosure(callsMat, callsList, nfuncs);
  buildClosure(wrapsMat, wrapsList, nfuncs);
  
  // fill in results
  
  unsigned gcidx = gcFunction->idx;
  for(unsigned i = 0; i < nfuncs; i++) {
    if (callsMat[i][gcidx]) {
      allocatingCFunctions->insert(getCalledFunction(i));
    }
    if (wrapsMat[i][gcidx]) {
      possibleCAllocators->insert(getCalledFunction(i));
    }    
  }
  allocatingCFunctions->insert(gcFunction);
  possibleCAllocators->insert(gcFunction);
}
