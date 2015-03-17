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
  CalledFunctionsVectorTy* calledFunctions = cm->getCalledFunctions();

  if (0) {  
    errs() << "Detected called functions: \n";
    for(CalledFunctionsVectorTy::iterator fi = calledFunctions->begin(), fe = calledFunctions->end(); fi != fe; ++fi) {
      CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      errs() << "  called function " << f->getName() << "\n";
    }
  }

  CalledFunctionsSetTy *possibleCAllocators = cm->getPossibleCAllocators();
  CalledFunctionsSetTy *allocatingCFunctions = cm->getAllocatingCFunctions();
  
  if(1) {
    for(CalledFunctionsSetTy::iterator fi = possibleCAllocators->begin(), fe = possibleCAllocators->end(); fi != fe; ++fi) {
      CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      errs() << "C-ALLOCATOR: " << f->getName() << "\n";
    }
  }

  if(1) {
    for(CalledFunctionsSetTy::iterator fi = allocatingCFunctions->begin(), fe = allocatingCFunctions->end(); fi != fe; ++fi) {
      CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      errs() << "C-ALLOCATING: " << f->getName() << "\n";
    }
  }

  if(1) {
    for(FunctionsSetTy::iterator fi = possibleAllocators->begin(), fe = possibleAllocators->end(); fi != fe; ++fi) {
      Function *f = *fi;
      if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
        continue;
      }
      errs() << "ALLOCATOR: " << f->getName() << "\n";
    }
  }
  

  if(1) {
    for(FunctionsSetTy::iterator fi = allocatingFunctions->begin(), fe = allocatingFunctions->end(); fi != fe; ++fi) {
      Function *f = *fi;
      if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
        continue;
      }
      errs() << "ALLOCATING: " << f->getName() << "\n";
    }
  }
  
  // check for which functions the context gave more precise result
  if (1) {  
    for(CalledFunctionsVectorTy::iterator fi = calledFunctions->begin(), fe = calledFunctions->end(); fi != fe; ++fi) {
      CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      bool callocator = possibleCAllocators->find(f) != possibleCAllocators->end();
      bool callocating = allocatingCFunctions->find(f) != allocatingCFunctions->end();
      bool allocator = possibleAllocators->find(f->fun) != possibleAllocators->end();
      bool allocating = allocatingFunctions->find(f->fun) != allocatingFunctions->end();
      
      if (!callocator && allocator) {
        errs() << "GOOD: NOT-CALLOCATOR but ALLOCATOR: " << f->getName() << "\n";
      }
      if (!callocating && allocating) {
        errs() << "GOOD: NOT-CALLOCATING but ALLOCATING: " << f->getName() << "\n";
      }
      if (callocator && !callocating) {
        errs() << "ERROR: NOT-CALLOCATING but CALLOCATOR: " << f->getName() << "\n";
      }
      if (allocator && !allocating) {
        errs() << "ERROR: NOT-ALLOCATING but ALLOCATOR: " << f->getName() << "\n";
      }
      if (callocator && !allocator) {
        errs() << "ERROR: C-ALLOCATOR but not ALLOCATOR: " << f->getName() << "\n";
      }
      if (callocating && !allocating) {
        errs() << "ERROR: C-ALLOCATING but not ALLOCATING: " << f->getName() << "\n";
      }
    }
  }

  CalledModuleTy::release(cm);  
  delete m;
}
