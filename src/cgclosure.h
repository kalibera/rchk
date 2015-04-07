#ifndef RCHK_CGCLOSURE_H
#define RCHK_CGCLOSURE_H

#include "common.h"

#include <map>
#include <set>
#include <vector>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>

using namespace llvm;

struct FunctionInfo;

struct CallInfo {
  const Instruction* const instruction;
  const FunctionInfo* target;
  
  public:
  CallInfo(const Instruction* instruction, const FunctionInfo* target): instruction(instruction), target(target) {};
};

struct FunctionInfo {  
  const Function* const function;
  std::vector<CallInfo> callInfos;
  std::vector<bool> callsFunctionMap;
  std::vector<FunctionInfo*> calledFunctionsList;
  const unsigned index;
  
  public:
  FunctionInfo(const Function* const f, unsigned long index, unsigned long maxFunctions): function(f), callInfos(), callsFunctionMap(maxFunctions, false), index(index) {};
};

typedef std::map<Function*, FunctionInfo> FunctionsInfoMapTy;

typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::map<Function*, FunctionsSetTy*> CallEdgesMapTy;

void buildCGClosure(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths = true, FunctionsSetTy *onlyFunctions = NULL, CallEdgesMapTy *onlyEdges = NULL, 
  Function* externalFunction = NULL);

#endif
