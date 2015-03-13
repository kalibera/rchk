#ifndef RCHK_COMMON_H
#define RCHK_COMMON_H

#include <set>
#include <unordered_set>
#include <unordered_map>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

typedef std::unordered_set<BasicBlock*> BasicBlocksSetTy;
typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::unordered_set<AllocaInst*> VarsSetTy;
typedef std::unordered_set<GlobalVariable*> GlobalVarsSetTy;
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
std::string funName(Function *f);
std::string varName(AllocaInst *var);

struct GlobalsTy {
  Function *protectFunction, *protectWithIndexFunction, *unprotectFunction, *unprotectPtrFunction;
  GlobalVariable *ppStackTopVariable;
  
  GlobalVariable *nilVariable;
  Function *isNullFunction, *isSymbolFunction, *isLogicalFunction, *isRealFunction,
    *isComplexFunction, *isExpressionFunction, *isEnvironmentFunction, *isStringFunction;
  
  public:
    GlobalsTy(Module *m);
  
  private:
    Function *getSpecialFunction(Module *m, std::string name);
    GlobalVariable *getSpecialVariable(Module *m, std::string name);
};

bool isSEXP(AllocaInst *var);
bool isSEXP(Type* type);
bool isSEXPPtr(Type *type);
bool isSEXP(GlobalVariable *var);
bool isInstall(Function *f);
bool isTypeTest(Function *f, GlobalsTy* g);

// from Boost
template <class T>
inline void hash_combine(std::size_t& seed, const T& v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

#endif
