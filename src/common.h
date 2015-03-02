#ifndef RCHK_COMMON_H
#define RCHK_COMMON_H

#include <set>
#include <unordered_set>
#include <unordered_map>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

typedef std::unordered_set<BasicBlock*> BasicBlocksSetTy;
typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::unordered_set<AllocaInst*> VarsSetTy;
typedef std::set<Function*> FunctionsOrderedSetTy;
typedef std::set<AllocaInst*> VarsOrderedSetTy;

struct VarBoolCacheTy_hash {
  size_t operator()(const AllocaInst* i) const {
    return (size_t) i;
  }
};
typedef std::unordered_map<AllocaInst*,bool,VarBoolCacheTy_hash> VarBoolCacheTy;

Module *parseArgsReadIR(int argc, char* argv[], FunctionsOrderedSetTy& functionsOfInterest, LLVMContext& context);

std::string demangle(std::string name);

bool sourceLocation(const Instruction *in, std::string& path, unsigned& line);
std::string sourceLocation(const Instruction *in);
std::string instructionAsString(Instruction *in);

bool isSEXP(AllocaInst* var);
bool isSEXP(Type* type);
bool isInstall(Function *f);

#endif
