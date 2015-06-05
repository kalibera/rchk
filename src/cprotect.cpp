
#include "cprotect.h"
#include "table.h"
#include "allocators.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/InstIterator.h>

#include <llvm/Support/raw_ostream.h>

#undef NDEBUG
#include <assert.h>

using namespace llvm;

typedef std::vector<bool> ArgsTy;
typedef IndexedTable<AllocaInst> VarIndexTy;
typedef IndexedTable<Argument> ArgIndexTy;

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

struct FunctionState {

  bool dirty; 			// needs to be re-analyzed
  ArgsTy exposed;		// true for arguments that are (possibly) not protected explicitly at an allocating call
  ArgsTy usedAfterExposure;	// true for arguments that are (possibly) used after being exposed [approximation, due to merge the order of events is not fixed]
  Function *fun;
  VarIndexTy varIndex;		// variable numbering
  ArgIndexTy argIndex;		// argument numbering
  
  FunctionState(Function *fun): fun(fun), exposed(fun->arg_size(), false), usedAfterExposure(fun->arg_size(), false), dirty(false), varIndex(), argIndex() {

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
      if (!exposed[i] && _exposed[i]) {
        exposed[i] = true;
        updated = true;
      }
      if (!usedAfterExposure[i] && _usedAfterExposure[i]) {
        usedAfterExposure[i] = true;
        updated = true;
      }
    }
    return updated;
  }
  
  bool isCalleeProtect() {

    unsigned nargs = exposed.size();
    for(unsigned i = 0; i < nargs; i++) {
      if (exposed[i]) return false;
    }
    return true;
  }
  
  bool isNonTriviallyCalleeProtect(FunctionsSetTy& allocatingFunctions) {

    if (!isCalleeProtect()) {
      return false;
    }
    if (allocatingFunctions.find(fun) == allocatingFunctions.end()) { // the functions allocates
      return false;
    }
    return hasSEXPArg(fun);
  }
};

typedef std::unordered_map<Function*, FunctionState> FunctionTableTy;
typedef std::vector<FunctionState*> FunctionListTy;

static void addToFunctionWorkList(FunctionListTy& workList, FunctionState *fstate) {

  if (!fstate->dirty) {
    fstate->dirty = true;
    workList.push_back(fstate);
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
    
  bool merge(BlockState s) {
    bool updated = false;

    unsigned depth = pstack.size();
    if (depth != s.pstack.size()) {
      errs() << "stack sizes mismatch at merge";
      return updated;
    }
    for(unsigned i = 0; i < depth; i++) {
      if (pstack[i] != -1 && s.pstack[i] == -1) {
        pstack[i] = -1;
        updated = true;
      }
    }
    
    unsigned nargs = exposed.size();
    assert(nargs == usedAfterExposure.size());
    assert(nargs == s.exposed.size());
    assert(nargs == s.usedAfterExposure.size());
    
    for(unsigned i = 0; i < nargs; i++) {
      if (!exposed[i] && s.exposed[i]) {
        exposed[i] = true;
        updated = true;
      }
      if (!usedAfterExposure[i] && s.usedAfterExposure[i]) {
        usedAfterExposure[i] = true;
        updated = true;
      }
    }
    
    unsigned nvars = vars.size();
    for (unsigned i = 0; i < nvars; i++) {
      if (vars[i] != s.vars[i] && vars[i] != -1) {
        vars[i] = -1;
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
      protects[pvalue] = true;
    }
  }
  
  return protects;
}

// note: the case may not be interesting (e.g. non-SEXP argument, not allocating function, that has to be checked extra)
static bool isExposedBitSet(Function *fun, FunctionTableTy& functions, unsigned aidx) {

  auto fsearch = functions.find(fun);
  assert(fsearch != functions.end());
  
  FunctionState& fstate = fsearch->second;
  assert(aidx < fstate.exposed.size());
  return fstate.exposed[aidx];
}

static void analyzeFunction(FunctionState *fstate, FunctionTableTy& functions, FunctionListTy& functionsWorkList, FunctionsSetTy& allocatingFunctions) {

  Function *fun = fstate->fun;

  if (!hasSEXPArg(fun) || allocatingFunctions.find(fun) == allocatingFunctions.end()) {
    return; // trivially nothing exposed
  }

  errs() << "analyzing function " << funName(fstate->fun) << " worklist size " << std::to_string(functionsWorkList.size()) << "\n";

  unsigned nvars = fstate->varIndex.size();
  unsigned nargs = fstate->argIndex.size();
  bool updated = false;
  
  BlocksTy blocks;
  BlockWorkListTy workList;
  
  BasicBlock *entryb = &fun->getEntryBlock();
  blocks.insert({entryb, BlockState(nargs, nvars)}); 
  workList.push_back(entryb); 
  
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
        
          if (Argument *arg = dyn_cast<Argument>(si->getValueOperand())) {  // var = arg
            unsigned aidx = fstate->argIndex.indexOf(arg);
            unsigned vidx = fstate->varIndex.indexOf(var);
            s.vars[vidx] = aidx;
            continue;
          }
          
          if (LoadInst *li = dyn_cast<LoadInst>(si->getValueOperand())) {
            if (AllocaInst *srcVar = dyn_cast<AllocaInst>(li->getPointerOperand())) { // var = srcVar
              unsigned vidx = fstate->varIndex.indexOf(var);
              unsigned svidx = vidx = fstate->varIndex.indexOf(srcVar);
              s.vars[vidx] = s.vars[svidx];
              continue;
            }
          }
        }
        continue;
      }
      
      if (LoadInst *li = dyn_cast<LoadInst>(in)) {
        if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // load of var
          unsigned vidx = fstate->varIndex.indexOf(var);
          int varState = s.vars[vidx];
          if (varState >= 0) {
            // variable holds a value from an argument
            unsigned aidx = varState;
            if (s.exposed[aidx]) {
              s.usedAfterExposure[aidx] = true;
            }
          }
        }
        continue;
      }
      
      CallSite cs(in);
      if (cs && cs.getCalledFunction() && allocatingFunctions.find(cs.getCalledFunction()) != allocatingFunctions.end()) {
        ArgsTy protects = protectedArgs(s.pstack, nargs);
        for(CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg_end(); ai != ae; ++ai) {
          Value* val = *ai;
          if (Argument *arg = dyn_cast<Argument>(val)) {
            unsigned aidx = fstate->argIndex.indexOf(arg);
            if (isSEXP(arg->getType()) && !protects[aidx] && isExposedBitSet(cs.getCalledFunction(), functions, aidx)) { // passing an unprotected argument directly
              s.exposed[aidx] = true;
            }
            continue;
          }
          if (LoadInst *li = dyn_cast<LoadInst>(val)) {
            if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // passing a variable
              unsigned vidx = fstate->varIndex.indexOf(var);
              int varState = s.vars[vidx];
              if (varState >= 0) {
                unsigned aidx = varState;
                if (isSEXP(var) && !protects[aidx] && isExposedBitSet(cs.getCalledFunction(), functions, aidx)) {
                  s.exposed[aidx] = true;
                }
              }
            }
            continue;
          }
        }
        continue;
      }
      
      // FIXME: should somehow record that the tool did not understand some protection features (confusion)
      //   functions with confusion should be treated specially
      
      std::string cfname = "";
      if (cs && cs.getCalledFunction()) {
        cfname = cs.getCalledFunction()->getName();
      }
      
      if (cfname == "Rf_protect" || cfname == "R_ProtectWithIndex") {
        // FIXME: should interpret the index in protectWithIndex
        Value* val = cs.getArgument(0);
        int protValue = -1; // by default, -1 means non-argument
        
        if (Argument *arg = dyn_cast<Argument>(val)) { // PROTECT(arg)
          unsigned aidx = fstate->argIndex.indexOf(arg);
          protValue = aidx;  
        }
        if (LoadInst *li = dyn_cast<LoadInst>(val)) {
          if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // PROTECT(var)
            unsigned vidx = fstate->varIndex.indexOf(var);
            int varState = s.vars[vidx];
            protValue = varState;
          }
        }
        if (s.pstack.size() < MAX_DEPTH) {
          s.pstack.push_back(protValue);
        } else {
          errs() << "maximum stack depth reached\n";
        }
        continue;
      }
      
      // FIXME: what about unprotect_ptr ?
      if (cfname == "Rf_unprotect") {
        Value *val = cs.getArgument(0);
        if (ConstantInt* ci = dyn_cast<ConstantInt>(val)) {
          uint64_t ival = ci->getZExtValue();
          while(ival--) {
            s.pstack.pop_back();
          }
          if (ival) {
            // FIXME: report some warning/confused
            errs() << "   confusion: unsupported form of unprotect\n";
          }
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
        if (pstate.merge(s) && !pstate.dirty) {
          pstate.dirty = true;
          workList.push_back(succ);
        }
      }
    }
    
    if (ReturnInst::classof(t)) {
      updated = updated || fstate->merge(s.exposed, s.usedAfterExposure);
    }
  }
  if (updated) {
    // mark dirty all functions calling this function
    for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
      User *u = *ui;

      if (Instruction *in = dyn_cast<Instruction>(u)) {
        if (BasicBlock *bb = dyn_cast<BasicBlock>(in->getParent())) {
          Function *pf = bb->getParent();
          auto fsearch = functions.find(pf);
          assert(fsearch != functions.end());
          FunctionState& pinfo = fsearch->second;
          if (!pinfo.dirty) {
            pinfo.dirty = true;
            functionsWorkList.push_back(&pinfo);
          }
        }
      }
    }
  }
}


FunctionsSetTy findCalleeProtectFunctions(Module *m) {

  FunctionTableTy functions; // function envelopes
  FunctionListTy workList; // functions to be re-analyzed
  
  errs() << "adding functions..\n";
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *f = fi;
    auto finsert = functions.insert({f, FunctionState(f)});
    assert(finsert.second);
    addToFunctionWorkList(workList, &finsert.first->second); // FIXME: is this correct?
  }
  
  FunctionsSetTy allocatingFunctions;
  findAllocatingFunctions(m, allocatingFunctions);
  
  while(!workList.empty()) {
    FunctionState* fstate = workList.back();
    workList.pop_back();

    analyzeFunction(fstate, functions, workList, allocatingFunctions);
    fstate->dirty = false;
      // a function may be recursive
      //   but then it does not have to be re-analyzed just because of 
      //   that it has been re-analyzed
  }  
  
  FunctionsSetTy cprotect;
  for(FunctionTableTy::iterator fi = functions.begin(), fe = functions.end(); fi != fe; ++fi) {
    Function* fun = fi->first;
    FunctionState& fstate = fi->second;
    
    if (fstate.isNonTriviallyCalleeProtect(allocatingFunctions)) {
      errs() << "Function " << funName(fun) << " is callee-protect.\n";
      cprotect.insert(fun);
    }
  }
  
  return cprotect;
}
