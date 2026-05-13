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
  const unsigned index;
  
  public:
  FunctionInfo(const Function* const f, unsigned long index, unsigned long maxFunctions): function(f), callInfos(), index(index) {};
};

typedef std::map<Function*, FunctionInfo> FunctionsInfoMapTy;

typedef std::unordered_set<Function*> FunctionsSetTy;
typedef std::map<Function*, FunctionsSetTy*> CallEdgesMapTy;

typedef std::vector<bool> CanReachVectorTy;
typedef std::vector<unsigned> AdjacencyListRow;
typedef std::vector<AdjacencyListRow> AdjacencyListTy;

// calculates which functions call target function
// adjacencyList[i] are all functions that get called by function i
// to match previous closure semantics, target is non-reflexive (i.e. target doesn't reach itself)
CanReachVectorTy computeCanReachToAnyIndex(const AdjacencyListTy& adjacencyList, const std::vector<unsigned>& targetIndices);

// adapter to call computeCanReach with AdjacencyListTy
CanReachVectorTy computeCanReachToAnyIndex(const FunctionsInfoMapTy& functionsMap, const std::vector<unsigned>& targetIndices);
CanReachVectorTy computeCanReachToIndex(const AdjacencyListTy& adjacencyList, unsigned targetIndex);
CanReachVectorTy computeCanReachToIndex(const FunctionsInfoMapTy& functionsMap, unsigned targetIndex);

void buildCGInfo(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths = true, FunctionsSetTy *onlyFunctions = NULL, CallEdgesMapTy *onlyEdges = NULL, 
  Function* externalFunction = NULL);

#endif
