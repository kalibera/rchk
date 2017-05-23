
#include "cprotect.h"
#include "table.h"
#include "allocators.h"

#include <unordered_map>
#include <vector>

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstIterator.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

typedef std::vector<bool> ArgsTy;
typedef IndexedTable<AllocaInst> VarIndexTy;
typedef IndexedTable<Argument> ArgIndexTy;

const bool DEBUG = false;
const bool CONMSG = DEBUG; // print message when confused
const unsigned MAX_DEPTH = 64;

// the function takes at least one SEXP variable as argument
static bool hasSEXPArg(Function *fun) {
  FunctionType* ftype = fun->getFunctionType();
  for(FunctionType::param_iterator pi = ftype->param_begin(), pe = ftype->param_end(); pi != pe; ++pi) {
    Type* type = *pi;
    if (isSEXP(type)) {
      return true; 
    }
  }
  return false;
}

static bool isSEXPParam(Function *fun, unsigned pidx) {
  FunctionType* ftype = fun->getFunctionType();
  if (pidx >= ftype->getNumParams()) {
    return false;
  }
  return isSEXP(ftype->getParamType(pidx));
}

struct FunctionState {

  Function *fun;
  ArgsTy exposed;		// true for arguments that are (possibly) not protected explicitly at an allocating call
  ArgsTy usedAfterExposure;	// true for arguments that are (possibly) used after being exposed [approximation, due to merge the order of events is not fixed]
  bool dirty; 			// needs to be re-analyzed
  VarIndexTy varIndex;		// variable numbering
  ArgIndexTy argIndex;		// argument numbering
  bool confused;
  
  FunctionState(Function *fun): fun(fun), exposed(fun->arg_size(), false), usedAfterExposure(fun->arg_size(), false), dirty(false), varIndex(), argIndex(), confused(false) {

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
  }
  
  bool merge(ArgsTy _exposed, ArgsTy _usedAfterExposure) {

    unsigned nargs = _exposed.size();
    bool updated = false;
    
    for(unsigned i = 0; i < nargs; i++) {
      if (!exposed.at(i) && _exposed.at(i)) {
        exposed.at(i) = true;
        updated = true;
      }
      if (!usedAfterExposure.at(i) && _usedAfterExposure.at(i)) {
        usedAfterExposure.at(i) = true;
        updated = true;
      }
    }
    return updated;
  }
  
  bool isCalleeProtect() {

    unsigned nargs = exposed.size();
    for(unsigned i = 0; i < nargs; i++) {
      if (exposed.at(i)) return false;
    }
    return true;
  }
  
  bool isNonTriviallyCalleeProtect(FunctionsSetTy& allocatingFunctions) {

    if (confused || !isCalleeProtect()) {
      return false;
    }
    if (allocatingFunctions.find(fun) == allocatingFunctions.end()) { // the functions allocates
      return false;
    }
    return hasSEXPArg(fun);
  }
  
  void markConfused() {
    if (confused) {
      return;
    }
    confused = true;
    unsigned nargs = exposed.size();
    for(unsigned i = 0; i < nargs; i++) { // conservatively mark all args exposed
      // this also marks non-SEXP args
      exposed.at(i) = true;
      usedAfterExposure.at(i) = true;
    }
  }
};

typedef std::unordered_map<Function*, FunctionState> FunctionTableTy;
typedef std::vector<Function*> FunctionListTy;

static void addToFunctionWorkList(FunctionListTy& workList, FunctionState& fstate) {

  if (!fstate.dirty) {
    fstate.dirty = true;
    workList.push_back(fstate.fun);
  }
}

typedef std::vector<int> ProtectStackTy;
typedef std::vector<int> VarsTy;

struct BlockState {

  ProtectStackTy pstack; 	// argument index (>=0) or -1, when not (surely) an argument value
  ArgsTy exposed;
  ArgsTy usedAfterExposure;
  VarsTy vars;			// argument index (>=0) or -1, when not (surely) an argument value
  bool dirty;			// block is in worklist, to be processed again
  
  BlockState(unsigned nargs, unsigned nvars):
    pstack(), exposed(nargs, false), usedAfterExposure(nargs, false), vars(nvars, -1), dirty(false) {}
  
  BlockState(ProtectStackTy pstack, ArgsTy exposed, ArgsTy usedAfterExposure, VarsTy vars, bool dirty):
    pstack(pstack), exposed(exposed), usedAfterExposure(usedAfterExposure), vars(vars), dirty(dirty) {}
    
  bool merge(BlockState& s, FunctionState& fstate) {
    bool updated = false;

    unsigned depth = pstack.size();
    if (depth != s.pstack.size()) {
      if (CONMSG) errs() << "confused, stack sizes mismatch at merge, not merging\n";
      fstate.markConfused();
      return updated;
    }
    for(unsigned i = 0; i < depth; i++) {
      if (pstack.at(i) != -1 && s.pstack.at(i) == -1) {
        pstack.at(i) = -1;
        updated = true;
      }
    }
    
    unsigned nargs = exposed.size();
    myassert(nargs == usedAfterExposure.size());
    myassert(nargs == s.exposed.size());
    myassert(nargs == s.usedAfterExposure.size());
    
    for(unsigned i = 0; i < nargs; i++) {
      if (!exposed.at(i) && s.exposed.at(i)) {
        exposed.at(i) = true;
        updated = true;
      }
      if (!usedAfterExposure.at(i) && s.usedAfterExposure.at(i)) {
        usedAfterExposure.at(i) = true;
        updated = true;
      }
    }
    
    unsigned nvars = vars.size();
    for (unsigned i = 0; i < nvars; i++) {
      if (vars.at(i) != s.vars.at(i) && vars.at(i) != -1) {
        vars.at(i) = -1;
        updated = true;
      }
    }
    return updated;
  }
};

typedef std::unordered_map<BasicBlock*, BlockState> BlocksTy;
typedef std::vector<BasicBlock*> BlockWorkListTy;

// calculates which arguments are currently (definitely) protected
static ArgsTy protectedArgs(ProtectStackTy pstack, unsigned nargs) {

  ArgsTy protects(nargs, false);
  for(ProtectStackTy::iterator pi = pstack.begin(), pe = pstack.end(); pi != pe; ++pi) {
    int pvalue = *pi;
    
    if (pvalue >= 0) {
      protects.at(pvalue) = true;
    }
  }
  
  return protects;
}

static void dumpArgs(ArgsTy args) {

  unsigned nargs = args.size();
  for(unsigned i = 0; i < nargs; i++) {
    if (args.at(i)) {
      errs() << " " << std::to_string(i);
    }
  }
}

// note: the case may not be interesting (e.g. non-SEXP argument, not allocating function, that has to be checked extra)
static bool isExposedBitSet(Function *fun, FunctionTableTy& functions, unsigned aidx) {

  auto fsearch = functions.find(fun);
  myassert(fsearch != functions.end());
  
  FunctionState& fstate = fsearch->second;
  return fstate.exposed.at(aidx);
}

// note: the case may not be interesting (e.g. non-SEXP argument, not allocating function, that has to be checked extra)
static bool isUsedAfterExposureBitSet(Function *fun, FunctionTableTy& functions, unsigned aidx) {

  auto fsearch = functions.find(fun);
  myassert(fsearch != functions.end());
  
  FunctionState& fstate = fsearch->second;
  return fstate.usedAfterExposure.at(aidx);
}

static FunctionState& getFunctionState(FunctionTableTy& functions, Function *f) {
  auto fsearch = functions.find(f);
  myassert(fsearch != functions.end());
  return fsearch->second;
}

// in a call to function fun, is the aidx'th argument matched to a function parameter of type SEXP?
//   (e.g. if passed through ..., it is not)

static bool matchedToSEXPArg(unsigned aidx, Function* fun) {

  if (aidx >= fun->arg_size()) {
    return false;
  }
  return isSEXPParam(fun, aidx);
} 

// these callee-protect functions are written to conditionally protect (all arguments)
// only if GC may run; the tool cannot find this out automatically

static bool isSpecialCalleeProtect(Function *fun) {
  if (!fun) return false;
  return fun->getName() == "Rf_cons" || fun->getName() == "CONS_NR" || fun->getName() == "Rf_NewEnvironment" || fun->getName() == "mkPROMISE";
}

static void addCallersToWorkList(Function *fun, FunctionTableTy& functions, FunctionListTy& functionsWorkList) {
  // mark dirty all functions calling this function
  for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
    User *u = *ui;

    if (Instruction *in = dyn_cast<Instruction>(u)) {
      if (BasicBlock *bb = dyn_cast<BasicBlock>(in->getParent())) {
        Function *pf = bb->getParent();
        FunctionState& pstate = getFunctionState(functions, pf);
        addToFunctionWorkList(functionsWorkList, pstate);
        if (DEBUG) errs() << "adding function " << funName(pf) << " to worklist (updated its callee)\n";
      }
    }
  }
}

static void analyzeFunction(FunctionState& fstate, FunctionTableTy& functions, FunctionListTy& functionsWorkList, FunctionsSetTy& allocatingFunctions) {

  Function *fun = fstate.fun;

  // these are constant properties of the function
  if (!hasSEXPArg(fun) || allocatingFunctions.find(fun) == allocatingFunctions.end()) {
    return; // trivially nothing exposed
  }
  if (isSpecialCalleeProtect(fun)) {
    return; // nothing exposed (hardcoded)
  }
  
  // this can only change from non-confused to confused
  if (fstate.confused) {
    return; // the functions is too complicated for the tool
      // it has already been marked as exposing everything
  }

  if (DEBUG) errs() << "analyzing function " << funName(fun) << " worklist size " << std::to_string(functionsWorkList.size()) << "\n";
  if (DEBUG) {
    errs() << "   exposed ";
    dumpArgs(fstate.exposed);
    errs() << "\n";
    errs() << "   usedAfterExposure ";
    dumpArgs(fstate.usedAfterExposure);
    errs() << "\n";
  }

  // keep copy of the original state to detect changes
  
  ArgsTy oldExposed(fstate.exposed);
  ArgsTy oldUsedAfterExposure(fstate.usedAfterExposure);
  
  unsigned nvars = fstate.varIndex.size();
  unsigned nargs = fstate.argIndex.size();
  
  for(unsigned i = 0; i < nargs; i++) {
    fstate.exposed.at(i) = false;
    fstate.usedAfterExposure.at(i) = false;
    // could instead use swap, but this is more readable
  }
  
  ArgsTy sexpArgs(nargs, false);
  for(unsigned i = 0; i < nargs; i++) {
    sexpArgs.at(i) = isSEXPParam(fun, i); // is this caching needed?
  }
  
  BlocksTy blocks;
  BlockWorkListTy workList;
  
  BasicBlock *entryb = &fun->getEntryBlock();
  blocks.insert({entryb, BlockState(nargs, nvars)}); 
  workList.push_back(entryb); 
  
  while(!workList.empty()) {
    BasicBlock *bb = workList.back();
    workList.pop_back();
    
    auto bsearch = blocks.find(bb);
    myassert(bsearch != blocks.end());
    bsearch->second.dirty = false;
    BlockState s = bsearch->second; // copy
    
    
    for(BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
      Instruction *in = &*ii;
      
      if (StoreInst *si = dyn_cast<StoreInst>(in)) {
        if (AllocaInst *var = dyn_cast<AllocaInst>(si->getPointerOperand())) {
        
          if (Argument *arg = dyn_cast<Argument>(si->getValueOperand())) {  // var = arg
            unsigned aidx = fstate.argIndex.indexOf(arg);
            unsigned vidx = fstate.varIndex.indexOf(var);
            s.vars.at(vidx) = aidx;
            if (DEBUG) errs() << "var = arg [" << varName(var) << "=" << aidx << "] " <<  sourceLocation(in) << "\n";
            continue;
          }
          
          if (LoadInst *li = dyn_cast<LoadInst>(si->getValueOperand())) {
            if (AllocaInst *srcVar = dyn_cast<AllocaInst>(li->getPointerOperand())) { // var = srcVar
              unsigned vidx = fstate.varIndex.indexOf(var);
              unsigned svidx = fstate.varIndex.indexOf(srcVar);
              s.vars.at(vidx) = s.vars.at(svidx);
              if (DEBUG) errs() << "var = srcVar [" << varName(var) << " = " << varName(srcVar) << "] " << sourceLocation(in) << "\n";
              continue;
            }
          }
        }
        continue;
      }
      
      if (LoadInst *li = dyn_cast<LoadInst>(in)) {
        if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // load of var
          unsigned vidx = fstate.varIndex.indexOf(var);
          int varState = s.vars.at(vidx);
          if (varState >= 0) {
            // variable holds a value from an argument
            unsigned aidx = varState;
            myassert(aidx < s.exposed.size() && aidx < s.usedAfterExposure.size());
            if (s.exposed.at(aidx)) {
              s.usedAfterExposure.at(aidx) = true;
              // Note: this does not work well for functions that make a value exposed
              //   note that if the loaded value is to be passed to a function that exposes it,
              //   the exposed bit is not yet set, so usedAfterExposure will not be set, either
              //
              //   but, this is handled in the function call case below
            }
            if (DEBUG) errs() << "load of var " << varName(var) << " holding argument " << aidx << " " << sourceLocation(in) << "\n";
          }
        }
        continue;
      }
      
      CallSite cs(in);
      if (cs && !cs.getCalledFunction()) { // call to external function
        // in allocation detection, this is treated as an allocating call, so for consistency we should
        // be also that conservative here, otherwise allocating functions that are allocating just because
        // of external calls would be reported as callee-protect... 
        
        ArgsTy protects = protectedArgs(s.pstack, nargs);
        for(unsigned i = 0; i < nargs; i++) {
          if (!protects.at(i) && sexpArgs.at(i)) {
            s.exposed.at(i) = true;
            s.usedAfterExposure.at(i) = true;
          }
        }
        continue;
      }
      if (cs && cs.getCalledFunction() && allocatingFunctions.find(cs.getCalledFunction()) != allocatingFunctions.end()) { // call to allocating function
        Function *tgtFun = cs.getCalledFunction();

        ArgsTy protects = protectedArgs(s.pstack, nargs);
        ArgsTy passedInCall(nargs, false);        
        ArgsTy passedToNonSEXPArg(nargs, false);
        ArgsTy exposedInCall(nargs, false);
        ArgsTy usedAfterExposureInCall(nargs, false);
        
        unsigned tgtAidx = 0;
        for(CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg_end(); ai != ae; ++ai, ++tgtAidx) {
          Value* val = *ai;
          unsigned aidx;
          bool passingArg = false;
          
          if (Argument *arg = dyn_cast<Argument>(val)) {
            aidx = fstate.argIndex.indexOf(arg);
            passingArg = isSEXP(arg->getType());
            if (DEBUG && passingArg) errs() << "passing argument " << aidx << " directly " << sourceLocation(in) << "\n";            
          } else if (LoadInst *li = dyn_cast<LoadInst>(val)) {
            if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // passing a variable
              unsigned vidx = fstate.varIndex.indexOf(var);
              int varState = s.vars.at(vidx);
              if (varState >= 0) {
                aidx = varState;
                passingArg = isSEXP(var);
                if (DEBUG && passingArg) errs() << "passing argument " << aidx << " through variable " << varName(var) << " " << sourceLocation(in) << "\n";
              }
            }
          }
          
          if (!passingArg) {
            continue;
          }
          passedInCall.at(aidx) = true;
          
          if (!matchedToSEXPArg(tgtAidx, tgtFun)) {
            // the subtle part: an argument may match to ... parameter
            //   the tool does not handle it, so to be safe, we treat the argument as exposed
            //   also if there was e.g. a void* parameter this conservativeness would apply
            passedToNonSEXPArg.at(aidx) = true;
            continue;
          }
          
          if (isExposedBitSet(tgtFun, functions, tgtAidx)) {
            exposedInCall.at(aidx) = true;
          }
          if (isUsedAfterExposureBitSet(tgtFun, functions, tgtAidx)) {
            usedAfterExposureInCall.at(aidx) = true;
          }
        }
        // first mark all arguments as exposed, but later fix-up for the case when
        //   some of them is passed to a callee-protect function
        for(unsigned i = 0; i < nargs; i++) {
          if (!sexpArgs.at(i)) {
            continue;
          }
          if (protects.at(i)) {
            continue; // arg is protected, the callee can do anything
          }
          if (!passedInCall.at(i)) {
            if (DEBUG) errs() << "argument " << std::to_string(i) << " exposed because not passed to allocating function " << funName(tgtFun) << "\n";
            s.exposed.at(i) = true; // arg is not passed to the (allocating) function
          }
          if (passedToNonSEXPArg.at(i)) {
            // be conservative
            s.exposed.at(i) = true;
            s.usedAfterExposure.at(i) = true;
            if (DEBUG) errs() << "argument " << std::to_string(i) << " assumed exposed+usedAfterExposure because passed to non-SEXP parameter of " << funName(tgtFun) << "\n";
          }
          
          if (exposedInCall.at(i)) {
            s.exposed.at(i) = true; // not protected, exposed at least through one parameter
            if (DEBUG) errs() << "   argument " << std::to_string(i) << " is exposed at call to " << funName(tgtFun) << "\n";
          }
          if (usedAfterExposureInCall.at(i) || passedToNonSEXPArg.at(i)) {
            s.usedAfterExposure.at(i) = true; // not protected, used after exposure at least through one parameter
            if (DEBUG) errs() << "   argument " << std::to_string(i) << " is used after exposure at call to " << funName(tgtFun) << "\n";
          }
        }
        continue;
      }
      
      std::string cfname = "";
      if (cs && cs.getCalledFunction()) {
        cfname = cs.getCalledFunction()->getName();
      }
      
      if (cfname == "Rf_protect" || cfname == "R_ProtectWithIndex") {
        // FIXME: should interpret the index in protectWithIndex
        Value* val = cs.getArgument(0);
        int protValue = -1; // by default, -1 means non-argument
        
        if (Argument *arg = dyn_cast<Argument>(val)) { // PROTECT(arg)
          unsigned aidx = fstate.argIndex.indexOf(arg);
          protValue = aidx;  
          if (DEBUG) errs() << "protecting argument directly " << sourceLocation(in) << "\n";
        }
        if (LoadInst *li = dyn_cast<LoadInst>(val)) {
          if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // PROTECT(var)
            unsigned vidx = fstate.varIndex.indexOf(var);
            int varState = s.vars.at(vidx);
            protValue = varState;
            if (DEBUG) errs() << "protecting argument via variable " << sourceLocation(in) << "\n";
          }
        }
        if (s.pstack.size() < MAX_DEPTH) {
          s.pstack.push_back(protValue);
          if (DEBUG) errs() << "pushing value " << std::to_string(protValue) << " to protect stack " << sourceLocation(in) << "\n";
        } else {
          errs() << "maximum stack depth reached (treating as confusion)\n";
          fstate.markConfused();
          addCallersToWorkList(fun, functions, functionsWorkList);
          return;
        }
        continue;
      }
      
      // FIXME: what about unprotect_ptr ?
      if (cfname == "Rf_unprotect") {
        Value *val = cs.getArgument(0);
        if (ConstantInt* ci = dyn_cast<ConstantInt>(val)) {
          uint64_t ival = ci->getZExtValue();
          if (DEBUG) errs() << "unprotecting " << std::to_string(ival) << " values, stack size " << std::to_string(s.pstack.size()) << " " << sourceLocation(in) << "\n";
          while(ival > 0 && !s.pstack.empty()) {
            ival--;
            s.pstack.pop_back();
          }

          if (ival) {
            if (CONMSG) errs() << "   confusion: unprotecting more values than protected\n";
            fstate.markConfused();
            addCallersToWorkList(fun, functions, functionsWorkList);
            return;
          }
          
        } else {
          if (CONMSG) errs() << "   confusion: unsupported form of unprotect " << sourceLocation(in) << "\n";
          fstate.markConfused();
          addCallersToWorkList(fun, functions, functionsWorkList);
          return;
        }
      }
    }
    
    TerminatorInst *t = bb->getTerminator();
    for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
      BasicBlock *succ = t->getSuccessor(i);
      auto ssearch = blocks.find(succ);
      if (ssearch == blocks.end()) {
      
        // not yet explored block
        BlockState sstate(s.pstack, s.exposed, s.usedAfterExposure, s.vars, true /* dirty */);
        blocks.insert({succ, sstate});
        workList.push_back(succ);

      } else {
        BlockState& pstate = ssearch->second;
        
        // merge
        if (pstate.merge(s, fstate) && !pstate.dirty) {
          if (fstate.confused) {
            if (CONMSG) errs() << "   confusion after merging into successor";
            fstate.markConfused();
            addCallersToWorkList(fun, functions, functionsWorkList);
            return;
          }
          pstate.dirty = true;
          workList.push_back(succ);
        }
      }
    }
  
    if (ReturnInst::classof(t)) {
      fstate.merge(s.exposed, s.usedAfterExposure);
    }
  }

  if (DEBUG) errs() << "done analyzing function " << funName(fun) << "\n";
  if (DEBUG) {
    errs() << "   exposed ";
    dumpArgs(fstate.exposed);
    errs() << "\n";
    errs() << "   usedAfterExposure ";
    dumpArgs(fstate.usedAfterExposure);
    errs() << "\n";
  }

  if (fstate.exposed != oldExposed || fstate.usedAfterExposure != oldUsedAfterExposure) {
    if (DEBUG) errs() << "adding callers of " << funName(fun) << " to worklist, because " << funName(fun) << " analysis has changed.\n";
    addCallersToWorkList(fun, functions, functionsWorkList);
  }
}

CProtectInfo findCalleeProtectFunctions(Module *m, FunctionsSetTy& allocatingFunctions) {

  FunctionTableTy functions; // function envelopes
  FunctionListTy workList; // functions to be re-analyzed
  
  if (DEBUG) errs() << "adding functions..\n";
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *f = &*fi;
    FunctionState fstate(f);
    auto finsert = functions.insert({f, fstate});
    myassert(finsert.second);
    addToFunctionWorkList(workList, fstate);
  }
  
  while(!workList.empty()) {
    FunctionState& fstate = getFunctionState(functions, workList.back());
    workList.pop_back();
    if (DEBUG) errs() << "size functions=" << functions.size() << " workList=" << workList.size() << "\n";

    analyzeFunction(fstate, functions, workList, allocatingFunctions);
    fstate.dirty = false;
      // a function may be recursive
      //   but then it does not have to be re-analyzed just because of 
      //   that it has been re-analyzed
  }  
  
  CProtectInfo cprotect;
  for(FunctionTableTy::iterator fi = functions.begin(), fe = functions.end(); fi != fe; ++fi) {
    Function* fun = fi->first;
    FunctionState& fstate = fi->second;
    
    unsigned nargs = fstate.exposed.size();
    CPArgsTy cpargs(nargs, CP_TRIVIAL);
    bool isAllocating = allocatingFunctions.find(fun) != allocatingFunctions.end();
    
    for(unsigned i = 0; i < nargs; i++) {
      if (!isAllocating || !isSEXPParam(fun, i)) {
        cpargs.at(i) = CP_TRIVIAL;
        continue;
      }
      if (fstate.exposed.at(i)) {
        if (!fstate.usedAfterExposure.at(i)) {
          cpargs.at(i) = CP_CALLEE_SAFE;
        } else {
          cpargs.at(i) = CP_CALLER_PROTECT;
        }
      } else {
        cpargs.at(i) = CP_CALLEE_PROTECT;
      }
    }
    cprotect.map.insert({fun, cpargs});
  }

  return cprotect;
}

bool CProtectInfo::isCalleeProtect(Function *fun, int argIndex, bool onlyNonTrivially) {
  auto fsearch = map.find(fun);
  myassert(fsearch != map.end()); 
  CPArgsTy& cpargs = fsearch->second;
  CPKind k = cpargs.at(argIndex);
  if (onlyNonTrivially) {
    return k == CP_CALLEE_PROTECT;
  } else {
    return k == CP_CALLEE_PROTECT || k == CP_TRIVIAL;
  }
}

bool CProtectInfo::isCalleeProtect(Function *fun, bool onlyNonTrivially) {
  auto fsearch = map.find(fun);
  myassert(fsearch != map.end()); 
  CPArgsTy& cpargs = fsearch->second;
  
  unsigned nargs = cpargs.size();
  bool seenNonTrivial = false;
  
  for(unsigned i = 0; i < nargs; i++) {
    CPKind k = cpargs.at(i);
    if (k == CP_TRIVIAL) {
      continue;
    }
    if (k == CP_CALLEE_PROTECT) {
      seenNonTrivial = true;
      continue;
    }
    return false;
  }
  if (onlyNonTrivially) {
    return seenNonTrivial;
  } else {
    return true;
  }
}

bool CProtectInfo::isCalleeSafe(Function *fun, int argIndex, bool onlyNonTrivially) {
  auto fsearch = map.find(fun);
  myassert(fsearch != map.end()); 
  CPArgsTy& cpargs = fsearch->second;
  
  CPKind k = cpargs.at(argIndex);
  
  if (onlyNonTrivially) {
    return k == CP_CALLEE_SAFE;
  } else {
    return k == CP_CALLEE_SAFE || CP_TRIVIAL || k == CP_CALLEE_PROTECT;
  }
}

bool CProtectInfo::isCalleeSafe(Function *fun, bool onlyNonTrivially) {
  auto fsearch = map.find(fun);
  myassert(fsearch != map.end()); 
  CPArgsTy& cpargs = fsearch->second;
  
  unsigned nargs = cpargs.size();
  bool seenNonTrivial = false;
  
  for(unsigned i = 0; i < nargs; i++) {
    CPKind k = cpargs.at(i);
    if (k == CP_TRIVIAL || k == CP_CALLEE_PROTECT) {
      continue;
    }
    if (k == CP_CALLEE_SAFE) {
      seenNonTrivial = true;
      continue;
    }
    return false;
  }
  if (onlyNonTrivially) {
    return seenNonTrivial;
  } else {
    return true;
  }
}

bool CProtectInfo::isNonTrivial(Function *fun) {

  auto fsearch = map.find(fun);
  myassert(fsearch != map.end()); 
  CPArgsTy& cpargs = fsearch->second;
  
  unsigned nargs = cpargs.size();
  
  for(unsigned i = 0; i < nargs; i++) {
    CPKind k = cpargs.at(i);
    if (k != CP_TRIVIAL) {
      return true;
    }
  }
  return false;
}

