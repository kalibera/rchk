/*
  This tool just prints symbol shortcuts - both global variables and static
  variables in functions.  It could be extended to do some checks on how
  symbols are defined, if necessary.
*/ 

#include "common.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "symbols.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
    // NOTE: functionsOfInterest ignored but (re-)analyzing the R core is necessary
  
  SymbolsMapTy symbolsMap;
  findSymbols(m, &symbolsMap);
  
  for(SymbolsMapTy::iterator si = symbolsMap.begin(), se = symbolsMap.end(); si != se; ++si) {
    GlobalVariable *gv = si->first;
    std::string& name = si->second;
    
    errs() << "  " << gv->getName() << "  \"" << name << "\"    " << "\n";
  }
  
  // FIXME: this could be extended to check for duplicate shortcuts
  // FIXME: the output could be sorted
  // FIXME: there could also be more detailed checks for ambiguous symbols (but I've not seen such in practice)
  
  delete m;
}
