#ifndef RCHK_COMMON_H
#define RCHK_COMMON_H

#include <set>
#include <unordered_set>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>

using namespace llvm;

typedef std::unordered_set<BasicBlock*> BasicBlocksSetTy;
typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::set<Function*> FunctionsOrderedSetTy;

Module *parseArgsReadIR(int argc, char* argv[], FunctionsOrderedSetTy& functionsOfInterest, LLVMContext& context);

std::string demangle(std::string name);

bool sourceLocation(const Instruction *in, std::string& path, unsigned& line);
std::string sourceLocation(const Instruction *in);

#endif
