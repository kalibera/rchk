
#include "callocators.h"
#include "errors.h"
#include "guards.h"
#include "symbols.h"
#include "linemsg.h"
#include "state.h"
#include "table.h"
#include "exceptions.h"
#include "patterns.h"

#include <map>
#include <stack>
#include <unordered_set>

#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

  // FIXME: could reduce copy-paste vs. bcheck?
const bool DEBUG = false;
const bool TRACE = false;
const bool UNIQUE_MSG = true;
const int MAX_STATES = CALLOCATORS_MAX_STATES;
const bool VERBOSE_DUMP = false;

const bool DUMP_STATES = false;
const std::string DUMP_STATES_FUNCTION = "XXXX"; // only dump states in this function
const bool ONLY_CHECK_ONLY_FUNCTION = false; // only check one function (named ONLY_FUNCTION_NAME)
const std::string ONLY_FUNCTION_NAME = "XXXX";
const bool ONLY_DEBUG_ONLY_FUNCTION = true;
const bool ONLY_TRACE_ONLY_FUNCTION = true;

const bool KEEP_CALLED_IN_STATE = false;

bool CalledFunctionTy::hasContext() const {
  if (!argInfo) {
    return false;
  }
  
  for(ArgInfosVectorTy::const_iterator ai = argInfo->begin(), ae = argInfo->end(); ai != ae; ++ai) {
    const ArgInfoTy *a = *ai;
    if (a && (a->isSymbol() || a->isVector())) {
      return true;
    }
  }
  
  return false;
}

std::string CalledFunctionTy::getNameSuffix() const {

  std::string suff;
  unsigned nKnown = 0;
  
  if (!argInfo) {
    return std::string();
  }

  for(ArgInfosVectorTy::const_iterator ai = argInfo->begin(), ae = argInfo->end(); ai != ae; ++ai) {
    const ArgInfoTy *a = *ai;
    if (ai != argInfo->begin()) {
      suff += ",";
    }
    if (a && a->isSymbol()) {
      suff += "S:" + static_cast<const SymbolArgInfoTy*>(a)->symbolName;
      nKnown++;
    } else if (a && a->isVector()) {
      suff += "V";
      nKnown++;
    } else {
      suff += "?";
    }
  }
  
  if (nKnown > 0) {
    return "(" + suff + ")";
  }
  return std::string();
}


std::string CalledFunctionTy::getName() const {
  return fun->getName().str() + getNameSuffix();
}

size_t CalledFunctionTy_hash::operator()(const CalledFunctionTy& t) const {
  size_t res = 0;
  hash_combine(res, t.fun);
  hash_combine(res, t.argInfo); // argInfos are interned
  return res;
}

bool CalledFunctionTy_equal::operator() (const CalledFunctionTy& lhs, const CalledFunctionTy& rhs) const {
  return lhs.fun == rhs.fun && lhs.argInfo == rhs.argInfo && lhs.module == rhs.module;  // argInfos are interned
}

SymbolArgInfoTy::SymbolArgInfoTableTy SymbolArgInfoTy::table;

size_t ArgInfosVectorTy_hash::operator()(const ArgInfosVectorTy& t) const {
  size_t res = 0;
  hash_combine(res, t.size());
  
  size_t cntSym = 0;
  size_t cntVec = 0;
  for(ArgInfosVectorTy::const_iterator ai = t.begin(), ae = t.end(); ai != ae; ++ai) {
    const ArgInfoTy *a = *ai;
    if (a && a->isSymbol()) {
      hash_combine(res, static_cast<const SymbolArgInfoTy*>(a)->symbolName);
      cntSym++;
    } else if (a && a->isVector()) {
      hash_combine(res, true);
      cntVec++;
    }
  }
  hash_combine(res, cntSym);
  hash_combine(res, cntVec);
  return res;
}

const CalledFunctionTy* CalledModuleTy::getCalledFunction(Function *f) {
  size_t nargs = f->arg_size();
  ArgInfosVectorTy argInfos(nargs, NULL);
  CalledFunctionTy calledFunction(f, intern(argInfos), this);
  return intern(calledFunction);
}

const CalledFunctionTy* CalledModuleTy::getCalledFunction(Value *inst, bool registerCallSite) {
  return getCalledFunction(inst, NULL, NULL, registerCallSite);
}

const CalledFunctionTy* CalledModuleTy::getCalledFunction(Value *inst, SEXPGuardsChecker* sexpGuardsChecker, SEXPGuardsTy *sexpGuards, bool registerCallSite) {
  // FIXME: this is quite inefficient, does a lot of allocation
  
  if (!CallBase::classof(inst)) {
    return NULL;
  }
  CallBase *cs = cast<CallBase>(inst);
  Function *fun = cs->getCalledFunction();
  if (!fun) {
    return NULL;
  }
      
  // build arginfo
      
  unsigned nargs = cs->arg_size();
  ArgInfosVectorTy argInfo(nargs, NULL);

  for(unsigned i = 0; i < nargs; i++) {
    Value *arg = cs->getArgOperand(i);
    if (LoadInst::classof(arg)) { // R_XSymbol
      Value *src = cast<LoadInst>(arg)->getPointerOperand();
      if (GlobalVariable::classof(src)) {
        auto ssearch = symbolsMap->find(cast<GlobalVariable>(src));
        if (ssearch != symbolsMap->end()) {
          argInfo[i] = SymbolArgInfoTy::create(ssearch->second);
          continue;
        }
      }
      if (sexpGuards && sexpGuardsChecker && AllocaInst::classof(src)) {
        AllocaInst *var = cast<AllocaInst>(src);
          
        std::string symbolName;
        SEXPGuardState gs = sexpGuardsChecker->getGuardState(*sexpGuards, var, symbolName);
        if (gs == SGS_SYMBOL) {
          argInfo[i] = SymbolArgInfoTy::create(symbolName);
          continue;
        }
        if (gs == SGS_VECTOR) {
          argInfo[i] = VectorArgInfoTy::get();
          continue;
        }
      }
    }
    std::string symbolName;  // install("X")
    if (isInstallConstantCall(arg, symbolName)) {
      argInfo[i] = SymbolArgInfoTy::create(symbolName);
      continue;
    }
    if (isVectorProducingCall(arg, this, sexpGuardsChecker, sexpGuards)) {
      argInfo[i] = VectorArgInfoTy::get();
      continue;
    }
    // not a symbol, leave argInfo as NULL
  }
      
  CalledFunctionTy calledFunction(fun, intern(argInfo), this);
  const CalledFunctionTy* cf = intern(calledFunction);
  
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
  callSiteTargets(), vrfState(NULL), gcFunction(getCalledFunction(getGCFunction(m)))  {

  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *fun = &*fi;

    myassert(fun);
    getCalledFunction(fun); // make sure each function has a called function counterpart
    for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
      User *u = *ui;
      getCalledFunction(cast<Value>(u)); // NOTE: this only gets contexts that are constant, more are gotten during allocators computation
    }
  }
    // only compute on demand - it takes a bit of time
  possibleCAllocators = NULL;
  allocatingCFunctions = NULL;
  contextSensitivePossibleAllocators = NULL;
  contextSensitiveAllocatingFunctions = NULL;
}

CalledModuleTy::~CalledModuleTy() {

  if (possibleCAllocators) {
    delete possibleCAllocators;
  }
  if (allocatingCFunctions) {
    delete allocatingCFunctions;
  }
  if (contextSensitiveAllocatingFunctions) {
    delete contextSensitiveAllocatingFunctions;
  }
  if (contextSensitivePossibleAllocators) {
    delete contextSensitivePossibleAllocators;
  }
  if (vrfState) {
    freeVrfState(vrfState);
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

typedef std::map<AllocaInst*,const CalledFunctionsOrderedSetTy*> InternedVarOriginsTy;
typedef std::map<AllocaInst*,CalledFunctionsOrderedSetTy> VarOriginsTy; // uninterned

  // for a local variable, a list of functions whose return values may have
  // been assigned, possibly indirectly, to that variable

struct CalledFunctionsOSTableTy_hash {
  size_t operator()(const CalledFunctionsOrderedSetTy& t) const {
    size_t res = 0;
    hash_combine(res, t.size());
        
    for(CalledFunctionsOrderedSetTy::const_iterator fi = t.begin(), fe = t.end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      hash_combine(res, (const void *) f);
    } // ordered set
    return res;
  }
};

typedef InterningTable<CalledFunctionsOrderedSetTy, CalledFunctionsOSTableTy_hash> CalledFunctionsOSTableTy;

struct CAllocStateTy;

struct CAllocPackedStateTy : public PackedStateWithGuardsTy {
  const size_t hashcode;
  const CalledFunctionsOrderedSetTy *called;
  const InternedVarOriginsTy varOrigins;
  
  
  CAllocPackedStateTy(size_t hashcode, BasicBlock* bb, const PackedIntGuardsTy& intGuards, const PackedSEXPGuardsTy& sexpGuards,
    const InternedVarOriginsTy& varOrigins, const CalledFunctionsOrderedSetTy *called):
    
    PackedStateBaseTy(bb), PackedStateWithGuardsTy(bb, intGuards, sexpGuards), hashcode(hashcode), called(called), varOrigins(varOrigins)  {};
    
  static CAllocPackedStateTy create(CAllocStateTy& us, IntGuardsChecker& intGuardsChecker, SEXPGuardsChecker& sexpGuardsChecker);
};

static VarOriginsTy unpackVarOrigins(const InternedVarOriginsTy& internedOrigins) {

  VarOriginsTy varOrigins;

  for(InternedVarOriginsTy::const_iterator oi = internedOrigins.begin(), oe = internedOrigins.end(); oi != oe; ++oi) {
    AllocaInst* var = oi->first;
    const CalledFunctionsOrderedSetTy* srcs = oi->second;
    varOrigins.insert({var, *srcs});
  }
  
  return varOrigins;
}

static CalledFunctionsOSTableTy osTable; // interned ordered sets

static InternedVarOriginsTy packVarOrigins(const VarOriginsTy& varOrigins) {

  InternedVarOriginsTy internedOrigins;

  for(VarOriginsTy::const_iterator oi = varOrigins.begin(), oe = varOrigins.end(); oi != oe; ++oi) {
    AllocaInst* var = oi->first;
    const CalledFunctionsOrderedSetTy& srcs = oi->second;
    internedOrigins.insert({var, osTable.intern(srcs)});
  }
  
  return internedOrigins;
}

struct CAllocStateTy : public StateWithGuardsTy {
  CalledFunctionsOrderedSetTy called;
  VarOriginsTy varOrigins;
  
  CAllocStateTy(const CAllocPackedStateTy& ps, IntGuardsChecker& intGuardsChecker, SEXPGuardsChecker& sexpGuardsChecker):
    CAllocStateTy(ps.bb, intGuardsChecker.unpack(ps.intGuards), sexpGuardsChecker.unpack(ps.sexpGuards), *ps.called, unpackVarOrigins(ps.varOrigins)) {};

  CAllocStateTy(BasicBlock *bb): StateBaseTy(bb), StateWithGuardsTy(bb), called(), varOrigins() {};

  CAllocStateTy(BasicBlock *bb, const IntGuardsTy& intGuards, const SEXPGuardsTy& sexpGuards, const CalledFunctionsOrderedSetTy& called, const VarOriginsTy& varOrigins):
    StateBaseTy(bb), StateWithGuardsTy(bb, intGuards, sexpGuards), called(called), varOrigins(varOrigins) {};
      
  virtual CAllocStateTy* clone(BasicBlock *newBB) {
    return new CAllocStateTy(newBB, intGuards, sexpGuards, called, varOrigins);
  }
    
  void dump(std::string dumpMsg) {
    StateBaseTy::dump(VERBOSE_DUMP);
    StateWithGuardsTy::dump(VERBOSE_DUMP);

    if (KEEP_CALLED_IN_STATE) {
      errs() << "=== called (allocating):\n";
      for(CalledFunctionsOrderedSetTy::iterator fi = called.begin(), fe = called.end(); fi != fe; *fi++) {
        const CalledFunctionTy* f = *fi;
        errs() << "   " << funName(f) << "\n";
      }
    }
    errs() << "=== origins (allocators):\n";
    for(VarOriginsTy::const_iterator oi = varOrigins.begin(), oe = varOrigins.end(); oi != oe; ++oi) {
      AllocaInst* var = oi->first;
      const CalledFunctionsOrderedSetTy& srcs = oi->second;

      errs() << "   " << varName(var) << ":";
        
      for(CalledFunctionsOrderedSetTy::const_iterator fi = srcs.begin(), fe = srcs.end(); fi != fe; ++fi) {
        const CalledFunctionTy *f = *fi;
        errs() << " " << funName(f);
      }
      errs() << "\n";
    }
    errs() << " ######################" << dumpMsg << "######################\n";
  }
    
  virtual bool add();
};


CAllocPackedStateTy CAllocPackedStateTy::create(CAllocStateTy& us, IntGuardsChecker& intGuardsChecker, SEXPGuardsChecker& sexpGuardsChecker) {

  InternedVarOriginsTy internedOrigins = packVarOrigins(us.varOrigins);
   
  size_t res = 0;
  hash_combine(res, us.bb);
  intGuardsChecker.hash(res, us.intGuards);
  sexpGuardsChecker.hash(res, us.sexpGuards);
    
  hash_combine(res, internedOrigins.size());
  for(InternedVarOriginsTy::const_iterator oi = internedOrigins.begin(), oe = internedOrigins.end(); oi != oe; ++oi) {
    //AllocaInst* var = oi->first;
    const CalledFunctionsOrderedSetTy* srcs = oi->second;
    hash_combine(res, (const void *)srcs); // interned
  } // ordered map
    
  return CAllocPackedStateTy(res, us.bb, intGuardsChecker.pack(us.intGuards), sexpGuardsChecker.pack(us.sexpGuards), internedOrigins, osTable.intern(us.called));
}
  
// the hashcode is cached at the time of first hashing
//   (and indeed is not copied)

struct CAllocPackedStateTy_hash {
  size_t operator()(const CAllocPackedStateTy& t) const {
    return t.hashcode;
  }
};

struct CAllocPackedStateTy_equal {
  bool operator() (const CAllocPackedStateTy& lhs, const CAllocPackedStateTy& rhs) const {
    return lhs.bb == rhs.bb && lhs.intGuards == rhs.intGuards && lhs.sexpGuards == rhs.sexpGuards && 
      lhs.varOrigins == rhs.varOrigins;
  }
};

typedef std::stack<const CAllocPackedStateTy*> WorkListTy;
typedef std::unordered_set<CAllocPackedStateTy, CAllocPackedStateTy_hash, CAllocPackedStateTy_equal> DoneSetTy;

static WorkListTy workList; // FIXME: avoid these "globals"
static DoneSetTy doneSet;   // FIXME: avoid these "globals"

static IntGuardsChecker* intGuardsChecker; // FIXME: avoid these "globals"
static SEXPGuardsChecker* sexpGuardsChecker; // FIXME: avoid these "globals"


bool CAllocStateTy::add() {

  CAllocPackedStateTy ps = CAllocPackedStateTy::create(*this, *intGuardsChecker, *sexpGuardsChecker);
  delete this; // NOTE: state suicide
  auto sinsert = doneSet.insert(ps);
  if (sinsert.second) {
    const CAllocPackedStateTy* insertedState = &*sinsert.first;
    workList.push(insertedState); // make the worklist point to the doneset
    return true;
  } else {
    return false;
  }
}

static void clearStates() { // FIXME: avoid copy paste (vs. bcheck)
  // clear the worklist and the doneset
  doneSet.clear();
  WorkListTy empty;
  std::swap(workList, empty);
  osTable.clear();
}

static void getCalledAndWrappedFunctions(const CalledFunctionTy *f, LineMessenger& msg, 
  CalledFunctionsOrderedSetTy& called, CalledFunctionsOrderedSetTy& wrapped) {

  static const CalledFunctionTy* const externalFunctionMarker = new CalledFunctionTy(NULL, NULL, NULL);
  
  if (!f->fun || !f->fun->size()) {
    return;
  }
  CalledModuleTy *cm = f->module;
    
  VarBoolCacheTy sexpGuardVarsCache;

  BasicBlocksSetTy errorBasicBlocks;
  findErrorBasicBlocks(f->fun, cm->getErrorFunctions(), errorBasicBlocks); // FIXME: this could be remembered in CalledFunction
    
  VarsSetTy possiblyReturnedVars; 
  findPossiblyReturnedVariables(f->fun, possiblyReturnedVars); // to restrict origin tracking
    
  bool trackOrigins = isSEXP(f->fun->getReturnType());
    
  if (DEBUG && ONLY_DEBUG_ONLY_FUNCTION) {
    if (ONLY_FUNCTION_NAME == funName(f)) {
      msg.debug(true);
    } else {
      msg.debug(false);
    }
  }

  if (TRACE && ONLY_TRACE_ONLY_FUNCTION) {
    if (ONLY_FUNCTION_NAME == funName(f)) {
      msg.trace(true);
    } else {
      msg.trace(false);
    }
  }
    
  clearStates();
  
  msg.newFunction(f->fun, " - " + funName(f));
  intGuardsChecker = new IntGuardsChecker(&msg);
  sexpGuardsChecker = new SEXPGuardsChecker(&msg, cm->getGlobals(), NULL /* possible allocators */, cm->getSymbolsMap(), f->argInfo, cm->getVrfState(), cm);
  
  bool intGuardsEnabled = !avoidIntGuardsFor(f);
  bool sexpGuardsEnabled = !avoidSEXPGuardsFor(f);
  
  {
    CAllocStateTy* initState = new CAllocStateTy(&f->fun->getEntryBlock());
    initState->add();
  }
  
  while(!workList.empty()) {
    CAllocStateTy s(*workList.top(), *intGuardsChecker, *sexpGuardsChecker); // unpacks the state
    workList.pop();    

    if (DUMP_STATES && (DUMP_STATES_FUNCTION.empty() || DUMP_STATES_FUNCTION == f->getName())) {
      msg.trace("going to work on this state:", &*s.bb->begin());
      s.dump("worklist top");
    }    

    if (ONLY_CHECK_ONLY_FUNCTION && ONLY_FUNCTION_NAME != f->getName()) {
      continue;
    }      

    if (errorBasicBlocks.find(s.bb) != errorBasicBlocks.end()) {
      msg.debug("ignoring basic block on error path", &*s.bb->begin());
      continue;
    }
      
    if (doneSet.size() > MAX_STATES) {
      errs() << "ERROR: too many states (abstraction error?) in function " << funName(f) << "\n";
      clearStates();
      delete intGuardsChecker;
      delete sexpGuardsChecker;
      
      if (called.erase(externalFunctionMarker) > 0) {
        // the functions calls an external function
        // lets assume conservatively such function may allocate
        called.insert(cm->getCalledGCFunction());
      }      
        
      // NOTE: some callsites may have already been registered to more specific called functions
      bool originAllocating = cm->isAllocating(f->fun);
      bool originAllocator = cm->isPossibleAllocator(f->fun);
        
      if (!originAllocating && !originAllocator) {
        return;
      }
      for(inst_iterator ini = inst_begin(*f->fun), ine = inst_end(*f->fun); ini != ine; ++ini) {
        Instruction *in = &*ini;
          
        if (errorBasicBlocks.find(in->getParent()) != errorBasicBlocks.end()) {
          continue;
        }
        if (isCallThroughPointer(in)) {
          if (originAllocating) {
            called.insert(cm->getCalledGCFunction());
          }
          if (originAllocator) {
            wrapped.insert(cm->getCalledGCFunction());
          }
          continue;
        }
        const CalledFunctionTy *ct = cm->getCalledFunction(in, true);
        if (CallBase::classof(in)) {
          CallBase *cs = cast<CallBase>(in);
          Function *t = cs->getCalledFunction();
          if (t && ct) {
            // t/ct can be NULL
            // well it could when CallSite was used, LLVM returned NULL for cs.getCalledFunction() for some internal calls
          
            // note that this is a heuristic, best-effort approach that is not equivalent to what allocators.cpp do
            //   this heuristic may treat a function as wrapped even when allocators.cpp will not
            //
            // on the other hand, we may discover that a call is in a context that makes it non-allocating/non-allocator
            // it would perhaps be cleaner to re-use the context-insensitive algorithm here
            // or just improve performance so that we don't run out of states in the first place
            if (originAllocating && cm->isAllocating(t)) {
              called.insert(ct);
            }
            if (originAllocator && cm->isPossibleAllocator(t)) {
              wrapped.insert(ct);
            }
          }
        }
      }
      return;
    }
      
    // process a single basic block
    // FIXME: phi nodes
      
    for(BasicBlock::iterator ini = s.bb->begin(), ine = s.bb->end(); ini != ine; ++ini) {
      Instruction *in = &*ini;
      msg.trace("visiting", in);
   
      if (intGuardsEnabled) {
        intGuardsChecker->handleForNonTerminator(in, s.intGuards);
      }
      if (sexpGuardsEnabled) {
        sexpGuardsChecker->handleForNonTerminator(in, s.sexpGuards);
      }
        
      // handle stores
      if (trackOrigins && StoreInst::classof(in)) {
        StoreInst *st = cast<StoreInst>(in);
          
        if (AllocaInst::classof(st->getPointerOperand())) {
          AllocaInst *dst = cast<AllocaInst>(st->getPointerOperand());
          if (possiblyReturnedVars.find(dst) != possiblyReturnedVars.end() && isSEXP(dst)) {
            
            // FIXME: should also handle phi nodes here, currently we may miss some allocators
            if (msg.debug()) msg.debug("dropping origins of " + varName(dst) + " at variable overwrite", in);
            s.varOrigins.erase(dst);
            
            ValuesSetTy vorig = valueOrigins(st->getValueOperand()); // this goes through Phi's and macros like CDR, CAR etc
            for(ValuesSetTy::iterator vi = vorig.begin(), ve = vorig.end(); vi != ve; ++vi) { 
              Value *v = *vi;
            
              if (CallBase::classof(v)) {
                CallBase *cs = cast<CallBase>(v);
                Function *tgt = cs->getCalledFunction();
                if (tgt && (tgt->getName() == "Rf_protect" || tgt->getName() == "Rf_protectWithIndex")) {
                  if (msg.debug()) msg.debug("propagating origins through PROTECT/PROTECT_WITH_INDEX to " + varName(dst), in);
                  v = cs->getArgOperand(0);
                }
              }
              if (AllocaInst* src = dyn_cast<AllocaInst>(v)) {
                if (isSEXP(src)) {
                  // copy all var origins of src into dst
                  if (msg.debug()) msg.debug("propagating origins on assignment of " + varName(src) + " to " + varName(dst), in); 
                  auto sorig = s.varOrigins.find(src);
                  if (sorig != s.varOrigins.end()) {
                    CalledFunctionsOrderedSetTy& srcOrigs = sorig->second;
                    s.varOrigins.insert({dst, srcOrigs}); // set (copy) origins
                  }
                  continue;
                }
              }
            
              const CalledFunctionTy *tgt;
              if (isCallThroughPointer(v)) {
                // a function called through a pointer may be e.g. a builtin function, and indeed may be an allocator
                if (msg.debug())
                  msg.debug("call through a pointer, asserting it may be allocating (marking as call to gc function) - assigned to " + varName(dst), dyn_cast<Instruction>(v));
                tgt = cm->getCalledGCFunction();
              } else {
                tgt = cm->getCalledFunction(v, sexpGuardsChecker, &s.sexpGuards, true);
                if (tgt && !cm->isPossibleAllocator(tgt->fun)) {
                  tgt = NULL;
                }
              }
              if (tgt) {
                // storing a value gotten from a (possibly allocator) function
                if (msg.debug()) msg.debug("setting origin " + funName(tgt) + " of " + varName(dst), in); 
                CalledFunctionsOrderedSetTy newOrigins;
                newOrigins.insert(tgt);
                s.varOrigins.insert({dst, newOrigins});
                continue;
              }
            }
          }
        }
      }
        
      // handle calls
      const CalledFunctionTy *tgt;
      
      if (isCallThroughPointer(in)) {
        if (msg.debug()) msg.debug("call through a pointer, using the external function marker", in);
        tgt = externalFunctionMarker;
      } else {
        tgt = cm->getCalledFunction(in, sexpGuardsChecker, &s.sexpGuards, true);
        if (tgt && !cm->isAllocating(tgt->fun)) {
          tgt = NULL;
        }
      }
      
      if (tgt) {
        if (msg.debug()) msg.debug("recording call to " + funName(tgt), in);
          
        if (KEEP_CALLED_IN_STATE) {  
          if (called.find(tgt) == called.end()) { // if we already know the function is called, don't add, save memory
            s.called.insert(tgt);
          }
        } else {
          called.insert(tgt);
        }
      }
    }
      
    TerminatorInst *t = s.bb->getTerminator();
      
    if (ReturnInst::classof(t)) { // handle return statement

      if (KEEP_CALLED_IN_STATE) {
        if (msg.debug()) msg.debug("collecting " + std::to_string(s.called.size()) + " calls at function return", t);
        called.insert(s.called.begin(), s.called.end());
      }

      if (trackOrigins) {
        Value *returnOperand = cast<ReturnInst>(t)->getReturnValue();
        ValuesSetTy vorig = valueOrigins(returnOperand); // this goes through Phi's and macros like CDR, CAR etc
        for(ValuesSetTy::iterator vi = vorig.begin(), ve = vorig.end(); vi != ve; ++vi) { 
          Value *v = *vi;

          if (AllocaInst *src = dyn_cast<AllocaInst>(v)) {
            if (isSEXP(src)) {
              auto origins = s.varOrigins.find(src);
              size_t nOrigins = 0;
              if (origins != s.varOrigins.end()) {
                CalledFunctionsOrderedSetTy& knownOrigins = origins->second;
                wrapped.insert(knownOrigins.begin(), knownOrigins.end()); // copy origins as result
                nOrigins = knownOrigins.size();
              }
              if (msg.debug()) msg.debug("collecting " + std::to_string(nOrigins) + " at function return, variable " + varName(src), t);
              if (msg.debug() && origins != s.varOrigins.end()) {
                std::string tmp = "tracked origins included:";
                CalledFunctionsOrderedSetTy& knownOrigins = origins->second;
                for(CalledFunctionsOrderedSetTy::iterator oi = knownOrigins.begin(), oe = knownOrigins.end(); oi != oe; ++oi) {
                  const CalledFunctionTy* cf = *oi;
                  tmp += " ";
                  tmp += funName(cf);
                }
                msg.debug(tmp, t);
              }
              continue;
            }
          }
          const CalledFunctionTy *tgt;
      
          if (isCallThroughPointer(returnOperand)) {
            if (msg.debug()) msg.debug("returning value from external function, asserting it is from gc function", t);
            tgt = cm->getCalledGCFunction();
          } else {
            tgt = cm->getCalledFunction(returnOperand, sexpGuardsChecker, &s.sexpGuards, true);
            if (tgt && !cm->isPossibleAllocator(tgt->fun)) {
              tgt = NULL;
            }
          }
          if (tgt) { // return(foo())
            if (msg.debug()) msg.debug("collecting immediate origin " + funName(tgt) + " at function return", t); 
            wrapped.insert(tgt);
          }
        }
      }
    }

    if (sexpGuardsEnabled && sexpGuardsChecker->handleForTerminator(t, s)) {
      continue;
    }

    if (intGuardsEnabled && intGuardsChecker->handleForTerminator(t, s)) {
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
  clearStates();
  delete intGuardsChecker;
  delete sexpGuardsChecker;
  
  if (trackOrigins && called.find(cm->getCalledGCFunction()) != called.end()) {
    // the GC function is an exception
    //   even though it does not return SEXP, any function that calls it and returns an SEXP is regarded as wrapping it
    //   (this is a heuristic)
    wrapped.insert(cm->getCalledGCFunction());
  }
  if (called.erase(externalFunctionMarker) > 0) {
    // the functions calls an external function
    // lets assume conservatively such function may allocate
    called.insert(cm->getCalledGCFunction());
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
  for (unsigned i = 0; i < n; i++) {
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
  
  unsigned nfuncs = getNumberOfCalledFunctions(); // NOTE: nfuncs can increase during the checking

  BoolMatrixTy callsMat(nfuncs, std::vector<bool>(nfuncs));  // calls[i][j] - function i calls function j
  AdjacencyListTy callsList(nfuncs, AdjacencyListRow()); // calls[i] - list of functions called by i
  BoolMatrixTy wrapsMat(nfuncs, std::vector<bool>(nfuncs));  // wraps[i][j] - function i wraps function j
  AdjacencyListTy wrapsList(nfuncs, AdjacencyListRow()); // wraps[i] - list of functions wrapped by i
  
  for(unsigned i = 0; i < getNumberOfCalledFunctions(); i++) {

    const CalledFunctionTy *f = getCalledFunction(i);
    if (!f->fun || !f->fun->size() || !isAllocating(f->fun)) {
      continue;
    }
    
    CalledFunctionsOrderedSetTy called;
    CalledFunctionsOrderedSetTy wrapped;
    getCalledAndWrappedFunctions(f, msg, called, wrapped);
    
    if (DEBUG && called.size()) {
      errs() << "\nDetected (possible allocators) called by function " << funName(f) << ":\n";
      for(CalledFunctionsOrderedSetTy::const_iterator cfi = called.begin(), cfe = called.end(); cfi != cfe; ++cfi) {
        const CalledFunctionTy *cf = *cfi;
        errs() << "   " << funName(cf) << "\n";
      }
    }
    if (DEBUG && wrapped.size()) {
      errs() << "\nDetected (possible allocators) wrapped by function " << funName(f) << ":\n";
      for(CalledFunctionsOrderedSetTy::const_iterator cfi = wrapped.begin(), cfe = wrapped.end(); cfi != cfe; ++cfi) {
        const CalledFunctionTy *cf = *cfi;
        errs() << "   " << funName(cf) << "\n";
      }
    }
    if (DEBUG) {
      FunctionsSetTy wrappedAllocators;
      getWrappedAllocators(f->fun, wrappedAllocators, getGCFunction(m));
      if (!wrappedAllocators.empty()) {
        errs() << "\nSimple (possible allocators) wrapped by function " << funName(f) << ":\n";
        for(FunctionsSetTy::iterator fi = wrappedAllocators.begin(), fe = wrappedAllocators.end(); fi != fe; ++fi) {
          Function *sf = *fi;
          errs() << "   " << funName(sf) << "\n";
        }
      }
    }
    
    nfuncs = getNumberOfCalledFunctions(); // get the current size
    resize(callsList, nfuncs);
    resize(wrapsList, nfuncs);
    resize(callsMat, nfuncs);
    resize(wrapsMat, nfuncs);
    
    for(CalledFunctionsOrderedSetTy::const_iterator cfi = called.begin(), cfe = called.end(); cfi != cfe; ++cfi) {
      const CalledFunctionTy *cf = *cfi;
      callsMat[f->idx][cf->idx] = true;
      callsList[f->idx].push_back(cf->idx);
    }

    for(CalledFunctionsOrderedSetTy::const_iterator wfi = wrapped.begin(), wfe = wrapped.end(); wfi != wfe; ++wfi) {
      const CalledFunctionTy *wf = *wfi;
      wrapsMat[f->idx][wf->idx] = true;
      wrapsList[f->idx].push_back(wf->idx);
    }    
  }
  
  // calculate transitive closure

  buildClosure(callsMat, callsList, nfuncs);
  buildClosure(wrapsMat, wrapsList, nfuncs);
  
  // fill in results
  
  // also fill in context-sensitive non-called allocators, allocating functions
  //   (note: this is more precise than possibleAllocators, allocatingFunctions)
  
  contextSensitiveAllocatingFunctions = new FunctionsSetTy();
  contextSensitivePossibleAllocators = new FunctionsSetTy();
  
  unsigned gcidx = gcFunction->idx;
  for(unsigned i = 0; i < nfuncs; i++) {
    if (callsMat[i][gcidx]) {
      const CalledFunctionTy *tgt = getCalledFunction(i);
      allocatingCFunctions->insert(tgt);
      if (!tgt->hasContext()) {
        contextSensitiveAllocatingFunctions->insert(tgt->fun);
      }
    }
    if (wrapsMat[i][gcidx]) {
      const CalledFunctionTy *tgt = getCalledFunction(i);
      if (!isKnownNonAllocator(tgt)) {
        possibleCAllocators->insert(tgt);
        if (!tgt->hasContext()) {
          contextSensitivePossibleAllocators->insert(tgt->fun);
        }
      }
    }    
  }
  allocatingCFunctions->insert(gcFunction);
  possibleCAllocators->insert(gcFunction);
  contextSensitiveAllocatingFunctions->insert(gcFunction->fun);
  contextSensitivePossibleAllocators->insert(gcFunction->fun);
}

std::string funName(const CalledFunctionTy *cf) {
  return funName(cf->fun) + cf->getNameSuffix();  
}
