\
#include "common.h"

#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "allocators.h"
#include "cgclosure.h"

using namespace llvm;

void getCalledFunctions(Function *f, FunctionsSetTy& calledFunctions) {

  for(Function::iterator bb = f->begin(), bbe = f->end(); bb != bbe; ++bb) {
    for(BasicBlock::iterator in = bb->begin(), ine = bb->end(); in != ine; ++in) {
      Value *v = &*in;
      CallSite cs(v);
      if (!cs) continue;
      Function *tgt = cs.getCalledFunction();
      calledFunctions.insert(tgt);
    }
  }
}

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);

  /* ignore Rf_error because it calls into Rf_errorcall */
  Function *errorf = m->getFunction("Rf_error");
  if (!errorf) {
    errs() << "Cannot find function to check.\n";
    exit(1);
  }

  Function *myf = m->getFunction("Rf_errorcall");
  if (!myf) {
    errs() << "Cannot find function to check.\n";
    exit(1);
  }
  
  FunctionsSetTy onlyFunctions;
  CallEdgesMapTy onlyEdges;
  
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *f = &*fi;

    if (f != errorf) onlyFunctions.insert(f);
    FunctionsSetTy calledFunctions;
    if (f != myf)
      getCalledFunctions(f, calledFunctions);
    
    onlyEdges.insert({f, new FunctionsSetTy(calledFunctions)});
  }

  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, false /* ignore error paths */, &onlyFunctions, &onlyEdges);

  unsigned myfindex = 0;
  
  auto fsearch = functionsMap.find(myf);
  if (fsearch == functionsMap.end()) {
    errs() << "Cannot find function info of function to check\n";
    exit(1);
  }
  myfindex = fsearch->second.index;  

  errs() << "Functions calling (recursively) function " << funName(myf) << "\n";
  for(FunctionsVectorTy::iterator FI = functionsOfInterestVector.begin(), FE = functionsOfInterestVector.end(); FI != FE; ++FI) {

    auto fisearch = functionsMap.find(*FI);
    if (fisearch == functionsMap.end()) continue;
    FunctionInfo& finfo = fisearch->second;

    if ((finfo.callsFunctionMap)[myfindex]) {
      errs() << funName(finfo.function) << "\n";
    }
  }

  delete m;
}
