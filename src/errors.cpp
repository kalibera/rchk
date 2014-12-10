
#include "errors.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

static bool checkAndAnalyzeErrorFunction(Function *fun, FunctionsSetTy& knownErrorFunctions, BasicBlocksSetTy& returningBlocks, bool onlyCheck) {

  BasicBlocksSetTy errorBlocks;
  BasicBlock *entry = &fun->getEntryBlock();

  for(Function::iterator bb = fun->begin(), bbe = fun->end(); bb != bbe; ++bb) {
    if (UnreachableInst::classof(bb->getTerminator())) {
      // this block ends by a call to a function with noreturn attribute
      errorBlocks.insert(bb);
      goto classified_block;
    }
    for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); in != ine; ++in) {
      CallSite cs(cast<Value>(in));
      if (cs) {
        Function *tgt = cs.getCalledFunction();
        if (knownErrorFunctions.find(tgt) != knownErrorFunctions.end()) {
          // this block calls into a function that does not return,
          // but does not have the noreturn attribute
          errorBlocks.insert(bb);
          goto classified_block;
        }
      }
    }
    if (ReturnInst::classof(bb->getTerminator())) {
      // this block has a return statement
      if (onlyCheck && entry == bb) {
        return false;
      }
      returningBlocks.insert(bb);
      goto classified_block;
    }
    
    classified_block: ;
  }
  
  // now add to returning blocks all blocks that can reach a returning block
  
  bool addedReturningBlock = !returningBlocks.empty();
  while(addedReturningBlock) {

    addedReturningBlock = false;
    for(Function::iterator bb = fun->begin(), bbe = fun->end(); bb != bbe; ++bb) {
      if (errorBlocks.find(bb) == errorBlocks.end() && 
        returningBlocks.find(bb) == returningBlocks.end()) {
        
        TerminatorInst *t = bb->getTerminator();
        for(int i = 0, nsucc = t->getNumSuccessors(); i < nsucc; i++) {
          BasicBlock *succ = t->getSuccessor(i);
          if (returningBlocks.find(succ) != returningBlocks.end()) {
            if (onlyCheck && entry == bb) {
              return false;
            }          
            returningBlocks.insert(bb);
            addedReturningBlock = true;
            break;
          }
        }
      }
    }
  }    
  // entry block is not a returning block    
  return returningBlocks.find(entry) == returningBlocks.end();
}


// an error function is a function in which no return instruction is
// reachable from the entry block

bool isErrorFunction(Function *fun, FunctionsSetTy& knownErrorFunctions) {

  BasicBlocksSetTy returningBlocks;
  return checkAndAnalyzeErrorFunction(fun, knownErrorFunctions, returningBlocks, true);
}

// returns a set of error basic blocks (those that always end up in an error, so from which
// the program never returns using the regular function return

void findErrorBasicBlocks(Function *fun, FunctionsSetTy& knownErrorFunctions, BasicBlocksSetTy& errorBlocks) {

  BasicBlocksSetTy returningBlocks;  
  checkAndAnalyzeErrorFunction(fun, knownErrorFunctions, returningBlocks, false);

  for(Function::iterator bb = fun->begin(), bbe = fun->end(); bb != bbe; ++bb) {
    if (returningBlocks.find(bb) == returningBlocks.end()) {
      errorBlocks.insert(bb);
    }
  }
}

// find all functions from module m that do not return, place them into
// errorFunctions

void findErrorFunctions(Module *m, FunctionsSetTy& errorFunctions) {

  bool addedErrorFunction = true;
  while(addedErrorFunction) {
    addedErrorFunction = false;
    for(Module::iterator FI = m->begin(), FE = m->end(); FI != FE; ++FI) {
      Function *fun = FI;

      if (!fun) continue;
      if (!fun->size()) continue;
    
      if (errorFunctions.find(fun) == errorFunctions.end() && isErrorFunction(fun, errorFunctions)) {
        errorFunctions.insert(fun);
        addedErrorFunction = true;
      }
    }
  }
}
