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

enum ArgExpKind {
  AK_NOALLOC = 0,  // no allocation
  AK_ALLOCATING,   // allocation, but not returning a fresh object
  AK_FRESH         // allocation and possibly returning a fresh object
};

ArgExpKind classifyArgumentExpression(Value *arg, FunctionsInfoMapTy& functionsMap, unsigned gcFunctionIndex, FunctionsSetTy& possibleAllocators) {

  if (!CallInst::classof(arg)) {
    // argument does not come (immediatelly) from a call
    return AK_NOALLOC;
  }

  CallInst *cinst = cast<CallInst>(arg);
  Function *fun = cinst->getCalledFunction();
  if (!fun) {
    return AK_NOALLOC;
  }

  if (!isAllocatingFunction(fun, functionsMap, gcFunctionIndex)) {
    // argument does not come from a call to an allocating function
    return AK_NOALLOC;
  }

  if (possibleAllocators.find(fun) != possibleAllocators.end()) {
    // the argument allocates and returns a fresh object
    return AK_FRESH;
  }
  return AK_ALLOCATING;
}


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

          ArgExpKind k;
          
          if (PHINode::classof(o)) {

            // for each argument comming from a PHI node, take the most
            // difficult kind of allocation (this is an approximation, the
            // most difficult combination of different arguments may not be
            // possible).

            PHINode* phi = cast<PHINode>(o);
            unsigned nvals = phi->getNumIncomingValues();
            k = AK_NOALLOC;
            for(unsigned i = 0; i < nvals; i++) {
              ArgExpKind cur = classifyArgumentExpression(phi->getIncomingValue(i), functionsMap, gcFunctionIndex, possibleAllocators);
              if (cur > k) {
                k = cur;
              }
            }
          } else {
            k = classifyArgumentExpression(o, functionsMap, gcFunctionIndex, possibleAllocators);
          }

          if (k >= AK_ALLOCATING) nAllocatingArgs++;
          if (k >= AK_FRESH) nFreshObjects++;
        }
        
        if (nAllocatingArgs >= 2 && nFreshObjects >= 1 ) {
          outs() << "WARNING Suspicious call (two or more unprotected arguments) to " << funName(middleFinfo->function) <<
            " at " << funName(finfo->function) << " " << sourceLocation(inst) << "\n";
        }
      }
    }
  }

  releaseMap(functionsMap);
  delete m;
}
