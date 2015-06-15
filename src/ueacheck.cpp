/* 
  Check for a particular pattern of errors (a super-set of multiple-allocating-argument-expressions - maacheck).
  Here, the arguments may also be read from unprotected variables.
  This tool is full of heuristics, it does have false alarms, but surprisingly found a good number of errors.
  Some of the false alarms can be avoided through simple fixes.
  
  The intended name for this was unescaped-argument-expressions.
*/

#include "common.h"
       
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

#include "allocators.h"
#include "cgclosure.h"

using namespace llvm;

const bool VERBOSE = false;

// FIXME: it might be better looking for an allocating store that is closest
// to the use, to reduce false alarms

StoreInst* getDominatingNonProtectingAllocatingStore(AllocaInst *v, const Instruction *useInst, FunctionsSetTy& possibleAllocators, DominatorTree& dominatorTree) {
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

    if (possibleAllocators.find(f) == possibleAllocators.end()) {
      continue;
    }
    if (!dominatorTree.dominates(s, useInst)) {
      continue;
    }
    // check that the value returned by the allocating call is not passed anywhere else
    if (ssrc->hasOneUse()) {
      return s;
    }
    if (ssrc->hasNUses(2)) {
      // also allow a store and a call to protect
      //   so that we can handle PROTECT(v = foo())

      Value::user_iterator ui = ssrc->user_begin();
      Value *u = *ui;
      if (u == s) {
        u = *++ui;
      }

      CallSite pcs(u);
      if (pcs && isProtectingFunction(pcs.getCalledFunction())) {
        return s;
      }
    }
  }
  return NULL;
}

// FIXME: there should be a way to offload this to capture (/escape) analysis
Instruction* getProtect(AllocaInst *v, const StoreInst *allocStore, const Instruction *useInst, DominatorTree& dominatorTree) {

  // look for PROTECT(var)
  for (Value::user_iterator ui = v->user_begin(), ue = v->user_end(); ui != ue; ++ui) {
    if (!LoadInst::classof(*ui)) {
      continue;
    }
    LoadInst *l = cast<LoadInst>(*ui);
    for(Value::user_iterator lui = l->user_begin(), lue = l->user_end(); lui != lue; ++lui) {
      Value *luv = *lui;
      CallSite cs(luv);
      if (!cs || !cs.getCalledFunction()) {
        continue;
      }
      if (!dominatorTree.dominates(cs.getInstruction(), useInst)) {
        continue;
      }
      if (!dominatorTree.dominates(allocStore, l)) {
        continue;
      }
      if (!isProtectingFunction(cs.getCalledFunction())) {
        continue;
      }
      return cs.getInstruction();
    }
  }

  // look for PROTECT(var = foo())
  //   in IR, the protect call may be on the result of foo directly without loading var
  Value* allocValue = const_cast<Value*>(allocStore->getValueOperand());
  if (!allocValue->hasOneUse()) {
    for(Value::user_iterator ui = allocValue->user_begin(), ue = allocValue->user_end(); ui != ue; ++ui) {
      Value *u = *ui;
      CallSite cs(u);
      if (!cs || !cs.getCalledFunction()) {
        continue;
      }
      if (!dominatorTree.dominates(cs.getInstruction(), useInst)) {
        continue;
      }
      if (!isProtectingFunction(cs.getCalledFunction())) {
        continue;
      }
      return cs.getInstruction();
    }
  }
  return NULL;
}

// FIXME: copy-paste from maacheck vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

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

// FIXME: copy-paste from maacheck ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


// this is approximative only
bool isLoadOfUnprotectedObject(Value *arg, Instruction *callInst, FunctionsSetTy& possibleAllocators, DominatorTree& dominatorTree) {
  if (!LoadInst::classof(arg)) {
    return false;
  }
  Value *v = cast<LoadInst>(arg)->getPointerOperand();

  if (!AllocaInst::classof(v) || !isSEXP(cast<AllocaInst>(v))) { // FIXME: does not handle phi nodes
    return false;
  }
  if (PointerMayBeCapturedBefore(v, false, true, callInst, &dominatorTree, true)) {
    return false;
  }
  StoreInst* allocStore = getDominatingNonProtectingAllocatingStore(cast<AllocaInst>(v), cast<LoadInst>(arg), possibleAllocators, dominatorTree);
  if (!allocStore) {
    return false;
  }
  Instruction* protect = getProtect(cast<AllocaInst>(v), allocStore, cast<LoadInst>(arg), dominatorTree);
  if (!protect) {
    if (VERBOSE) {
      outs() << "Variable " << *v << " may be unprotected in call " << sourceLocation(callInst) << " with allocation at  "
        << sourceLocation(allocStore) << "\n";
    }
    return true;
  }
  return false;
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
  findPossibleAllocators(m, possibleAllocators); // FIXME: use context-sensitive (more precise) allocator detection

  DominatorTreeWrapperPass dtPass;
  
  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    if (functionsOfInterest.find(FI->first) == functionsOfInterest.end()) {
      continue;
    }

    FunctionInfo& finfo = FI->second;
    if (finfo.function->empty()) {
      continue;
    }

    dtPass.runOnFunction(*const_cast<Function*>(finfo.function));
    DominatorTree& dominatorTree = dtPass.getDomTree();
    
    for(std::vector<CallInfo>::const_iterator CI = finfo.callInfos.begin(), CE = finfo.callInfos.end(); CI != CE; ++CI) {
      const CallInfo& cinfo = *CI;
      const FunctionInfo *middleFinfo = cinfo.target;
      const Instruction* inst = cinfo.instruction;
      
      unsigned nFreshObjects = 0;
      unsigned nAllocatingArgs = 0;
        
      for(unsigned u = 0, nop = inst->getNumOperands(); u < nop; u++) {
        Value* o = inst->getOperand(u);
          
        // FIXME: partial copy-paste from maacheck vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
          
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
            Value* incoming = phi->getIncomingValue(i);
            ArgExpKind cur = classifyArgumentExpression(incoming, functionsMap, gcFunctionIndex, possibleAllocators);
            if (isLoadOfUnprotectedObject(incoming, const_cast<Instruction*>(inst), possibleAllocators, dominatorTree)) {
              cur = AK_FRESH;
            }
            if (cur > k) {
              k = cur;
              if (cur == AK_FRESH) break; // no need to check more args
            }
          }
        } else {
          k = classifyArgumentExpression(o, functionsMap, gcFunctionIndex, possibleAllocators);
          if (isLoadOfUnprotectedObject(o, const_cast<Instruction*>(inst), possibleAllocators, dominatorTree)) {
            k = AK_FRESH;
          }
        }

        if (k >= AK_ALLOCATING) nAllocatingArgs++;
        if (k >= AK_FRESH) nFreshObjects++;

        if (VERBOSE) {
          if (k != AK_NOALLOC) {
            outs() << " Argument " << *o << " in call to " << funName(middleFinfo->function) << " is of kind " << k << 
            " at " << sourceLocation(inst)  << "\n";
          }
        }
        // FIXME: partial copy-paste from maacheck ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
      }
        
      if (nAllocatingArgs >= 2 && nFreshObjects >= 1) {
        outs() << "WARNING Suspicious call (two or more unprotected arguments) to " << funName(middleFinfo->function) <<
          " at " << funName(finfo.function) << " " << sourceLocation(inst) << "\n";
      }
    }
  }
  
  delete m;
}
