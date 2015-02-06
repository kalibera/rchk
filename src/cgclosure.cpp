
#include "cgclosure.h"
#include "errors.h"

#include <llvm/Analysis/CallGraph.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

const bool DEBUG = false;

// build closure over the callgraph of module m
// each function from module m gets its FunctionInfo in the functionsMap

void buildCGClosure(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths, FunctionsSetTy *onlyFunctions) {

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
     
  for (CallGraph::iterator MI = cg->begin(), ME = cg->end(); MI != ME; ++MI) {
    Function* fun = const_cast<Function*>(MI->first);
    CallGraphNode* sourceCGN = MI->second;
    if (!fun) continue;
    
    if (onlyFunctions && onlyFunctions->find(fun) == onlyFunctions->end()) {
      continue;
    }

    FunctionInfo *finfo;
    auto fsearch = functionsMap.find(fun);
    if (fsearch == functionsMap.end()) {
      finfo = new FunctionInfo(fun, functions++);
      functionsMap.insert({fun, finfo});
    } else {
      finfo = fsearch->second;
    }
    
    // check which basic blocks of the function are "error" blocks
    //  (they always end up, possibly recursively, in a noreturn - that is error - function)
    //  recursively means through other basic blocks of the same function, but we won't catch
    //  if a noreturn function is wrapped
    
    BasicBlocksSetTy errorBlocks;
    if (ignoreErrorPaths) {
      findErrorBasicBlocks(fun, errorFunctions, errorBlocks);
    }
    
    for(CallGraphNode::const_iterator RI = sourceCGN->begin(), RE = sourceCGN->end(); RI != RE; ++RI) {

      const CallGraphNode* const targetCGN = RI->second;
      if (!RI->first) continue; /* NULL call site */
      CallSite* callSite = new CallSite(RI->first);
      Instruction* callInst = dyn_cast<Instruction>(callSite->getInstruction());
      Function* targetFun = targetCGN->getFunction();
      if (!targetFun) continue;
      if (onlyFunctions && onlyFunctions->find(targetFun) == onlyFunctions->end()) {
        continue;
      }

      // find or create FunctionInfo for the target      
      FunctionInfo *targetFunctionInfo;
      auto search = functionsMap.find(targetFun);
      if (search == functionsMap.end()) {
        if (DEBUG) errs() << " creating new info";
        targetFunctionInfo = new FunctionInfo(targetFun, functions++);
        functionsMap.insert({targetFun, targetFunctionInfo});
      } else {
        if (DEBUG) errs() << " reusing old info";
        targetFunctionInfo = search->second;
      }
      
      if (targetFun->doesNotReturn()) {
        if (DEBUG) errs() << " ignoring edge to function " << targetFun->getName() << " as it does not return.\n";
        continue;
      }
      
      BasicBlock *bb = callInst->getParent();
      if (errorBlocks.find(bb) != errorBlocks.end()) {
        if (DEBUG) {
          errs() << " in function " << fun->getName() << " ignoring edge to function " << 
            targetFun->getName() << " as it is called from a basic block that always results in error.\n";
        }
        continue;
      }
      
      // create callinfo for this instruction
      CallInfo *cinfo = new CallInfo(callInst);
      cinfo->targets.insert(targetFunctionInfo);
      finfo->callInfos.push_back(cinfo);
      finfo->calledFunctionsList.push_back(targetFunctionInfo);
      edges++;
      
      if (DEBUG) errs() << " when recording call from " << finfo->function->getName() << " to " << targetFunctionInfo->function->getName() << "\n";
    }
    if (DEBUG) errs() << " mapped function " << finfo->function->getName() << "\n";
  }
  
  // fill-in bitmaps of which functions are reachable from which - now we know the number of functions to do that
  
  if (DEBUG) errs() << "Allocating bitmaps and registering functions.\n";

  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    FunctionInfo *finfo = FI->second;
    if (!finfo->callsFunctionMap) {
      finfo->callsFunctionMap = new std::vector<bool>(functions, false);
    }
    
    for(std::vector<FunctionInfo*>::iterator TFI = finfo->calledFunctionsList.begin(), TFE = finfo->calledFunctionsList.end(); TFI != TFE; ++TFI) {
      FunctionInfo *targetFinfo = *TFI;
          
      if (!targetFinfo->callsFunctionMap) {
        targetFinfo->callsFunctionMap = new std::vector<bool>(functions, false);
      }
        
      (*finfo->callsFunctionMap)[targetFinfo->index] = true; 
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
    for(std::map<Function*, FunctionInfo*>::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
      FunctionInfo *finfo = FI->second;
      processedFunctions++;
      if (DEBUG && !(processedFunctions % (functions/10))) errs() << "#";
      
      std::vector<FunctionInfo*> toadd;
      
      for(std::vector<FunctionInfo*>::iterator MFI = finfo->calledFunctionsList.begin(), MFE = finfo->calledFunctionsList.end(); MFI != MFE; ++MFI) {
        FunctionInfo *middleFinfo = *MFI;
          
        for(std::vector<FunctionInfo*>::iterator TFI = middleFinfo->calledFunctionsList.begin(), TFE = middleFinfo->calledFunctionsList.end(); TFI != TFE; ++TFI) {
          FunctionInfo *targetFinfo = *TFI;
              
          if (!(*finfo->callsFunctionMap)[targetFinfo->index]) {
            (*finfo->callsFunctionMap)[targetFinfo->index] = true;
            toadd.push_back(targetFinfo);
            addedCalls++;
          }
        }
      }
      
      finfo->calledFunctionsList.insert(finfo->calledFunctionsList.end(), toadd.begin(), toadd.end());
    } 
    if (DEBUG) errs() << " added " << addedCalls << " calls out of " << visitedCalls << " visited calls.\n";
  }
  delete cg;
}
