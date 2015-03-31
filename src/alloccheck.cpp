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

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "allocators.h"
#include "callocators.h"
#include "errors.h"


using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;

  FunctionsOrderedSetTy functionsOfInterest;
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  CalledModuleTy *cm = CalledModuleTy::create(m);

  FunctionsSetTy *possibleAllocators = cm->getPossibleAllocators();
  FunctionsSetTy *allocatingFunctions = cm->getAllocatingFunctions();
  const CalledFunctionsIndexTy* calledFunctions = cm->getCalledFunctions();

  if (0) {  
    outs() << "Detected called functions: \n";
    for(CalledFunctionsIndexTy::const_iterator fi = calledFunctions->begin(), fe = calledFunctions->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "  called function " << funName(f) << "\n";
    }
  }

  const CalledFunctionsSetTy *possibleCAllocators = cm->getPossibleCAllocators();
  const CalledFunctionsSetTy *allocatingCFunctions = cm->getAllocatingCFunctions();
  
  if(1) {
    for(CalledFunctionsSetTy::const_iterator fi = possibleCAllocators->begin(), fe = possibleCAllocators->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "C-ALLOCATOR: " << funName(f) << "\n";
    }
  }

  if(1) {
    for(CalledFunctionsSetTy::const_iterator fi = allocatingCFunctions->begin(), fe = allocatingCFunctions->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "C-ALLOCATING: " << funName(f) << "\n";
    }
  }

  if(1) {
    for(FunctionsSetTy::iterator fi = possibleAllocators->begin(), fe = possibleAllocators->end(); fi != fe; ++fi) {
      Function *f = *fi;
      if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "ALLOCATOR: " << funName(f) << "\n";
    }
  }
  

  if(1) {
    for(FunctionsSetTy::iterator fi = allocatingFunctions->begin(), fe = allocatingFunctions->end(); fi != fe; ++fi) {
      Function *f = *fi;
      if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "ALLOCATING: " << funName(f) << "\n";
    }
  }
  
  // check for which functions the context gave more precise result
  if (1) {  
    for(CalledFunctionsIndexTy::const_iterator fi = calledFunctions->begin(), fe = calledFunctions->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      bool callocator = possibleCAllocators->find(f) != possibleCAllocators->end();
      bool callocating = allocatingCFunctions->find(f) != allocatingCFunctions->end();
      bool allocator = possibleAllocators->find(f->fun) != possibleAllocators->end();
      bool allocating = allocatingFunctions->find(f->fun) != allocatingFunctions->end();
      
      if (!callocator && allocator) {
        outs() << "GOOD: NOT-CALLOCATOR but ALLOCATOR: " << funName(f) << "\n";
      }
      if (!callocating && allocating) {
        outs() << "GOOD: NOT-CALLOCATING but ALLOCATING: " << funName(f) << "\n";
      }
      if (callocator && !callocating) {
        outs() << "ERROR: NOT-CALLOCATING but CALLOCATOR: " << funName(f) << "\n";
      }
      if (allocator && !allocating) {
        outs() << "ERROR: NOT-ALLOCATING but ALLOCATOR: " << funName(f) << "\n";
      }
      if (callocator && !allocator) {
        outs() << "ERROR: C-ALLOCATOR but not ALLOCATOR: " << funName(f) << "\n";
      }
      if (callocating && !allocating) {
        outs() << "ERROR: C-ALLOCATING but not ALLOCATING: " << funName(f) << "\n";
      }
    }
  }

  CalledModuleTy::release(cm);  
  delete m;
}
