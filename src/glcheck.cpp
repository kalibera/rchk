/*
  This tool is to detect global variables/structures that may (accidentally)
  hold SEXPs, but possibly are not known as roots to the GC.

  This tool doesn't use the cache.
*/ 

#include "common.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include <set>
#include <unordered_map>
#include <unordered_set>

#include "symbols.h"

using namespace llvm;

std::string compositeTypeCacheKey(DICompositeType* t) {
  if (!t) return "";
  std::string name = t->getName().str();
  if (name.empty()) return "";
  return name + ";" + std::to_string(t->getTag());
}

// helper struct to compare DICompositeType by file location
struct DICompositeTypeLocationLess {
  bool operator()(const DICompositeType* lhs, const DICompositeType* rhs) const {
    auto *lhsFile = lhs->getFile();
    auto *rhsFile = rhs->getFile();

    if (lhsFile == rhsFile) return lhs->getLine() < rhs->getLine();

    return std::less<const DIFile*>()(lhsFile, rhsFile);
  }
};

typedef std::set<DICompositeType*, DICompositeTypeLocationLess> DICompositeTypeSetTy;
typedef std::unordered_map<std::string, DICompositeTypeSetTy> DICompositeTypeCacheTy;

const DICompositeTypeSetTy* resolveForwardDeclaration(DICompositeType* t, Module* m) {
  if (!t) return nullptr;

  // Cache names and actual composite types on first use for each module.
  static Module* cachedModule = nullptr;
  static DICompositeTypeCacheTy cache;
  if (cachedModule != m) {
    cachedModule = m;
    cache.clear();

    DebugInfoFinder finder;
    finder.processModule(*m);

    for (auto *type : finder.types()) {
      if (auto *composite = dyn_cast<DICompositeType>(type)) {
        std::string name = compositeTypeCacheKey(composite);
        if (name.empty()) continue; // discard anonymous composite types

        if (!composite->isForwardDecl()) {
          cache[name].insert(composite);
        }
      }
    }
  }

  auto it = cache.find(compositeTypeCacheKey(t));
  return (it != cache.end() && !it->second.empty()) ? &it->second : nullptr;
}

// return true if there is SEXP somewhere within type t
typedef std::unordered_set<DIType*> DITypeSetTy;

bool containsSEXP(DIType *t, DITypeSetTy& visited, Module* m) {
  if (!t) return false;
  
  if (visited.find(t) != visited.end()) {
    // already under evaluation (recursive type)
    return false;
  }
  visited.insert(t);

  if (auto *derived = dyn_cast<DIDerivedType>(t)) {
    return containsSEXP(derived->getBaseType(), visited, m);
  }

  if (auto *composite = dyn_cast<DICompositeType>(t)) {
    if (composite->isForwardDecl()) {
      const DICompositeTypeSetTy* resolvedTypes = resolveForwardDeclaration(composite, m);
      if (!resolvedTypes) return false;

      for (auto *resolvedType : *resolvedTypes) {
        if (containsSEXP(resolvedType, visited, m)) {
          return true;
        }
      }
      return false;
    }

    if (composite->getTag() == dwarf::DW_TAG_array_type)
      return containsSEXP(composite->getBaseType(), visited, m);

    if (composite->getTag() == dwarf::DW_TAG_structure_type) {
      if (composite->getName() == "SEXPREC" || composite->getName() == "struct SEXPREC") {
        return true;
      }
  
      for (auto *element : composite->getElements()) {
        if (containsSEXP(cast<DIType>(element), visited, m)) {
          return true;
        }
      }
    }
  }
  return false;
}

bool isStructureWithSEXPFields(GlobalVariable *gv, Module* m) {
  SmallVector<DIGlobalVariableExpression *> debugInfoVector;
  gv->getDebugInfo(debugInfoVector);

  for (auto debugInfo : debugInfoVector) {
    if (!debugInfo) continue;
    DIGlobalVariable *variable = debugInfo->getVariable();

    if (!variable) continue;
    DIType *type = variable->getType();

    DITypeSetTy visited;
    if (containsSEXP(type, visited, m)) {
      return true;
    }
  }

  return false;
}

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);
    // NOTE: functionsOfInterest ignored but (re-)analyzing the R core is necessary
  
  SymbolsMapTy symbolsMap;
  findSymbols(m, &symbolsMap); // symbols are globals which hold SEXPs, but are safe
  
  for(Module::global_iterator gi = m->global_begin(), ge = m->global_end(); gi != ge ; ++gi) {
    GlobalVariable *gv = &*gi;
    
    if (isSEXP(gv)) {
      if (symbolsMap.find(gv) != symbolsMap.end()) {
        continue;
        }
    
      errs() << "non-symbol SEXP global variable " << gv->getName() << "  " << *gv << "\n";
      // many of these are OK, but it does not seem to be easily checkable
      continue;
    }
    
    if (isStructureWithSEXPFields(gv, m)) {
      errs() << "structure with SEXP fields " << gv->getName() << " " << *gv << "\n";
    }
  }
  
  delete m;
}
