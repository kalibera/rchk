#ifndef RCHK_SYMBOLS_H
#define RCHK_SYMBOLS_H

#include "common.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

using namespace llvm;

typedef std::unordered_map<GlobalVariable*, std::string> SymbolsMapTy;

bool isInstallConstantCall(Value *inst, std::string& symbolName);
void findSymbols(Module *m, GlobalVarsSetTy& symbols, SymbolsMapTy* symbolsMap = NULL);

#endif
