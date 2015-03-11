
#ifndef RCHK_CALLOCATORS_H
#define RCHK_CALLOCATORS_H

#include "common.h"
#include "allocators.h"

#include <vector>

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>

using namespace llvm;

struct ArgInfoTy {

  virtual bool isSymbol();
};

struct SymbolArgInfoTy : public ArgInfoTy {

  std::string symbolName;

  SymbolArgInfoTy(std::string& symbolName) : symbolName(symbolName) {};
  
  virtual bool isSymbol() { return true; }
};

typedef std::vector<ArgInfoTy*> ArgInfosTy;

struct CalledFunctionTy {

  Function *fun;
  ArgInfosTy argInfo; // NULL element means nothing known about that argument

  CalledFunctionTy(CallSite& cs);
  virtual ~CalledFunctionTy();
};

#endif
