
#include "cprotect.h"
#include "table.h"
#include "allocators.h"

#include <llvm/IR/InstIterator.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/CallSite.h>

using namespace llvm;

typedef std::vector<bool> ArgsTy;
typedef IndexedTable<AllocaInst> VarIndexTy;
typedef IndexedTable<Argument> ArgIndexTy;

const unsigned MAX_DEPTH = 64;

struct FunctionState {
  bool dirty; // needs to be re-analyzed
  ArgsTy exposed;
  ArgsTy exposedAndUsed;
  Function *fun;
  VarIndexTy varIndex;
  ArgIndexTy argIndex;
  
  FunctionState(Function *fun): fun(fun), exposed(fun->arg_size(), false), exposedAndUsed(fun->arg_size(), false), dirty(false) {

    // index variables
    for(inst_iterator ii = inst_begin(*fun), ie = inst_end(*fun); ii != ie; ++ii) {
      Instruction *in = &*ii;
      if (AllocaInst* var = dyn_cast<AllocaInst>(in)) {
        varIndex.indexOf(var);
      }
    }
    
    // index arguments
    for(Function::arg_iterator ai = fun->arg_begin(), ae = fun->arg_end(); ai != ae; ++ai) {
      Argument *a;
      argIndex.indexOf(a);
    }
  }
  
  bool merge(ArgsTy _exposed, ArgsTy _exposedAndUsed) {

    unsigned nargs = _exposed.size();
    bool updated = false;
    
    for(unsigned i = 0; i < nargs; i++) {
      if (!exposed[i] && _exposed[i]) {
        exposed[i] = true;
        updated = true;
      }
      if (!exposedAndUsed[i] && _exposedAndUsed[i]) {
        exposedAndUsed[i] = true;
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
    FunctionType* ftype = fun->getFunctionType();
    for(FunctionType::param_iterator pi = ftype->param_begin(), pe = ftype->param_end(); pi != pe; ++pi) {
      Type* type = *pi;
      if (isSEXP(type)) {
        return true; // the function takes at least one SEXP variable as argument
      }
    }
    return false;
  }
};

typedef std::unordered_map<Function*, FunctionState> FunctionTableTy;
typedef std::vector<FunctionState*> FunctionListTy;

static void addToWorkList(FunctionListTy& workList, FunctionState *fstate) {
  if (!fstate->dirty) {
    fstate->dirty = true;
    workList.push_back(fstate);
  }
}

typedef std::vector<int> ProtectStackTy;
typedef std::vector<int> VarsTy;

struct BlockState {
  ProtectStackTy pstack;
  ArgsTy exposed;
  ArgsTy exposedAndUsed;
  VarsTy vars;
  bool dirty;
  
  BlockState(unsigned nargs, unsigned nvars):
    pstack(), exposed(nargs, false), exposedAndUsed(nargs, false), vars(nvars, -1), dirty(false) {}
  
  BlockState(ProtectStackTy pstack, ArgsTy exposed, ArgsTy exposedAndUsed, VarsTy vars, bool dirty):
    pstack(pstack), exposed(exposed), exposedAndUsed(exposedAndUsed), vars(vars), dirty(dirty) {}
    
  void merge(BlockState s) {
    dirty = false;

    unsigned depth = pstack.size();
    if (depth != s.pstack.size()) {
      errs() << "stack sizes mismatch at merge";
      return;
    }
    for(unsigned i = 0; i < depth; i++) {
      if (pstack[i] != -1 && s.pstack[i] == -1) {
        pstack[i] = -1;
        dirty = true;
      }
    }
    
    unsigned nargs = exposed.size();
    assert(nargs == exposedAndUnused.size());
    for(unsigned i = 0; i < nargs; i++) {
      if (!exposed[i] && s.exposed[i]) {
        exposed[i] = true;
        dirty = true;
      }
      if (!exposedAndUsed[i] && s.exposedAndUsed[i]) {
        exposedAndUsed[i] = true;
        dirty = true;
      }
    }
    
    unsigned nvars = vars.size();
    for (unsigned i = 0; i < nvars; i++) {
      if (vars[i] != s.vars[i]) {
        vars[i] = -1;
        dirty = true;
      }
    }
  }
};

typedef std::unordered_map<BasicBlock*, BlockState> BlocksTy;
typedef std::vector<BasicBlock*> BlockWorkListTy;

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

static bool isExposed(Function *fun, FunctionTableTy& functions, unsigned aidx) {

  auto fsearch = functions.find(fun);
  assert(fsearch != functions.end());
  
  FunctionState& fstate = fsearch->second;
  assert(aidx < fstate.exposed.size());
  return fstate.exposed[aidx];
}

static void analyzeFunction(FunctionState *fstate, FunctionTableTy& functions, FunctionListTy& functionsWorkList, FunctionsSetTy& allocatingFunctions) {

  Function *fun = fstate->fun;
  unsigned nvars = fstate->varIndex.size();
  unsigned nargs = fstate->argIndex.size();
  bool updated = false;
  
  ArgsTy exposed(nargs, false);
  ArgsTy exposedAndUsed(nargs, false);
  
  BlocksTy blocks;
  BlockWorkListTy workList;
  
  BasicBlock *entryb = &fun->getEntryBlock();
  blocks.insert({entryb, BlockState(nargs, nvars)}); 
  workList.push_back(&fun->getEntryBlock()); 
  
  while(!workList.empty()) {
    BasicBlock *bb = workList.back();
    workList.pop_back();
    
    auto bsearch = blocks.find(bb);
    assert(bsearch != blocks.end());
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
      
      if (LoadInst *li = dyn_cast<LoadInst>(li)) {
        if (AllocaInst *var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // load of var
          unsigned vidx = fstate->varIndex.indexOf(var);
          int varState = s.vars[vidx];
          if (varState >= 0) {
            // variable holds a value from an argument
            unsigned aidx = varState;
            if (s.exposed[aidx]) {
              s.exposedAndUsed[aidx] = true;
            }
          }
        }
        continue;
      }
      
      CallSite cs(in);
      if (cs && cs.getCalledFunction() && allocatingFunctions.find(cs.getCalledFunction()) != allocatingFunctions.end()) {
        ArgsTy protects = protectedArgs(s.pstack, nargs);
        for(CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg.end(); ai != ae; ++ai) {
          Value* val = *ai;
          if (Argument *arg = dyn_cast<Argument>(val)) {
            unsigned aidx = fstate->argIndex.indexOf(arg);
            if (isSEXP(arg.getType()) && !protects[aidx] && isExposed(cs.getCalledFunction(), functions, aidx)) { // passing an unprotected argument directly
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
                if (isSEXP(arg.getType()) && !protects[aidx] && isExposed(cs.getCalledFunction(), functions, aidx)) {
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
      
      if (cs && cs.getCalledFunction() && cs.getCalledFunction()->getName() == "Rf_protect" || cs.getCalledFunction()->getName() == "R_ProtectWithIndex") {
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
      if (cs && cs.getCalledFunction() && cs.getCalledFunction()->getName() == "Rf_unprotect") {
        Value *val = cs.getArgument(0);
        if (ConstantInt* ci = dyn_cast<ConstantInt>(arg)) {
          uint64_t val = ci->getZExtValue();
          while(val--) {
            s.pstack.pop_back();
          }
          if (val) {
            // FIXME: report some warning/confused
          }
        }
      }
    }
    TerminatorInst *t = bb->getTerminator();
    for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
      BasicBlock *succ = t->getSuccessor(i);
      ssearch = blocks.find(succ);
      if (ssearch == blocks.end()) {
      
        // not yet explored block
        BlockState sstate(s.pstack, s.exposed, s.exposedAndUsed, s.vars, true);
        blocks.insert(sstate);
        workList.push_back({succ, sstate});
      } else {
        BlockState& pstate = ssearch->second;
        
        // merge
        pstate.merge(s);
        workList.push_back({succ, pstate});
      }
    }
    
    if (ReturnInst::classof(t)) {
      updated = updated || fstate->merge(s.exposed, s.exposedAndUsed);
    }
  }
  
  if (updated) {
    // mark dirty all functions calling this function
    for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
      User *u = *ui;

      if (BasicBlock *bb = dyn_cast<BasicBlock>(u->getParent())) {
        Function *pf = bb->getParent();
        auto fsearch = functions.find(pf);
        assert(fsearch != functions.end());
        FunctionState *pinfo = fsearch->second;
        if (!pinfo->dirty) {
          pinfo->dirty = true;
          functionsWorkList.insert(pinfo);
        }
      }
    }
  }
}


FunctionsSetTy findCalleeProtectFunctions(Module *m) {

  FunctionTableTy functions; // function envelopes
  FunctionListTy workList; // functions to be re-analyzed
  
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *f = *fi;
    auto finsert = functions.insert({f, fstate(f)});
    assert(finsert->second)
    addToWorkList(&*finsert->first);
  }
  
  FunctionsSetTy allocatingFunctions;
  findAllocatingFunctions(allocatingFunctions);
  
  while(!workList.empty()) {
    FunctionState* fstate = workList.pop_back();

    analyzeFunction(fstate, functions, workList, allocatingFunctions);
    fstate->dirty = false;
      // a function may be recursive
      //   but then it does not have to be re-analyzed just because of 
      //   that it has been re-analyzed
  }  
  
  FunctionsSetTy cprotect;
  for(FunctionTableTy::iterator fi = functions.begin(), fe = functions.end(); fi != fe; ++fi) {
    Function* fun = fi->first;
    FunctionState *fstate = fi->second;
    
    if (isNonTriviallyCalleeProtect(fun)) {
      errs() << "Function " << fun << " is callee-protect.\n";
      cprotect.insert(fun);
    }
  }
}


// get the maximum number of arguments accepted by any function (ignoring ellipsis)

unsigned maxNumberOfArguments(Module *m) {

  unsigned max = 0;
  
  for(Module::

}