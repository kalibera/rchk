/*
  Check which source lines of the C code of GNU-R may call into a GC (are a
  safepoint). 
  
  By default this ignores error paths, because due to runtime checking,
  pretty much anything then would be a safepoint.

  This tool doesn't use the cache.
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
#include "cginfo.h"
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
  buildCGInfo(m, functionsMap, true /* ignore error paths */);
  
  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);

  // we want: canReachGoodTarget[f] = exists g: reach(f,g) && reach(g,GC) && !assertedNonAllocating(g)

  // compute which functions can reach GC function
  CanReachVectorTy canReachGC = computeCanReachToIndex(functionsMap, gcFunctionIndex);

  // take only functions that can reach GC and are not asserted non-allocating
  std::vector<unsigned> goodTargets;
  for (const auto& [_, finfo] : functionsMap) {
    if (!canReachGC[finfo.index]) {
      continue;
    }
    if (!isAssertedNonAllocating(const_cast<Function*>(finfo.function))) {
      goodTargets.push_back(finfo.index);
    }
  }
  // compute which functions can reach any of the good targets
  CanReachVectorTy canReachGoodTarget = computeCanReachToAnyIndex(functionsMap, goodTargets);
  
  errs() << "List of functions and callsites calling (recursively) into " << gcFunction << ":\n";

  std::string lastFile = "";
  std::string lastDirectory = "";

  LinesTy sfpLines;
    
  for(FunctionsVectorTy::iterator FI = functionsOfInterestVector.begin(), FE = functionsOfInterestVector.end(); FI != FE; ++FI) {

    auto fisearch = functionsMap.find(*FI);
    myassert(fisearch != functionsMap.end());
    FunctionInfo& finfo = fisearch->second;

    for(std::vector<CallInfo>::const_iterator CI = finfo.callInfos.begin(), CE = finfo.callInfos.end(); CI != CE; ++CI) {
      const CallInfo& cinfo = *CI;
      const FunctionInfo *middleFinfo = cinfo.target;

      if (canReachGoodTarget[middleFinfo->index]) {
        annotateLine(sfpLines, cinfo.instruction);
      }
    }
  }
  printLineAnnotations(sfpLines);
  delete m;
}
