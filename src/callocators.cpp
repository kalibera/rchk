
#include "callocators.h"
#include "symbols.h"

#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;

CalledFunctionTy::CalledFunctionTy(CallSite& cs, SymbolsMapTy& symbolsMap) : argInfo(cs.arg_size(), NULL) {

  assert(cs);
  unsigned nargs = cs.arg_size();

  for(unsigned i = 0; i < nargs; i++) {
    Value *arg = cs.getArgument(i);
    if (LoadInst::classof(arg)) {
      Value *src = cast<LoadInst>(arg)->getPointerOperand();
      if (GlobalVariable::classof(src)) {
        auto ssearch = symbolsMap.find(cast<GlobalVariable>(src));
        if (ssearch != symbolsMap.end()) {
          SymbolArgInfoTy *sa = new SymbolArgInfoTy(ssearch->second);
          argInfo.insert(i, sa);
          continue;
        }
      }
    }
    // not a symbol
    argInfo.insert(i, NULL);
  }
}

CalledFunctionTy::~CalledFunctionTy() {

  for(ArgInfosTy::iterator ai = argInfo.begin(), ae = argInfo.end(); ai != ae; ++ai) {
    ArgInfoTy *a = *ai;
    if (a) {
      delete a;
    }
  }
}

