
#include <unordered_set>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>

using namespace llvm;

typedef std::unordered_set<BasicBlock*> BasicBlocksSetTy;
typedef std::unordered_set<Function*> FunctionsSetTy;
