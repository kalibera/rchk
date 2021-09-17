
class CalledModuleTy;

#ifndef RCHK_CALLOCATORS_H
#define RCHK_CALLOCATORS_H

#include "common.h"
#include "allocators.h"
#include "guards.h"
#include "symbols.h"
#include "table.h"
#include "vectors.h"

#include <unordered_set>
#include <vector>

//#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

struct ArgInfoTy {

  virtual bool isSymbol() const { return false; }; /* this means a specific symbol, defined by its string name */
  virtual bool isVector() const { return false; }; /* this means anything LENGTH could be called on */
  virtual ~ArgInfoTy() = default; // to make the compiler happy :(
};


struct SymbolArgInfoTy : public ArgInfoTy {

  const std::string symbolName;
  SymbolArgInfoTy(const std::string& symbolName) : symbolName(symbolName) {};
  
  virtual bool isSymbol() const { return true; }
  
  struct SymbolArgInfoTy_hash {
    size_t operator()(const SymbolArgInfoTy& t) const {
      return std::hash<std::string>()(t.symbolName);
    }
  };

  struct SymbolArgInfoTy_equal {
    bool operator() (const SymbolArgInfoTy& lhs, const SymbolArgInfoTy& rhs) const {
      return lhs.symbolName == rhs.symbolName;
    }
  };

  typedef InterningTable<SymbolArgInfoTy, SymbolArgInfoTy_hash, SymbolArgInfoTy_equal> SymbolArgInfoTableTy;
  static SymbolArgInfoTableTy table;
  
  static const SymbolArgInfoTy* create(const std::string& symbolName) {
    return table.intern(SymbolArgInfoTy(symbolName)); // FIXME: leaks memory  
  }
};

struct VectorArgInfoTy : public ArgInfoTy { // a signleton class

  virtual bool isVector() const { return true; }
  
  static const VectorArgInfoTy* get() {
    static VectorArgInfoTy instance;
    return &instance;
  }
  
  private:
    VectorArgInfoTy() {};
    VectorArgInfoTy(VectorArgInfoTy const&);
    void operator=(VectorArgInfoTy const&);
};

typedef std::vector<const ArgInfoTy*> ArgInfosVectorTy; // for interned elements



struct CalledFunctionTy {

  Function* const fun;
  const ArgInfosVectorTy *argInfo; // NULL element means nothing known about that argument, interned
  CalledModuleTy* const module;
  
  unsigned idx; // filled in during interning

  CalledFunctionTy(Function *fun, const ArgInfosVectorTy *argInfo, CalledModuleTy *module): fun(fun), argInfo(argInfo), module(module), idx(UINT_MAX) {};
  std::string getName() const;
  std::string getNameSuffix() const;
  bool hasContext() const;
};

struct CalledFunctionTy_hash {
  size_t operator()(const CalledFunctionTy& t) const;
};

struct CalledFunctionTy_equal {
  bool operator() (const CalledFunctionTy& lhs, const CalledFunctionTy& rhs) const;
};    

typedef IndexedInterningTable<CalledFunctionTy, CalledFunctionTy_hash, CalledFunctionTy_equal> CalledFunctionsTableTy;
typedef std::vector<const CalledFunctionTy*> CalledFunctionsIndexTy;

typedef std::set<const CalledFunctionTy*> CalledFunctionsOrderedSetTy; // for interned functions
typedef std::unordered_set<const CalledFunctionTy*> CalledFunctionsSetTy; // for interned functions

struct ArgInfosVectorTy_hash {
  size_t operator()(const ArgInfosVectorTy& t) const;
};

struct ArgInfosVectorTy_equal {
  bool operator() (const ArgInfosVectorTy& lhs, const ArgInfosVectorTy& rhs) const;
};    

typedef InterningTable<ArgInfosVectorTy, ArgInfosVectorTy_hash> ArgInfoVectorsTableTy;


  // yikes, need forward type def
struct SEXPGuardTy;
class SEXPGuardsChecker;
typedef std::map<AllocaInst*,SEXPGuardTy> SEXPGuardsTy;

typedef std::map<Value*, CalledFunctionsSetTy> CallSiteTargetsTy;

class CalledModuleTy {
  CalledFunctionsTableTy calledFunctionsTable; // intern table
  ArgInfoVectorsTableTy argInfoVectorsTable; // intern table
  
  Module *m;
  SymbolsMapTy* symbolsMap;
  FunctionsSetTy* errorFunctions;
  GlobalsTy* globals;
  FunctionsSetTy* possibleAllocators;
  FunctionsSetTy* allocatingFunctions;
  FunctionsSetTy* contextSensitivePossibleAllocators;
  FunctionsSetTy* contextSensitiveAllocatingFunctions;
  CalledFunctionsSetTy* possibleCAllocators;
  CalledFunctionsSetTy* allocatingCFunctions;
  CallSiteTargetsTy callSiteTargets; // maps  call instruction -> set of target functions
  VrfStateTy* vrfState; // state for vector returning functions detection
  
  const CalledFunctionTy* const gcFunction;

  private:
    const ArgInfosVectorTy* intern(const ArgInfosVectorTy& argInfos) { return argInfoVectorsTable.intern(argInfos); }
    const CalledFunctionTy* intern(const CalledFunctionTy& calledFunction) { return calledFunctionsTable.intern(calledFunction); }
    void computeCalledAllocators();

  public:
    CalledModuleTy(Module *m, SymbolsMapTy* symbolsMap, FunctionsSetTy* errorFunctions, GlobalsTy* globals,
      FunctionsSetTy* possibleAllocators, FunctionsSetTy* allocatingFunctions);
      
    static CalledModuleTy* create(Module *m);
    static void release(CalledModuleTy *cm);
      
    const CalledFunctionTy* getCalledFunction(Value *inst, bool registerCallSite = false);
    const CalledFunctionTy* getCalledFunction(Value *inst, SEXPGuardsChecker *sexpGuardsChecker, SEXPGuardsTy *sexpGuards, bool registerCallSite); // takes context from guards
    const CalledFunctionTy* getCalledFunction(Function *f); // gets a version with no context
    const CalledFunctionTy* getCalledFunction(unsigned idx) { return calledFunctionsTable.at(idx); };
    const CalledFunctionsIndexTy* getCalledFunctions() { return calledFunctionsTable.getIndex(); }
    size_t getNumberOfCalledFunctions() { return calledFunctionsTable.getIndex()->size(); }
    const CalledFunctionsSetTy* getPossibleCAllocators() { computeCalledAllocators(); return possibleCAllocators; }
    const CalledFunctionsSetTy* getAllocatingCFunctions() { computeCalledAllocators(); return allocatingCFunctions; }
    const CallSiteTargetsTy* getCallSiteTargets() { computeCalledAllocators(); return &callSiteTargets; }
    
    virtual ~CalledModuleTy();
    
    bool isAllocating(Function *f) { return allocatingFunctions->find(f) != allocatingFunctions->end(); }
    bool isPossibleAllocator(Function *f) { return possibleAllocators->find(f) != possibleAllocators->end(); }
    bool isCAllocating(const CalledFunctionTy *cf) { computeCalledAllocators(); return allocatingCFunctions->find(cf) != allocatingCFunctions->end(); }
    bool isPossibleCAllocator(const CalledFunctionTy *cf) { computeCalledAllocators(); return possibleCAllocators->find(cf) != possibleCAllocators->end(); }
    
    FunctionsSetTy* getErrorFunctions() { return errorFunctions; }
    FunctionsSetTy* getPossibleAllocators() { return possibleAllocators; }
    FunctionsSetTy* getAllocatingFunctions() { return allocatingFunctions; }
    FunctionsSetTy* getContextSensitiveAllocatingFunctions() { computeCalledAllocators(); return contextSensitiveAllocatingFunctions; }
    FunctionsSetTy* getContextSensitivePossibleAllocators() { computeCalledAllocators(); return contextSensitivePossibleAllocators; }
    GlobalsTy* getGlobals() { return globals; }
    Module* getModule() { return m; }
    const CalledFunctionTy* getCalledGCFunction() { return gcFunction; }
    SymbolsMapTy* getSymbolsMap() { return symbolsMap; }
    void computeVectorReturningFunctions() { if (vrfState == NULL) findVectorReturningFunctions(this); }
    VrfStateTy* getVrfState() { computeVectorReturningFunctions(); return vrfState; }
    void setVrfState(VrfStateTy* vrfState) { this->vrfState = vrfState; }
};

std::string funName(const CalledFunctionTy *cf);

#endif
