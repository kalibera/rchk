/*
  Check which source lines of the C code of GNU-R may call into a GC (are a
  safepoint). 
  
  By default this ignores error paths, because due to runtime checking,
  pretty much anything then would be a safepoint.
  
  This is a more precise variant of sfpcheck, allocator detection is
  somewhat context-aware (e.g.  taking into account some constant arguments
  being passed to functions, which makes a big difference for calls like
  getAttrib)
*/

#include "common.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "callocators.h"
#include "lannotate.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;

  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);
  CalledModuleTy *cm = CalledModuleTy::create(m);

  const CallSiteTargetsTy *callSiteTargets = cm->getCallSiteTargets();
  const CalledFunctionsSetTy *allocatingCFunctions = cm->getAllocatingCFunctions();
  
  LinesTy sfpLines;
  
  for(CallSiteTargetsTy::const_iterator ci = callSiteTargets->begin(), ce = callSiteTargets->end(); ci != ce; ++ci) {
    Value *inst = ci->first;
    Function *csFun = cast<Instruction>(inst)->getParent()->getParent();
    if (functionsOfInterestSet.find(csFun) == functionsOfInterestSet.end()) {
        continue;
    }
    const CalledFunctionsSetTy& funcs = ci->second;
    
    for(CalledFunctionsSetTy::const_iterator fi = funcs.begin(), fe = funcs.end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      
      if (allocatingCFunctions->find(f) != allocatingCFunctions->end()) {
        annotateLine(sfpLines, cast<Instruction>(inst));
      }
    }
  }

  printLineAnnotations(sfpLines);
  
  CalledModuleTy::release(cm);  
  delete m;
}  

