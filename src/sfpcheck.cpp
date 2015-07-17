/*
  Check which source lines of the C code of GNU-R may call into a GC (are a
  safepoint). 
  
  By default this ignores error paths, because due to runtime checking,
  pretty much anything then would be a safepoint.
*/

#include "common.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h> 
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "allocators.h"
#include "cgclosure.h"
#include "exceptions.h"
#include "lannotate.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */);
  
  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);
  
  errs() << "List of functions and callsites calling (recursively) into " << gcFunction << ":\n";

  std::string lastFile = "";
  std::string lastDirectory = "";
  unsigned lastLine = 0;

  LinesTy sfpLines;
    
  for(FunctionsVectorTy::iterator FI = functionsOfInterestVector.begin(), FE = functionsOfInterestVector.end(); FI != FE; ++FI) {

    auto fisearch = functionsMap.find(*FI);
    assert (fisearch != functionsMap.end());
    FunctionInfo& finfo = fisearch->second;

    for(std::vector<CallInfo>::const_iterator CI = finfo.callInfos.begin(), CE = finfo.callInfos.end(); CI != CE; ++CI) {
      const CallInfo& cinfo = *CI;
      const FunctionInfo *middleFinfo = cinfo.target;
        
      for(std::vector<FunctionInfo*>::const_iterator TFI = middleFinfo->calledFunctionsList.begin(), TFE = middleFinfo->calledFunctionsList.end(); TFI != TFE; ++TFI) {
        const FunctionInfo *targetFinfo = *TFI;
          
        if ((targetFinfo->callsFunctionMap)[gcFunctionIndex] && !isAssertedNonAllocating(const_cast<Function*>(targetFinfo->function))) {
          annotateLine(sfpLines, cinfo.instruction);        
        }
      }
    }
  }
  printLineAnnotations(sfpLines);
  delete m;
}
