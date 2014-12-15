
/* 
   Check if there are functions that do not return, but are not marked as
   noreturn (e.g. error call wrappers)
 */
 
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h> 
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "errors.h"

using namespace llvm;

void printFunctionInfo(Function* fun) {

  Instruction *instWithDI = NULL;
  for(Function::iterator bb = fun->begin(), bbe = fun->end(); !instWithDI && bb != bbe; ++bb) {
    for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); !instWithDI && in != ine; ++in) {
      if (!in->getDebugLoc().isUnknown()) {
        instWithDI = in;
      }
    }
  }
  if (instWithDI) {
    LLVMContext&context = instWithDI->getParent()->getContext();
    DebugLoc debugLoc = instWithDI->getDebugLoc().getFnDebugLoc(context);
    DILocation loc(debugLoc.getScopeNode(context));
    errs() << " " << loc.getDirectory() << "/" << loc.getFilename() << ":" << debugLoc.getLine() << "\n";
  } else {
    errs() << " no debug info found for " << fun << "\n";
  }

}

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  FunctionsSetTy errorFunctions;
  findErrorFunctions(m, errorFunctions);
  
  for(FunctionsOrderedSetTy::iterator fi = functionsOfInterest.begin(), fe = functionsOfInterest.end(); fi != fe; ++fi) {
    Function *fun = *fi;

    if (!fun) continue;
    if (!fun->size()) continue;
    
    if (errorFunctions.find(fun) != errorFunctions.end()) {

      // FIXME: newer versions of llvm have getDISubprogram(Function*)
      
      if (fun->doesNotReturn()) {
        errs() << "Marked (noreturn) ";
      } else {
        errs() << "UNMARKED ";
      }
      
      errs() << "error function " << fun->getName();
      printFunctionInfo(fun);

    } else {
      if (fun->doesNotReturn()) {
        errs() << "WARNING - returning function marked noerror ";
        printFunctionInfo(fun);
      }
    }
  }
  delete m;
} 
