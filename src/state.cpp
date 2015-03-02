
#include "state.h"
#include "common.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

void StateBaseTy::dump(bool verbose) {
  errs() << "\n ###################### STATE DUMP " << this << " ######################\n";
  Function *fun = bb->getParent();
  errs() << "=== Function: " << ((fun == NULL) ? "NULL" : demangle(fun->getName())) << "\n";
  if (verbose) {
    errs() << "=== Basic block: \n" << *bb << "\n";
  }
      
  Instruction *in = bb->begin();
  errs() << "=== Basic block src: " << sourceLocation(in) << "\n";
}
