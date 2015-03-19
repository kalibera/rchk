#ifndef RCHK_CGCLOSURE_H
#define RCHK_CGCLOSURE_H

#include <map>
#include <set>
#include <vector>

#include "common.h"

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>

using namespace llvm;

struct FunctionInfo;

struct CallInfo {
  const Instruction* const instruction;
  std::set<FunctionInfo*> targets;
  
  public:
  CallInfo(const Instruction* const i): instruction(i) {};
};

struct FunctionInfo {  
  const Function* const function;
  std::vector<CallInfo*> callInfos;
  std::vector<bool>* callsFunctionMap;
  std::vector<FunctionInfo*> calledFunctionsList;
  unsigned long index;
  
  public:
  FunctionInfo(const Function* const f, unsigned long id): function(f), index(id), callsFunctionMap(NULL) {};
};

typedef std::map<Function*, FunctionInfo*> FunctionsInfoMapTy;

typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::map<Function*, FunctionsSetTy*> CallEdgesMapTy;

void buildCGClosure(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths = true, FunctionsSetTy *onlyFunctions = NULL, CallEdgesMapTy *onlyEdges = NULL);

void releaseMap(FunctionsInfoMapTy& map);

#endif
