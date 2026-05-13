
#include "cginfo.h"
#include "errors.h"

#include <llvm/Analysis/CallGraph.h>

#include <llvm/Support/raw_ostream.h>

#include <queue>

using namespace llvm;

const bool DEBUG = false;

static AdjacencyListTy reverseEdges(const AdjacencyListTy& adjacencyList) {
  // lets reverse edges
  // neighbours[i] are all functions that call function i
  AdjacencyListTy neighbours(adjacencyList.size());
  for (unsigned i = 0; i < adjacencyList.size(); i++) {
    for (unsigned j : adjacencyList[i]) {
      neighbours[j].push_back(i);
    }
  }
  return neighbours;
}

CanReachVectorTy computeCanReachToAnyIndex(const AdjacencyListTy& adjacencyList, const std::vector<unsigned>& targetIndices) {
  CanReachVectorTy canReach(adjacencyList.size(), false);
  AdjacencyListTy neighbours = reverseEdges(adjacencyList);

  std::queue<unsigned> q;
  for (unsigned targetIndex : targetIndices) {
    if (targetIndex < neighbours.size()) {
      q.push(targetIndex);
    }
    else { // should not happen
      errs() << "Error: target index " << targetIndex << " is out of bounds for adjacency list of size " << adjacencyList.size() << "\n";
    }
  }

  while (!q.empty()) {
    unsigned i = q.front();
    q.pop();

    for (unsigned j : neighbours[i]) {
      if (!canReach[j]) {
        canReach[j] = true;
        q.push(j);
      }
    }
  }
  return canReach;
}

CanReachVectorTy computeCanReachToAnyIndex(const FunctionsInfoMapTy& functionsMap, const std::vector<unsigned>& targetIndices) {
  // NOTE: buildCGInfo guarantees that any function in callInfo should have an entry in functionsMap
  AdjacencyListTy adjacencyList(functionsMap.size() + 1);

  for (const auto& [_, finfo] : functionsMap) {
    auto& callees = adjacencyList[finfo.index];
    callees.reserve(finfo.callInfos.size());
    for (const auto& cinfo : finfo.callInfos) {
      if (cinfo.target->index >= adjacencyList.size()) {
        errs() << "Error: function index " << cinfo.target->index << " is out of bounds for adjacency list of size " << adjacencyList.size() << "\n";
        continue; // should not happen if buildCGInfo is correct
      }
      callees.push_back(cinfo.target->index);
    }
  }

  return computeCanReachToAnyIndex(adjacencyList, targetIndices);
}

CanReachVectorTy computeCanReachToIndex(const FunctionsInfoMapTy& functionsMap, unsigned targetIndex) {
  return computeCanReachToAnyIndex(functionsMap, {targetIndex});
}

CanReachVectorTy computeCanReachToIndex(const AdjacencyListTy& adjacencyList, unsigned targetIndex) {
  return computeCanReachToAnyIndex(adjacencyList, {targetIndex});
}

// build information about the callgraph of module m
// each function from module m gets its FunctionInfo in the functionsMap

void buildCGInfo(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths, FunctionsSetTy *onlyFunctions, CallEdgesMapTy *onlyEdges, Function* externalFunction) {

  FunctionsSetTy errorFunctions;
  if (ignoreErrorPaths) {
    findErrorFunctions(m, errorFunctions);
  }

  // build llvm callgraph
  CallGraph *cg = new CallGraph(*m);
  
  // convert the callgraph to a different structure, suitable for transitive closure computation
  //
  // ... vector of FunctionInfo(s)
  //     each represents a function and lists which functions are reachable from which instructions

  unsigned long edges = 0;
  unsigned long functions = 0;
  
  // count (maximum) number of functions that will be stored in calledFunctionsMap (also a bound for function index)
  unsigned long maxFunctions = 0;
  
  for (CallGraph::const_iterator MI = cg->begin(), ME = cg->end(); MI != ME; ++MI) {
    Function* fun = const_cast<Function*>(MI->first);
    if (!fun) continue;
    
    if (onlyFunctions && onlyFunctions->find(fun) == onlyFunctions->end()) {
      continue;
    }
    maxFunctions++;
  }
     
  for (CallGraph::const_iterator MI = cg->begin(), ME = cg->end(); MI != ME; ++MI) {
    Function* fun = const_cast<Function*>(MI->first);
    if (!fun) continue;

    const CallGraphNode* sourceCGN = MI->second.get();
    if (onlyFunctions && onlyFunctions->find(fun) == onlyFunctions->end()) {
      continue;
    }

    FunctionInfo *finfo;
    auto fsearch = functionsMap.find(fun);
    if (fsearch == functionsMap.end()) {
      auto insert = functionsMap.insert({fun, FunctionInfo(fun, functions++, maxFunctions)});
      finfo = &insert.first->second;
    } else {
      finfo = &fsearch->second;
    }
    
    // check which basic blocks of the function are "error" blocks
    //  (they always end up, possibly recursively, in a noreturn - that is error - function)
    //  recursively means through other basic blocks of the same function, but we won't catch
    //  if a noreturn function is wrapped
    
    BasicBlocksSetTy errorBlocks;
    if (ignoreErrorPaths) {
      findErrorBasicBlocks(fun, &errorFunctions, errorBlocks);
    }

    for(CallGraphNode::const_iterator RI = sourceCGN->begin(), RE = sourceCGN->end(); RI != RE; ++RI) {
      if (!RI->first)
        continue;
      Value *callVal = *RI->first;
      CallGraphNode* targetCGN = RI->second;
      
      if (!callVal || !Instruction::classof(callVal)) {
        // value can be null
        // also value can be e.g. a declaration (but why?)
        continue;
      }

      Function* targetFun = targetCGN->getFunction();
      if (!targetFun) {
        if (DEBUG) errs() << "   call to external function\n";
        targetFun = externalFunction;
      }
      if (!targetFun) {
        continue;
      }

      Instruction* callInst = cast<Instruction>(callVal);

      if (onlyFunctions && onlyFunctions->find(targetFun) == onlyFunctions->end()) {
        continue;
      }
      if (onlyEdges) {
        auto esearch = onlyEdges->find(fun);
        if (esearch != onlyEdges->end()) {
          FunctionsSetTy* onlyTargets = esearch->second;
          if (onlyTargets->find(targetFun) == onlyTargets->end()) {
            continue;
          }
        } else continue;
      }
      // find or create FunctionInfo for the target      
      FunctionInfo *targetFunctionInfo;
      auto search = functionsMap.find(targetFun);
      if (search == functionsMap.end()) {
        if (DEBUG) errs() << " creating new info";
        auto insert = functionsMap.insert({targetFun, FunctionInfo(targetFun, functions++, maxFunctions)});
        targetFunctionInfo = &insert.first->second;
        
      } else {
        if (DEBUG) errs() << " reusing old info";
        targetFunctionInfo = &search->second;
      }
      
      if (ignoreErrorPaths && targetFun->doesNotReturn()) {
        if (DEBUG) errs() << " ignoring edge to function " << funName(targetFun) << " as it does not return.\n";
        continue;
      }
      
      if (ignoreErrorPaths) {
        BasicBlock *bb = callInst->getParent();
        if (errorBlocks.find(bb) != errorBlocks.end()) {
          if (DEBUG) {
            errs() << " in function " << funName(fun) << " ignoring edge to function " << 
              funName(targetFun) << " as it is called from a basic block that always results in error.\n";
          }
          continue;
        }
      }
      
      // create callinfo for this instruction
      
      finfo->callInfos.push_back(CallInfo(callInst, targetFunctionInfo));
      edges++;
      
      if (DEBUG) errs() << " when recording call from " << funName(finfo->function) << " to " << funName(targetFunctionInfo->function) << "\n";
    }
    if (DEBUG) errs() << " mapped function " << funName(finfo->function) << "\n";
  }
  delete cg;
}
