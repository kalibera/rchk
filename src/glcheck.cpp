/*
  This tool is to detect global variables/structures that may (accidentally)
  hold SEXPs, but possibly are not known as roots to the GC.
*/ 

#include "common.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include <unordered_set>

#include "symbols.h"

using namespace llvm;

// return true if there is SEXP somewhere within type t
typedef std::unordered_set<Type*> TypeSetTy;

bool containsSEXP(Type *t, TypeSetTy& visited) {

  if (visited.find(t) != visited.end()) {
    // already under evaluation (recursive type)
    return false;
  }
  visited.insert(t);
  
  if (SequentialType *st = dyn_cast<PointerType>(t)) {
    return containsSEXP(st->getElementType(), visited);
  }
  
  if (StructType *st = dyn_cast<StructType>(t)) {

    if (st->hasName() && st->getName() == "struct.SEXPREC") {
      return true;
    }
    unsigned nelems = st->getNumElements();
    for(unsigned i = 0; i < nelems; i++) {
      if (containsSEXP(st->getElementType(i), visited)) {
        return true;
      }
    }
  }
  return false;
}

bool isStructureWithSEXPFields(Type *t) {

  TypeSetTy visited;
  return containsSEXP(t, visited);
}

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
    // NOTE: functionsOfInterest ignored but (re-)analyzing the R core is necessary
  
  SymbolsMapTy symbolsMap;
  findSymbols(m, &symbolsMap); // symbols are globals which hold SEXPs, but are safe
  
  for(Module::global_iterator gi = m->global_begin(), ge = m->global_end(); gi != ge ; ++gi) {
    GlobalVariable *gv = gi;
    
    if (isSEXP(gv)) {
      if (symbolsMap.find(gv) != symbolsMap.end()) {
        continue;
        }
    
      errs() << "non-symbol SEXP global variable " << gv->getName() << "  " << *gv << "\n";
      // many of these are OK, but it does not seem to be easily checkable
      continue;
    }
    
    if (isStructureWithSEXPFields(gv->getType())) {
      errs() << "structure with SEXP fields " << gv->getName() << " " << *gv << "\n";
    }
    
  }
  
  delete m;
}
