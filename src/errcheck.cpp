
/* 
   Check if there are functions that do not return, but are not marked as
   noreturn (e.g. error call wrappers)
*/
 
#include "common.h" 
 
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h> 
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "errors.h"

using namespace llvm;

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
        errs() << "Marked (noreturn) error function " << funName(fun) << " " << funLocation(fun) << "\n";
      } else {
        outs() << "UNMARKED error function " << funName(fun) << " " << funLocation(fun) << "\n";
      }

    } else {
      if (fun->doesNotReturn()) {
        outs() << "WARNING - returning function marked noerror - " << funName(fun) << " " << funLocation(fun) << "\n";
      }
    }
  }
  delete m;
} 
