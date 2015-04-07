
#include "cgclosure.h"
#include "errors.h"

#include <llvm/Analysis/CallGraph.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

const bool DEBUG = false;

// build closure over the callgraph of module m
// each function from module m gets its FunctionInfo in the functionsMap

void buildCGClosure(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths, FunctionsSetTy *onlyFunctions, CallEdgesMapTy *onlyEdges, Function* externalFunction) {

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
    const CallGraphNode* sourceCGN = MI->second;
    if (!fun) continue;
    
    if (onlyFunctions && onlyFunctions->find(fun) == onlyFunctions->end()) {
      continue;
    }
    maxFunctions++;
  }
     
  for (CallGraph::const_iterator MI = cg->begin(), ME = cg->end(); MI != ME; ++MI) {
    Function* fun = const_cast<Function*>(MI->first);
    const CallGraphNode* sourceCGN = MI->second;
    if (!fun) continue;
    
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

      Value *callVal = RI->first;
      const CallGraphNode* const targetCGN = RI->second;
      
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
        }
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
      
      if (targetFun->doesNotReturn()) {
        if (DEBUG) errs() << " ignoring edge to function " << funName(targetFun) << " as it does not return.\n";
        continue;
      }
      
      BasicBlock *bb = callInst->getParent();
      if (errorBlocks.find(bb) != errorBlocks.end()) {
        if (DEBUG) {
          errs() << " in function " << funName(fun) << " ignoring edge to function " << 
            funName(targetFun) << " as it is called from a basic block that always results in error.\n";
        }
        continue;
      }
      
      // create callinfo for this instruction
      
      finfo->callInfos.push_back(CallInfo(callInst, targetFunctionInfo));
      finfo->calledFunctionsList.push_back(targetFunctionInfo);
      edges++;
      
      if (DEBUG) errs() << " when recording call from " << funName(finfo->function) << " to " << funName(targetFunctionInfo->function) << "\n";
    }
    if (DEBUG) errs() << " mapped function " << funName(finfo->function) << "\n";
  }
  
  // fill-in bitmaps of which functions are reachable from which - now we know the number of functions to do that
  
  if (DEBUG) errs() << "Allocating bitmaps and registering functions.\n";

  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    FunctionInfo& finfo = FI->second;
    
    for(std::vector<FunctionInfo*>::iterator TFI = finfo.calledFunctionsList.begin(), TFE = finfo.calledFunctionsList.end(); TFI != TFE; ++TFI) {
      FunctionInfo *targetFinfo = *TFI;
          
      (finfo.callsFunctionMap)[targetFinfo->index] = true; 
    }
  }
  
  // compute transitive closure
  // no attempts were made to make this efficient
  //     
  // for each function node (function info)
  //   for each call instruction (call info)
  //     add targets reachable through 1 intermediate call to this call info
  //
  // repeat the above as long as at least one target has actually been added
  
  if (DEBUG) errs() << "Calculating transitive closure.\n";
  int iterations = 0;
  if (DEBUG) errs() << "The graph has " << functions << " nodes and " << edges << " edges.\n";
  
  for(unsigned long addedCalls = ULONG_MAX; addedCalls != 0; iterations++) {
    addedCalls = 0;
    unsigned long visitedCalls = 0;
    unsigned long processedFunctions = 0;    
    if (DEBUG) errs() << "Iteration " << iterations << "...";
    for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
      FunctionInfo& finfo = FI->second;
      processedFunctions++;
      if (DEBUG && !(processedFunctions % (functions/10))) errs() << "#";
      
      std::vector<FunctionInfo*> toadd;
      
      for(std::vector<FunctionInfo*>::iterator MFI = finfo.calledFunctionsList.begin(), MFE = finfo.calledFunctionsList.end(); MFI != MFE; ++MFI) {
        FunctionInfo *middleFinfo = *MFI;
          
        for(std::vector<FunctionInfo*>::iterator TFI = middleFinfo->calledFunctionsList.begin(), TFE = middleFinfo->calledFunctionsList.end(); TFI != TFE; ++TFI) {
          FunctionInfo *targetFinfo = *TFI;
              
          if (!(finfo.callsFunctionMap)[targetFinfo->index]) {
            (finfo.callsFunctionMap)[targetFinfo->index] = true;
            toadd.push_back(targetFinfo);
            addedCalls++;
          }
        }
      }
      
      finfo.calledFunctionsList.insert(finfo.calledFunctionsList.end(), toadd.begin(), toadd.end());
    } 
    if (DEBUG) errs() << " added " << addedCalls << " calls out of " << visitedCalls << " visited calls.\n";
  }
  delete cg;
}
