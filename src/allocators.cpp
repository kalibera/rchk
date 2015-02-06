/* 
  This tool attempts to detect "allocators". An allocator is a function that
  returns a newly allocated pointer.  An allocator may indeed be a wrapper
  for other allocators, so there is a lot of allocators in the R source code.

*/
       
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/InstIterator.h>
#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "cgclosure.h"

const std::string gcFunction = "R_gc_internal";

using namespace llvm;

const bool DEBUG = false;

typedef std::unordered_set<AllocaInst*> VarsSetTy;

void findPossiblyReturnedVariables(Function& f, VarsSetTy& possiblyReturned) {

  if (DEBUG) outs() << "Function " << f.getName() << "...\n";
  
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
          if (DEBUG) outs() << "  directly returned " << var->getName() << "(" << *var << ")\n";
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
          if (DEBUG) outs() << "  indirectly returned " << varSrc->getName() << " through " << varDst->getName() 
            << " via load " << *storeValueOperand << " and store " << *in << "\n";
        }
      }
    }
  }
}

bool valueMayBeReturned(Value* v, VarsSetTy& possiblyReturned) {

  for(Value::user_iterator ui = v->user_begin(), ue = v->user_end(); ui != ue; ++ui) {
    User *u = *ui;
    if (ReturnInst::classof(u)) {
      if (DEBUG) outs() << "  callsite result is returned directly\n";  
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
       
        if (DEBUG) outs() << "  callsite result is returned indirectly through variable " << *(cast<AllocaInst>(storePointer)) << "\n";
        return true;
      }
    }
  }
  return false;
}

bool mayBeAllocator(Function& f) {
  if (!isSEXP(f.getReturnType())) return false; // allocator must return SEXP

  VarsSetTy possiblyReturnedVars; // true if value from this variable may be returned
  
  findPossiblyReturnedVariables(f, possiblyReturnedVars);
      
  for(Function::iterator bb = f.begin(), bbe = f.end(); bb != bbe; ++bb) {
    for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); in != ine; ++in) {
      CallSite cs(cast<Value>(in));
      if (!cs) continue;
      Function *tgt = cs.getCalledFunction();
      if (!tgt) continue;
      if (!isSEXP(tgt->getReturnType())) continue;
        
      // tgt is a function returning an SEXP, check if the result may be returned by function f
      if (valueMayBeReturned(cast<Value>(in), possiblyReturnedVars)) {
        if (DEBUG) outs() << "  has callsite" << *in << "\n";  
        return true;
      }
    }
  }
  return false;
}

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  FunctionsSetTy possibleAllocators;
  
  for(Module::iterator f = m->begin(), fe = m->end(); f != fe; ++f) {
    if (mayBeAllocator(*f)) {
      possibleAllocators.insert(f);
    }
  }
  
  Function *gcf = m->getFunction(gcFunction);
  if (!gcf) {
    outs() << "Cannot find function " << gcFunction << ".\n";
    return 1;
  }
  possibleAllocators.insert(gcf);
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */, &possibleAllocators);
  
  auto fsearch = functionsMap.find(gcf);
  if (fsearch == functionsMap.end()) {
    outs() << "Cannot find function info in callgraph for function " << gcFunction << ", internal error?\n";
    return 1;
  }
  unsigned gcFunctionIndex = fsearch->second->index;

  for(FunctionsInfoMapTy::iterator fi = functionsMap.begin(), fe = functionsMap.end(); fi != fe; ++fi) {
    Function const *f = fi->second->function;
    if (!f) continue;
    
    if ((*fi->second->callsFunctionMap)[gcFunctionIndex]) {
      errs() << "POSSIBLE ALLOCATOR: " << f->getName() << "\n";
    }
  }
  
  delete m;
}
