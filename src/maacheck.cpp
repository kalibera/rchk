/*
  Check for a particular call pattern, multiple allocating arguments, that
  is a common source of PROTECT errors.  Calls such as
  
  cons(install("x"), ScalarInt(1))
  
  where at least two arguments are given as immediate results of allocating
  functions and at least one of these functions returns a fresh allocated
  object.

  It does not matter that cons protects is arguments - if ScalarInt is
  evaluated before install, then install may allocate, thrashing that scalar
  integer.
  
  By default the checking ignores error paths.
*/

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "allocators.h"
#include "cgclosure.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);  
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */);
  
  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);
  
  FunctionsSetTy possibleAllocators;
  findPossibleAllocators(m, possibleAllocators);

  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    if (functionsOfInterest.find(FI->first) == functionsOfInterest.end()) {
      continue;
    }

    FunctionInfo *finfo = FI->second;

    for(std::vector<CallInfo*>::iterator CI = finfo->callInfos.begin(), CE = finfo->callInfos.end(); CI != CE; ++CI) {
      CallInfo* cinfo = *CI;
      for(std::set<FunctionInfo*>::iterator MFI = cinfo->targets.begin(), MFE = cinfo->targets.end(); MFI != MFE; ++MFI) {
        FunctionInfo *middleFinfo = *MFI;
        
        const Instruction* inst = cinfo->instruction;
        unsigned nFreshObjects = 0;
        unsigned nAllocatingArgs = 0;
        
        for(unsigned u = 0, nop = inst->getNumOperands(); u < nop; u++) {
          Value* o = inst->getOperand(u);
          
          if (!CallInst::classof(o)) continue; // argument does not come (immediatelly) from a call
          
          CallInst *cinst = cast<CallInst>(o);
          Function *fun = cinst->getCalledFunction();
          if (!fun) continue;
          
          if (!isSEXP(fun->getReturnType())) { // argument is not SEXP
            continue;
          }
          
          if (!isAllocatingFunction(fun, functionsMap, gcFunctionIndex)) {
            // argument does not come from a call to an allocating function
            continue;
          }

          if (!isInstall(fun) && possibleAllocators.find(fun) != possibleAllocators.end()) {
            // the argument allocates and returns a fresh object
            nFreshObjects++;
          }

          // the argument allocates
          nAllocatingArgs++;
        }
        
        if (nAllocatingArgs >= 2 && nFreshObjects >= 1 ) {
          outs() << "WARNING Suspicious call (two or more unprotected arguments) at " << demangle(finfo->function->getName()) << " " 
            << sourceLocation(inst) << "\n";
        }
      }
    }
  }

  delete m;
}
