
#ifndef RCHK_CALLOCATORS_H
#define RCHK_CALLOCATORS_H

#include "common.h"
#include "allocators.h"
#include "symbols.h"

#include <unordered_set>
#include <vector>

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

struct ArgInfoTy {

  virtual bool isSymbol() const { return false; };
};

struct SymbolArgInfoTy : public ArgInfoTy {

  std::string symbolName;

  SymbolArgInfoTy(std::string& symbolName) : symbolName(symbolName) {};
  
  virtual bool isSymbol() const { return true; }
};

typedef std::vector<ArgInfoTy*> ArgInfosTy;

struct CalledFunctionTy {

  Function *fun;
  ArgInfosTy *argInfo; // NULL element means nothing known about that argument

  CalledFunctionTy(Function *fun, ArgInfosTy *argInfo): fun(fun), argInfo(argInfo) {};
  std::string getName() const;
};

struct CalledFunctionPtrTy_hash {
  size_t operator()(const CalledFunctionTy* t) const;
};

struct CalledFunctionPtrTy_equal {
  bool operator() (const CalledFunctionTy* lhs, const CalledFunctionTy* rhs) const;
};    

typedef std::unordered_set<CalledFunctionTy*, CalledFunctionPtrTy_hash, CalledFunctionPtrTy_equal> CalledFunctionsSetTy;

struct ArgInfosPtrTy_hash {
  size_t operator()(const ArgInfosTy* t) const;
};

struct ArgInfosPtrTy_equal {
  bool operator() (const ArgInfosTy* lhs, const ArgInfosTy* rhs) const;
};    

typedef std::unordered_set<ArgInfosTy*, ArgInfosPtrTy_hash, ArgInfosPtrTy_equal> ArgInfosSetTy;

class CalledModuleTy {
  CalledFunctionsSetTy calledFunctionsTable; // intern table
  SymbolsMapTy* symbolsMap;
  ArgInfosSetTy argInfosTable; // intern table
  
  public:
    CalledModuleTy(Module *m, SymbolsMapTy* symbolsMap);
    CalledFunctionTy* getCalledFunction(Value *inst);
    CalledFunctionsSetTy* getCalledFunctions() { return &calledFunctionsTable; }
    virtual ~CalledModuleTy();
};

#endif
