#ifndef RCHK_COMMON_H
#define RCHK_COMMON_H

void myassert_fail (const char *assertion, const char *file, unsigned int line, const char *function);

#define ENABLE_ASSERTIONS
#ifdef ENABLE_ASSERTIONS
  // based on GCC assert.h, but do not use NDEBUG and standard assertions, because it confuses LLVM
  #define myassert(x) ((x) ? static_cast<void>(0) : myassert_fail(#x, __FILE__, __LINE__, __func__))
#else
  #define myassert(x) (static_cast<void>(0))
#endif


#include <set>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

#if LLVM_VERSION_MAJOR>=8
  #define TerminatorInst Instruction
#endif

using namespace llvm;

typedef std::unordered_set<BasicBlock*> BasicBlocksSetTy;
typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::unordered_set<AllocaInst*> VarsSetTy;
typedef std::unordered_set<GlobalVariable*> GlobalVarsSetTy;
typedef std::set<Function*> FunctionsOrderedSetTy;
typedef std::vector<Function*> FunctionsVectorTy;
typedef std::set<AllocaInst*> VarsOrderedSetTy;

struct VarBoolCacheTy_hash {
  size_t operator()(const AllocaInst* i) const {
    return (size_t) i;
  }
};
typedef std::unordered_map<AllocaInst*,bool,VarBoolCacheTy_hash> VarBoolCacheTy;

Module *parseArgsReadIR(int argc, char* argv[], FunctionsOrderedSetTy& functionsOfInterestSet, FunctionsVectorTy& functionsOfInterestVector, LLVMContext& context);

std::string demangle(std::string name);

bool sourceLocation(const Instruction *in, std::string& path, unsigned& line);
std::string sourceLocation(const Instruction *in);
std::string funLocation(const Function *f);
std::string instructionAsString(const Instruction *in);
std::string funName(const Function *f);
std::string varName(const AllocaInst *var);

enum SEXPType {
  RT_NIL = 0,
  RT_SYMBOL = 1,
  RT_LIST = 2,
  RT_CLOSURE = 3,
  RT_ENVIRONMENT = 4,
  RT_PROMISE = 5,
  RT_LANGUAGE = 6,
  RT_SPECIAL = 7,
  RT_BUILTIN = 8,
  RT_CHAR = 9,
  RT_LOGICAL = 10,
  RT_INT = 13,
  RT_REAL = 14,
  RT_COMPLEX = 15,
  RT_STRING = 16,
  RT_DOT = 17,
  RT_ANY = 18,
  RT_VECTOR = 19,
  RT_EXPRESSION = 20,
  RT_BYTECODE = 21,
  RT_EXTPTR = 22,
  RT_WEAKREF = 23,
  RT_RAW = 24,
  RT_S4 = 25,
  RT_INTCHAR = 73,
  
  RT_UNKNOWN = -1  // not really an R type
};

typedef std::map<Function*, SEXPType> TypeNamesMapTy;

struct GlobalsTy {
  Function *protectFunction, *protectWithIndexFunction, *unprotectFunction, *unprotectPtrFunction;
  GlobalVariable *ppStackTopVariable;
  
  GlobalVariable *nilVariable;
  Function *isNullFunction, *isSymbolFunction, *isLogicalFunction, *isRealFunction,
    *isComplexFunction, *isExpressionFunction, *isEnvironmentFunction, *isStringFunction;
  
  TypeNamesMapTy typesMap;
  
  public:
    GlobalsTy(Module *m);
    SEXPType getTypeForTypeTest(Function *f) const;
  
  private:
    Function *getSpecialFunction(Module *m, std::string name);
    GlobalVariable *getSpecialVariable(Module *m, std::string name);
};

bool isPointerToStruct(Type* type, std::string name);
//bool isPointerToUnion(Type* type, std::string name);
bool isSEXP(AllocaInst *var);
bool isSEXP(Type* type);
bool isSEXPPtr(Type *type);
bool isSEXP(GlobalVariable *var);
bool isInstall(Function *f);
bool isProtectingFunction(Function *f);

// functions like setAttrib(x, name, value) that protect their non-first argument if the first argument is protected
// (by joining to the first argument)
bool isSetterFunction(Function *f);

bool isTypeTest(Function *f, const GlobalsTy* g);
  
// from Boost
template <class T>
inline void hash_combine(std::size_t& seed, const T& v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

#endif
