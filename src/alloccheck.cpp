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

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);

  FunctionsSetTy possibleAllocators;
  findPossibleAllocators(m, possibleAllocators);
  
  for(FunctionsSetTy::iterator fi = possibleAllocators.begin(), fe = possibleAllocators.end(); fi != fe; ++fi) {
    Function *f = *fi;
    errs() << "POSSIBLE ALLOCATOR: " << f->getName() << "\n";
  }
  
  delete m;
}
