
#include "allocators.h"
#include "exceptions.h"
#include "patterns.h"

using namespace llvm;

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

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
  return fsearch->second.index;
}

// Possible allocators are (all) functions that may be returning a pointer
// to a fresh R object (object allocated inside the call to that function). 
// There may be false positives: some possible allocators may not in fact be
// returning a fresh pointer at all or under certain conditions.  But, all
// functions possibly returning a fresh pointer should be identified.

void findPossiblyReturnedVariables(Function *f, VarsSetTy& possiblyReturned) {

  if (f->getReturnType()->isVoidTy()) {
    return;
  }
  if (DEBUG) errs() << "Function " << funName(f) << "...\n";
  
  // insert variables values of which are directly returned
  for(inst_iterator ini = inst_begin(*f), ine = inst_end(*f); ini != ine; ++ini) {
    Instruction *in = &*ini;
    if (ReturnInst::classof(in)) {
      Value* returnOperand = cast<ReturnInst>(in)->getReturnValue();

      ValuesSetTy vorig = valueOrigins(returnOperand); 
      for(ValuesSetTy::iterator vi = vorig.begin(), ve = vorig.end(); vi != ve; ++vi) { 
        Value *v = *vi;
        if (AllocaInst* var = dyn_cast<AllocaInst>(v)) {
          possiblyReturned.insert(var);
          if (DEBUG) errs() << "  directly returned " << varName(var) << "(" << *var << ")\n";
        }
      }
    }
  }
  
  // insert variables values of which may be stored into possibly returned
  // variables
  
  bool addedVar = true;
  while (addedVar) {
    addedVar = false;
    
    for(inst_iterator ini = inst_begin(*f), ine = inst_end(*f); ini != ine; ++ini) {
      Instruction *in = &*ini;
      if (StoreInst::classof(in)) {
        Value *storePointerOperand = cast<StoreInst>(in)->getPointerOperand();
        if (!AllocaInst::classof(storePointerOperand)) continue;
        
        AllocaInst* dst = cast<AllocaInst>(storePointerOperand);
        if (possiblyReturned.find(dst) == possiblyReturned.end()) {
          continue;
        }
        
        ValuesSetTy vorig = valueOrigins(cast<StoreInst>(in)->getValueOperand());
        for(ValuesSetTy::iterator vi = vorig.begin(), ve = vorig.end(); vi != ve; ++vi) { 
          Value *v = *vi;
          if (AllocaInst* src = dyn_cast<AllocaInst>(v)) {
            if (possiblyReturned.find(src) == possiblyReturned.end()) {
              possiblyReturned.insert(src);
              addedVar = true;
              if (DEBUG) errs() << "  indirectly returned " << varName(src) << " through " << varName(dst) << " store " << *in << "\n";
            }
          }
        }
      }
    }
  }
}

// this ignores derived/cast values
static bool valueMayBeReturned(Value* v, VarsSetTy& possiblyReturned) {

  for(Value::user_iterator ui = v->user_begin(), ue = v->user_end(); ui != ue; ++ui) {
    User *u = *ui;
    if (ReturnInst::classof(u)) {
      if (DEBUG) errs() << "  callsite result is returned directly\n";
      return true;
    }
    if (GetElementPtrInst::classof(u) || BitCastInst::classof(u)) { // go through casts and (some) derived assignments
      if (valueMayBeReturned(u, possiblyReturned)) {
        return true;
      }
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

// returns a set of functions such that if any of them was an allocator,
// this function f could also have been an allocator
//
// returns an empty set if this function cannot be an allocator

void getWrappedAllocators(Function *f, FunctionsSetTy& wrappedAllocators, Function* gcFunction) {
  if (!isSEXP(f->getReturnType())) return; // allocator must return SEXP

  VarsSetTy possiblyReturnedVars;
  findPossiblyReturnedVariables(f, possiblyReturnedVars);
      
  for(Function::iterator bb = f->begin(), bbe = f->end(); bb != bbe; ++bb) {
    for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); in != ine; ++in) {
      Value *v = in;
      CallSite cs(v);
      if (!cs) continue;
      Function *tgt = cs.getCalledFunction();
      if (tgt == gcFunction) {
        // an exception: treat a call to R_gc_internal as an indication this is a direct allocator
        // (note: R_gc_internal itself does not return an SEXP)
        if (DEBUG) errs() << "SEXP function " << funName(f) << " calls directly into " << funName(tgt) << "\n";
        wrappedAllocators.insert(tgt);
        continue;
      }
      if (isCallThroughPointer(v) && valueMayBeReturned(v, possiblyReturnedVars)) {
        if (DEBUG) errs() << "SEXP function " << funName(f) << " calls through a pointer, asserted to call gc function\n";
        wrappedAllocators.insert(gcFunction);
        continue;
      }
      if (!tgt) continue;
      if (!isSEXP(tgt->getReturnType())) continue;
      if (isKnownNonAllocator(tgt)) continue;
        
      // tgt is a function returning an SEXP, check if the result may be returned by function f
      if (valueMayBeReturned(v, possiblyReturnedVars)) {
        if (DEBUG) errs() << "SEXP function " << funName(f) << " wraps functions " << funName(tgt) << "\n";
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
    getWrappedAllocators(f, wrappedAllocators, gcFunction);
    if (!wrappedAllocators.empty()) {
      onlyEdges.insert({f, new FunctionsSetTy(wrappedAllocators)});
      onlyFunctions.insert(f);
    }
  }
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */, &onlyFunctions, &onlyEdges, gcFunction /* assume external functions allocate */);

  for(CallEdgesMapTy::iterator cei = onlyEdges.begin(), cee = onlyEdges.end(); cei != cee; ++cei) {
    delete cei->second;
  }
  
  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);

  for(FunctionsInfoMapTy::iterator fi = functionsMap.begin(), fe = functionsMap.end(); fi != fe; ++fi) {
    Function *f = const_cast<Function *>(fi->second.function);
    if (!f) continue;

    if ((fi->second.callsFunctionMap)[gcFunctionIndex]) {
      possibleAllocators.insert(f);
    }
  }
  
  possibleAllocators.insert(gcFunction);
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
  FunctionInfo& finfo = fsearch->second;

  return (finfo.callsFunctionMap)[gcFunctionIndex];
}

void findAllocatingFunctions(Module *m, FunctionsSetTy& allocatingFunctions) {

  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */, NULL, NULL, getGCFunction(m) /* assume external functions allocate */);

  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);

  for(FunctionsInfoMapTy::iterator fi = functionsMap.begin(), fe = functionsMap.end(); fi != fe; ++fi) {
    Function *f = const_cast<Function *>(fi->second.function);
    if (!f) continue;

    if ((fi->second.callsFunctionMap)[gcFunctionIndex]) {
      allocatingFunctions.insert(f);
    }
  }
  
  allocatingFunctions.insert(getGCFunction(m));
}
