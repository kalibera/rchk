
#ifndef RCHK_CALLOCATORS_H
#define RCHK_CALLOCATORS_H

#include "common.h"
#include "allocators.h"
#include "guards.h"
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

class CalledModuleTy;

struct CalledFunctionTy {

  Function *fun;
  ArgInfosTy *argInfo; // NULL element means nothing known about that argument
  CalledModuleTy *module;
  unsigned idx; // filled in during interning

  CalledFunctionTy(Function *fun, ArgInfosTy *argInfo, CalledModuleTy *module): fun(fun), argInfo(argInfo), module(module), idx(UINT_MAX) {};
  std::string getName() const;
};

struct CalledFunctionPtrTy_hash {
  size_t operator()(const CalledFunctionTy* t) const;
};

struct CalledFunctionPtrTy_equal {
  bool operator() (const CalledFunctionTy* lhs, const CalledFunctionTy* rhs) const;
};    

typedef std::unordered_set<CalledFunctionTy*, CalledFunctionPtrTy_hash, CalledFunctionPtrTy_equal> CalledFunctionsTableTy; // can be used for interning
typedef std::set<CalledFunctionTy*> CalledFunctionsOrderedSetTy; // for interned functions
typedef std::unordered_set<CalledFunctionTy*> CalledFunctionsSetTy; // for interned functions

struct ArgInfosPtrTy_hash {
  size_t operator()(const ArgInfosTy* t) const;
};

struct ArgInfosPtrTy_equal {
  bool operator() (const ArgInfosTy* lhs, const ArgInfosTy* rhs) const;
};    

typedef std::unordered_set<ArgInfosTy*, ArgInfosPtrTy_hash, ArgInfosPtrTy_equal> ArgInfosSetTy;

struct ArgInfoPtrTy_hash {
  size_t operator()(const ArgInfoTy* t) const;
};

struct ArgInfoPtrTy_equal {
  bool operator() (const ArgInfoTy* lhs, const ArgInfoTy* rhs) const;
};    


typedef std::unordered_set<ArgInfoTy*, ArgInfoPtrTy_hash, ArgInfoPtrTy_equal> ArgInfoSetTy; // can be used for interning
typedef std::vector<CalledFunctionTy*> CalledFunctionsVectorTy;

  // yikes, need forward type def
struct SEXPGuardTy;
typedef std::map<AllocaInst*,SEXPGuardTy> SEXPGuardsTy;

class CalledModuleTy {
  CalledFunctionsTableTy calledFunctionsTable; // intern table
  CalledFunctionsVectorTy calledFunctionsVector; // for mapping idx -> function
  ArgInfosSetTy argInfosTable; // intern table
  ArgInfoSetTy argInfoTable; // intern table
  
  SymbolsMapTy* symbolsMap;
  Module *m;
  FunctionsSetTy* errorFunctions;
  GlobalsTy* globals;
  FunctionsSetTy* possibleAllocators;
  FunctionsSetTy* allocatingFunctions;
  
  CalledFunctionTy *gcFunction;

  private:
    ArgInfosTy* intern(ArgInfosTy& argInfos);
    SymbolArgInfoTy* intern(SymbolArgInfoTy& argInfo);
    CalledFunctionTy* intern(CalledFunctionTy& calledFunction);

  public:
    CalledModuleTy(Module *m, SymbolsMapTy* symbolsMap, FunctionsSetTy* errorFunctions, GlobalsTy* globals,
      FunctionsSetTy* possibleAllocators, FunctionsSetTy* allocatingFunctions);
      
    CalledFunctionTy* getCalledFunction(Value *inst);
    CalledFunctionTy* getCalledFunction(Value *inst, SEXPGuardsTy *sexpGuards); // takes context from guards
    CalledFunctionTy* getCalledFunction(Function *f); // gets a version with no context
    CalledFunctionTy* getCalledFunction(unsigned idx) { return calledFunctionsVector.at(idx); };
    CalledFunctionsVectorTy* getCalledFunctions() { return &calledFunctionsVector; }
    virtual ~CalledModuleTy();
    
    bool isAllocating(Function *f) { return allocatingFunctions->find(f) != allocatingFunctions->end(); }
    bool isPossibleAllocator(Function *f) { return possibleAllocators->find(f) != possibleAllocators->end(); }
    
    FunctionsSetTy* getErrorFunctions() { return errorFunctions; }
    FunctionsSetTy* getPossibleAllocators() { return possibleAllocators; }
    FunctionsSetTy* getAllocatingFunctions() { return allocatingFunctions; }
    GlobalsTy* getGlobals() { return globals; }
    Module* getModule() { return m; }
    CalledFunctionTy* getCalledGCFunction() { return gcFunction; }
};

void getCalledAllocators(CalledModuleTy *cm, CalledFunctionsSetTy& possibleCAllocators, CalledFunctionsSetTy& allocatingCFunctions); 
// FIXME: eventually move this into CalledModuleTy

#endif
