
#include "common.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "allocators.h"
#include "cgclosure.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);  
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, false /* ignore error paths */);

  Function *myf = m->getFunction("Rf_errorcall");
  if (!myf) {
    errs() << "Cannot find function to check.\n";
    exit(1);
  }
  
  unsigned myfindex = 0;
  
  auto fsearch = functionsMap.find(myf);
  if (fsearch == functionsMap.end()) {
    errs() << "Cannot find function info of function to check\n";
    exit(1);
  }
  myfindex = fsearch->second.index;  

  errs() << "Functions calling (recursively) function " << funName(myf) << "\n";
  for(FunctionsVectorTy::iterator FI = functionsOfInterestVector.begin(), FE = functionsOfInterestVector.end(); FI != FE; ++FI) {

    auto fisearch = functionsMap.find(*FI);
    assert (fisearch != functionsMap.end());
    FunctionInfo& finfo = fisearch->second;

    if ((finfo.callsFunctionMap)[myfindex]) {
      errs() << funName(finfo.function) << "\n";
    }
  }

  delete m;
}
