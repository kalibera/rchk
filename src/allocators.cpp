
#include "allocators.h"

using namespace llvm;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/InstIterator.h>
#include <llvm/Support/raw_ostream.h>

const bool DEBUG = false;

Function *getGCFunction(Module *m) {

  Function *gcf = m->getFunction(gcFunction);
  if (!gcf) {
    errs() << "Cannot find function " << gcFunction << ".\n";
    exit(1);
  }
  return gcf;
}

unsigned getGCFunctionIndex(FunctionsInfoMapTy& functionsMap, Module *m) {

  auto fsearch = functionsMap.find(getGCFunction(m));
  if (fsearch == functionsMap.end()) {
    errs() << "Cannot find function info in callgraph for function " << gcFunction << ", internal error?\n";
    exit(1);
  }
  return fsearch->second->index;
}

// Possible allocators are (all) functions that may be returning a pointer
// to a fresh R object (object allocated inside the call to that function). 
// There may be false positives: some possible allocators may not in fact be
// returning a fresh pointer at all or under certain conditions.  But, all
// functions possibly returning a fresh pointer should be identified.

static void findPossiblyReturnedVariables(Function& f, VarsSetTy& possiblyReturned) {

  if (DEBUG) errs() << "Function " << f.getName() << "...\n";
  
  // insert variables values of which are directly returned
  for(inst_iterator ini = inst_begin(f), ine = inst_end(f); ini != ine; ++ini) {
    Instruction *in = &*ini;
    if (ReturnInst::classof(in)) {
      Value* returnOperand = cast<ReturnInst>(in)->getReturnValue();
      if (LoadInst::classof(returnOperand)) {
        Value* loadOperand = cast<LoadInst>(returnOperand)->getPointerOperand();
        if (AllocaInst::classof(loadOperand)) {
          AllocaInst* var = cast<AllocaInst>(loadOperand);
          possiblyReturned.insert(var);
          if (DEBUG) errs() << "  directly returned " << var->getName() << "(" << *var << ")\n";
        }
      }
    }
  }
  
  // insert variables values of which may be stored into possibly returned
  // variables
  
  bool addedVar = true;
  while (addedVar) {
    addedVar = false;
    
    for(inst_iterator ini = inst_begin(f), ine = inst_end(f); ini != ine; ++ini) {
      Instruction *in = &*ini;
      if (StoreInst::classof(in)) {
        Value *storePointerOperand = cast<StoreInst>(in)->getPointerOperand();
        if (!AllocaInst::classof(storePointerOperand)) continue;
        
        Value *storeValueOperand = cast<StoreInst>(in)->getValueOperand();
        if (!LoadInst::classof(storeValueOperand)) continue;
        Value *loadOperand = cast<LoadInst>(storeValueOperand)->getPointerOperand();
        if (!AllocaInst::classof(loadOperand)) continue;
        
        AllocaInst* varDst = cast<AllocaInst>(storePointerOperand);
        AllocaInst* varSrc = cast<AllocaInst>(loadOperand);
        
        if (possiblyReturned.find(varDst) != possiblyReturned.end() &&
          possiblyReturned.find(varSrc) == possiblyReturned.end()) {
          
          possiblyReturned.insert(varSrc);
          addedVar = true;
          if (DEBUG) errs() << "  indirectly returned " << varSrc->getName() << " through " << varDst->getName() 
            << " via load " << *storeValueOperand << " and store " << *in << "\n";
        }
      }
    }
  }
}

static bool valueMayBeReturned(Value* v, VarsSetTy& possiblyReturned) {

  for(Value::user_iterator ui = v->user_begin(), ue = v->user_end(); ui != ue; ++ui) {
    User *u = *ui;
    if (ReturnInst::classof(u)) {
      if (DEBUG) errs() << "  callsite result is returned directly\n";
      return true;
    }
    if (StoreInst::classof(u)) {
      Value* storeValue = cast<StoreInst>(u)->getValueOperand();
      Value* storePointer = cast<StoreInst>(u)->getPointerOperand();
      if (u == storePointer) {
        // the variable is overwritten
        continue;
      }
      if (AllocaInst::classof(storePointer) &&
          possiblyReturned.find(cast<AllocaInst>(storePointer)) != possiblyReturned.end()) {
       
        if (DEBUG) errs() << "  callsite result is returned indirectly through variable " << *(cast<AllocaInst>(storePointer)) << "\n";
        return true;
      }
    }
  }
  return false;
}

// some manually added exceptions that so far seem too hard to find automatically
bool isKnownNonAllocator(Function *f) {
  if (isInstall(f)) return true;
  return false;
}


// returns a set of functions such that if any of them was an allocator,
// this function f could also have been an allocator
//
// returns an empty set if this function cannot be an allocator

void getWrappedAllocators(Function& f, FunctionsSetTy& wrappedAllocators, Function* gcFunction) {
  if (!isSEXP(f.getReturnType())) return; // allocator must return SEXP

  VarsSetTy possiblyReturnedVars; // true if value from this variable may be returned
  findPossiblyReturnedVariables(f, possiblyReturnedVars);
      
  for(Function::iterator bb = f.begin(), bbe = f.end(); bb != bbe; ++bb) {
    for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); in != ine; ++in) {
      CallSite cs(cast<Value>(in));
      if (!cs) continue;
      Function *tgt = cs.getCalledFunction();
      if (tgt == gcFunction) {
        // an exception: treat a call to R_gc_internal as an indication this is a direct allocator
        // (note: R_gc_internal itself does not return an SEXP)
        if (DEBUG) errs() << "SEXP function " << f.getName() << " calls directly into " << tgt->getName() << "\n";
        wrappedAllocators.insert(tgt);
        continue;
      }
      if (!tgt) continue;
      if (!isSEXP(tgt->getReturnType())) continue;
      if (isKnownNonAllocator(tgt)) continue;
        
      // tgt is a function returning an SEXP, check if the result may be returned by function f
      if (valueMayBeReturned(cast<Value>(in), possiblyReturnedVars)) {
        if (DEBUG) errs() << "SEXP function " << f.getName() << " wraps functions " << tgt->getName() << "\n";
        wrappedAllocators.insert(tgt);
      }
    }
  }
}

void findPossibleAllocators(Module *m, FunctionsSetTy& possibleAllocators) {

  FunctionsSetTy onlyFunctions;
  CallEdgesMapTy onlyEdges;
  Function* gcFunction = getGCFunction(m);

  onlyFunctions.insert(gcFunction);
  for(Module::iterator f = m->begin(), fe = m->end(); f != fe; ++f) {

    if (isKnownNonAllocator(f)) {
      continue;
    }
    FunctionsSetTy wrappedAllocators;
    getWrappedAllocators(*f, wrappedAllocators, gcFunction);
    if (!wrappedAllocators.empty()) {
      onlyEdges.insert({f, new FunctionsSetTy(wrappedAllocators)});
      onlyFunctions.insert(f);
    }
  }
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */, &onlyFunctions, &onlyEdges);

  for(CallEdgesMapTy::iterator cei = onlyEdges.begin(), cee = onlyEdges.end(); cei != cee; ++cei) {
    delete cei->second;
  }
  
  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);

  for(FunctionsInfoMapTy::iterator fi = functionsMap.begin(), fe = functionsMap.end(); fi != fe; ++fi) {
    Function *f = const_cast<Function *>(fi->second->function);
    if (!f) continue;

    if ((*fi->second->callsFunctionMap)[gcFunctionIndex]) {
      possibleAllocators.insert(f);
    }
  }
}

bool isAllocatingFunction(Function *fun, FunctionsInfoMapTy& functionsMap, unsigned gcFunctionIndex) {
  if (!fun) {
    return false;
  }
  auto fsearch = functionsMap.find(const_cast<Function*>(fun));
  if (fsearch == functionsMap.end()) {
    // should not happen
    return false;
  }
  FunctionInfo *finfo = fsearch->second;

  return (*finfo->callsFunctionMap)[gcFunctionIndex];
}

void findAllocatingFunctions(Module *m, FunctionsSetTy& allocatingFunctions) {

  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */);

  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);

  for(FunctionsInfoMapTy::iterator fi = functionsMap.begin(), fe = functionsMap.end(); fi != fe; ++fi) {
    Function *f = const_cast<Function *>(fi->second->function);
    if (!f) continue;

    if ((*fi->second->callsFunctionMap)[gcFunctionIndex]) {
      allocatingFunctions.insert(f);
    }
  }
}
