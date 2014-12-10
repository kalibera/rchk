
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
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "errors.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  SMDiagnostic error;
  
  std::string fname;
  if (argc == 2) {
    fname = argv[1];
  } else {
    fname = "R.bin.bc";
    outs() << "Input file not specified, using the default " << fname << "\n";
  }
  
  errs() << "Input file: " << fname << "\n";

  Module *m = ParseIRFile(fname, error, context);
  if (!m) {
    error.print("errcheck", errs());
    return 1;
  }

  FunctionsSetTy errorFunctions;
  findErrorFunctions(m, errorFunctions);
  
  for(Module::iterator FI = m->begin(), FE = m->end(); FI != FE; ++FI) {
      Function *fun = FI;

    if (!fun) continue;
    if (!fun->size()) continue;
    
    if (errorFunctions.find(fun) != errorFunctions.end()) {

      // FIXME: newer versions of llvm have getDISubprogram(Function*)
      
      Instruction *instWithDI = NULL;
      for(Function::iterator bb = fun->begin(), bbe = fun->end(); !instWithDI && bb != bbe; ++bb) {
        for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); !instWithDI && in != ine; ++in) {
          if (!in->getDebugLoc().isUnknown()) {
            instWithDI = in;
          }
        }
      }
      
      if (fun->doesNotReturn()) {
        errs() << "Marked (noreturn) ";
      } else {
        errs() << "UNMARKED ";
      }
      
      errs() << "error function " << fun->getName();
      
      if (instWithDI) {
        DebugLoc debugLoc = instWithDI->getDebugLoc().getFnDebugLoc(context);
        DILocation loc(debugLoc.getScopeNode(context));
        errs() << " " << loc.getDirectory() << "/" << loc.getFilename() << ":" << debugLoc.getLine() << "\n";
      } else {
        errs() << "\n";
      }
    }
  }
} 
