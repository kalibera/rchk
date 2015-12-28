/*
  This tool prints results of vector-related analyses, such as which
  functions and when return a vector.
*/ 

#include "common.h"
#include "callocators.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "vectors.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);
  CalledModuleTy *cm = CalledModuleTy::create(m);
  
    // FIXME: this will not discover many call-sites (will not include many interesting contexts)
  printVectorReturningFunctions(cm);
    
  delete m;
}
