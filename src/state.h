#ifndef RCHK_STATE_H
#define RCHK_STATE_H

#include <llvm/IR/BasicBlock.h>

using namespace llvm;

struct StateBaseTy {
  BasicBlock *bb;
  
  virtual StateBaseTy* clone(BasicBlock *newBB) = 0;
  virtual bool add() = 0;
  void dump(bool verbose);
};

#endif
