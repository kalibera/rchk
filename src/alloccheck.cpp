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
#include "allocators.h"
#include "callocators.h"
#include "errors.h"


using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);

  // so far just a test for called allocators/called functions
  SymbolsMapTy symbolsMap;
  GlobalVarsSetTy symbolsSet;
  findSymbols(m, symbolsSet, &symbolsMap);

  FunctionsSetTy errorFunctions;
  findErrorFunctions(m, errorFunctions);
  
  GlobalsTy globals(m);
  
  FunctionsSetTy possibleAllocators;
  findPossibleAllocators(m, possibleAllocators);

  FunctionsSetTy allocatingFunctions;
  findAllocatingFunctions(m, allocatingFunctions);
      
  CalledModuleTy cm(m, &symbolsMap, &errorFunctions, &globals, &possibleAllocators, &allocatingFunctions);
  CalledFunctionsSetTy* calledFunctions = cm.getCalledFunctions();

  if (0) {  
  errs() << "Detected called functions: \n";
  for(CalledFunctionsSetTy::iterator cfi = calledFunctions->begin(), cfe = calledFunctions->end(); cfi != cfe; ++cfi) {
    CalledFunctionTy *cf = *cfi;
    errs() << "  called function " << cf->getName() << "\n";
  }
  }

  getCalledAllocators(&cm);

  if(1) {
  for(FunctionsSetTy::iterator fi = possibleAllocators.begin(), fe = possibleAllocators.end(); fi != fe; ++fi) {
    Function *f = *fi;
    if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
      continue;
    }
    errs() << "POSSIBLE ALLOCATOR: " << f->getName() << "\n";
  }
  }
  

  if(1) {
  for(FunctionsSetTy::iterator fi = allocatingFunctions.begin(), fe = allocatingFunctions.end(); fi != fe; ++fi) {
    Function *f = *fi;
    if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
      continue;
    }
    errs() << "ALLOCATING FUNCTION: " << f->getName() << "\n";
  }
  }
  
  delete m;
}
