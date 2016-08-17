
#include "symbols.h"

using namespace llvm;

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

bool isInstallConstantCall(Value *inst, std::string& symbolName) {
  CallSite cs(inst);
  if (!cs) {
    return false;
  }
  Function *tgt = cs.getCalledFunction();
  if (!tgt || tgt->getName() != "Rf_install") {
    return false;
  }
  Value *arg = cs.getArgument(0);
  
  // getting a constant string from the IR is not very straightforward
  // http://lists.cs.uiuc.edu/pipermail/llvmdev/2012-January/047147.html
  
  if (!ConstantExpr::classof(arg)) {
    return false;
  }	
  ConstantExpr *ce = cast<ConstantExpr>(arg);
  if (!ce->isGEPWithNoNotionalOverIndexing()) {
    return false;
  }
  
  Value *ceop = ce->getOperand(0);
  if (!GlobalVariable::classof(ceop)) {
    return false;
  }
  Constant *gvInit = cast<GlobalVariable>(ceop)->getInitializer();
  
  if (!ConstantDataArray::classof(gvInit)) {
    return false;
  }
  ConstantDataArray *cda = cast<ConstantDataArray>(gvInit);
  if (!cda->isCString()) {
    return false;
  }
  symbolName = cda->getAsCString();
  return true;   
}

void findSymbols(Module *m, SymbolsMapTy* symbolsMap) {

  for(Module::global_iterator gi = m->global_begin(), ge = m->global_end(); gi != ge ; ++gi) {
    GlobalVariable *gv = &*gi;
    if (!isSEXP(gv)) {
      continue;
    }
    bool foundInstall = false;
    std::string symbolName;
    
    for(Value::user_iterator ui = gv->user_begin(), ue = gv->user_end(); ui != ue; ++ui) {
      User *u = *ui;
      if (!StoreInst::classof(u)) {
        continue;
      }
      Value *valueOp = cast<StoreInst>(u)->getValueOperand();
      std::string name;
      if (isInstallConstantCall(valueOp, name)) {
        if (!foundInstall) {
          symbolName = name;
          foundInstall = true;
        } else {
          if (symbolName != name) {
            errs() << "ERROR: Multiple names for symbol " << gv->getName() << ": " << symbolName << " and " << name << "\n";
            goto cannot_be_symbol;
          }
        }
      } else {
        if (foundInstall) {
          errs() << "ERROR: Invalid write to symbol " << gv->getName();
          if (Instruction::classof(valueOp)) {
            errs() << " at " << sourceLocation(cast<Instruction>(valueOp));
          }
          errs() << "\n";
        }
        goto cannot_be_symbol;
      }
    }
    if (foundInstall) {
      symbolsMap->insert({gv, symbolName});
    }
    cannot_be_symbol:
      ;    
  } 
}
