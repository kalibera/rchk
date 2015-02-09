/* 
  Check for a particular pattern of errors (a super-set of multiple-allocating-argument-expressions - maacheck).
  Here, the arguments may also be read from unprotected variables.
  This tool is full of heuristics, it does have false alarms, but surprisingly found a good number of errors.
  Some of the false alarms can be avoided through simple fixes.
  
  The intended name for this was unescaped-argument-expressions.
*/
       
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Analysis/CaptureTracking.h>

#include <llvm/Support/GenericDomTree.h>
#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "allocators.h"
#include "cgclosure.h"

using namespace llvm;

// FIXME: it might be better looking for an allocating store that is closest
// to the use, to reduce false alarms

StoreInst* getDominatingNonProtectingAllocatingStore(AllocaInst *v, const Instruction *useInst, FunctionsInfoMapTy& functionsMap, unsigned gcFunctionIndex, DominatorTree& dominatorTree) {
  for (Value::user_iterator ui = v->user_begin(), ue = v->user_end(); ui != ue; ++ui) {
    if (!StoreInst::classof(*ui)) {
      continue;
    }
    StoreInst *s = cast<StoreInst>(*ui);
    if (s->getPointerOperand() != v) {
      continue;
    }
    Value *ssrc = s->getValueOperand();
    CallSite cs(ssrc);
    if (!cs) {
      continue;
    } 
    Function *f = cs.getCalledFunction();
    if (!f) continue;
    if (isInstall(f)) continue;  // this implicitly protects
    
    if (!isAllocatingFunction(cs.getCalledFunction(), functionsMap, gcFunctionIndex)) {
      continue;
    }
    if (!dominatorTree.dominates(s, useInst)) {
      continue;
    }
    // check that the value returned by the allocating call is not passed anywhere else
    if (ssrc->hasOneUse()) {
      return s;
    }
  }
  return NULL;
}

// FIXME: there should be a way to offload this to capture (/escape) analysis
Instruction* getProtect(AllocaInst *v, const Instruction *allocInst, const Instruction *useInst, FunctionsInfoMapTy& functionsMap, unsigned gcFunctionIndex, DominatorTree& dominatorTree) {
  for (Value::user_iterator ui = v->user_begin(), ue = v->user_end(); ui != ue; ++ui) {
    if (!LoadInst::classof(*ui)) {
      continue;
    }
    LoadInst *l = cast<LoadInst>(*ui);
    if (!l->hasOneUse()) {
      continue;
    }
    CallSite cs(l->user_back());
    if (!cs) {
      continue;
    } 
    if (!dominatorTree.dominates(cs.getInstruction(), useInst)) {
      continue;
    }
    if (!dominatorTree.dominates(allocInst, cs.getInstruction())) {
      continue;
    }
    if (!cs.getCalledFunction()) {
      continue;
    }
    std::string fname = cs.getCalledFunction()->getName();
    if (fname != "Rf_protect" && fname != "R_ProtectWithIndex") {
      continue;
    }
    return cs.getInstruction();
  }
  return NULL;
}


int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);  
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */);
  
  unsigned gcFunctionIndex = getGCFunctionIndex(functionsMap, m);
  DominatorTreeWrapperPass dtPass;
  
  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    if (functionsOfInterest.find(FI->first) == functionsOfInterest.end()) {
      continue;
    }

    FunctionInfo *finfo = FI->second;
    if (finfo->function->empty()) {
      continue;
    }

    dtPass.runOnFunction(*const_cast<Function*>(finfo->function));
    DominatorTree& dominatorTree = dtPass.getDomTree();
    
    for(std::vector<CallInfo*>::iterator CI = finfo->callInfos.begin(), CE = finfo->callInfos.end(); CI != CE; ++CI) {
      CallInfo* cinfo = *CI;
      for(std::set<FunctionInfo*>::iterator MFI = cinfo->targets.begin(), MFE = cinfo->targets.end(); MFI != MFE; ++MFI) {
        FunctionInfo *middleFinfo = *MFI;
        
        const Instruction* inst = cinfo->instruction;
        unsigned nFreshObjects = 0;
        unsigned nInstallCalls = 0;
        
        for(unsigned u = 0, nop = inst->getNumOperands(); u < nop; u++) {
          Value* o = inst->getOperand(u);
          
          // FIXME:: there may be implicit loads, such as
          //
          // PROTECT(x=foo())
          // bar(x)
          //
          // may not have a load for x
          
          if (LoadInst::classof(o)) {
            Value *v = cast<LoadInst>(o)->getPointerOperand();
            if (!AllocaInst::classof(v) || !isSEXP(cast<AllocaInst>(v))) {
              continue;
            }
            if (PointerMayBeCapturedBefore(v, false, true, inst, &dominatorTree, true)) {
              continue; 
            }
            Instruction* allocStore = getDominatingNonProtectingAllocatingStore(cast<AllocaInst>(v), cast<LoadInst>(o), functionsMap, gcFunctionIndex, dominatorTree);
            if (!allocStore) {
              continue;
            }
            Instruction* protect = getProtect(cast<AllocaInst>(v), allocStore, cast<LoadInst>(o), functionsMap, gcFunctionIndex, dominatorTree);
            if (!protect) {
              errs() << "Variable " << *v << " may be unprotected in call " << sourceLocation(inst) << " with allocation at  "
                << sourceLocation(allocStore) << "\n";
               
              nFreshObjects++;
            }
            continue;
          }
          
          if (!CallInst::classof(o)) continue; // argument does not come (immediatelly) from a call
          
          CallInst *cinst = cast<CallInst>(o);
          Function *fun = cinst->getCalledFunction();
          if (!fun) continue;
          
          if (!isSEXP(fun->getReturnType())) continue; // argument is not SEXP

          if (!isAllocatingFunction(fun, functionsMap, gcFunctionIndex)) {
            // argument does not come from a call to an allocating function
            continue;
          }
          if (isInstall(fun)) {
            nInstallCalls++;
          }
          nFreshObjects++;
        }
        
        if (nFreshObjects > 1 && nInstallCalls < nFreshObjects) {
          outs() << "WARNING Suspicious call (two or more unprotected arguments) at " << demangle(finfo->function->getName()) << " " 
            << sourceLocation(inst) << "\n";
        }
      }
    }
  }

  delete m;
}
