
#include "callocators.h"
#include "symbols.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

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
  return lhs->fun == rhs->fun && lhs->argInfo == rhs->argInfo;  // argInfos are interned
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

CalledFunctionTy* CalledModuleTy::getCalledFunction(Value *inst) {
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
  ArgInfosTy *argInfo = new ArgInfosTy(nargs, NULL);

  for(unsigned i = 0; i < nargs; i++) {
    Value *arg = cs.getArgument(i);
    if (LoadInst::classof(arg)) { // R_XSymbol
      Value *src = cast<LoadInst>(arg)->getPointerOperand();
      if (GlobalVariable::classof(src)) {
        auto ssearch = symbolsMap->find(cast<GlobalVariable>(src));
        if (ssearch != symbolsMap->end()) {
          (*argInfo)[i] = new SymbolArgInfoTy(ssearch->second);
          continue;
        }
      }
    }
    std::string symbolName;  // install("X")
    if (isInstallConstantCall(arg, symbolName)) {
      (*argInfo)[i] = new SymbolArgInfoTy(symbolName);
      continue;
    }
    // not a symbol, leave argInfo as NULL
  }
      
  // intern arginfo
      
  auto ainsert = argInfosTable.insert(argInfo);
  ArgInfosTy *ai = *ainsert.first;
  if (!ainsert.second) {
    delete argInfo;
  }

  // create called function
      
  CalledFunctionTy *calledFunction = new CalledFunctionTy(fun, ai);
      
  // intern (and remember) called function
  auto finsert = calledFunctionsTable.insert(calledFunction);
  CalledFunctionTy *cfun = *finsert.first;
  if (!finsert.second) {
    delete calledFunction;
  }
  return cfun;
}

CalledModuleTy::CalledModuleTy(Module *m, SymbolsMapTy *symbolsMap) : symbolsMap(symbolsMap) {

  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *fun = fi;

    for(Value::user_iterator ui = fun->user_begin(), ue = fun->user_end(); ui != ue; ++ui) {
      User *u = *ui;
      getCalledFunction(cast<Value>(u));
    }
  }  
}

CalledModuleTy::~CalledModuleTy() {

  // delete dynamically allocated elements in intern tables
  for(CalledFunctionsSetTy::iterator cfi = calledFunctionsTable.begin(), cfe = calledFunctionsTable.end(); cfi != cfe; ++cfi) {
    CalledFunctionTy *cfun = *cfi;
    delete cfun;
  }
  for(ArgInfosSetTy::iterator ai = argInfosTable.begin(), ae = argInfosTable.end(); ai != ae; ++ai) {
    ArgInfosTy *a = *ai;
    delete a;
  }
}
