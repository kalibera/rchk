#include "cache.h"

#include "common.h"

#include <algorithm>
#include <functional>
#include <vector>

// This program tests whether CAllocatorCacheTy correctly serializes and deserializes objects from CalledModuleTy
// NOTE: As this test recomputes entire analysis, clearCommonCaches() must be called
//       before each test to clear results from static caches

std::string baseIRFile = "./src/main/R.bin.bc";
std::string cacheName = baseIRFile + ".cache";
std::string execName = "../src/cachecheck";
std::vector<std::string> packageIRFiles = {
  "./.conftest.bc",
  "./src/library/tools/src/tools.so.bc",
  "./src/library/parallel/src/parallel.so.bc",
  "./src/library/grid/src/grid.so.bc",
  "./src/library/tcltk/src/tcltk.so.bc",
  "./src/library/utils/src/utils.so.bc",
  "./src/library/splines/src/splines.so.bc",
  "./src/library/stats/src/stats.so.bc",
  "./src/library/grDevices/src/grDevices.so.bc",
  "./src/library/grDevices/src/cairo/cairo.so.bc",
  "./src/library/graphics/src/graphics.so.bc",
  "./src/library/methods/src/methods.so.bc",
  "./src/modules/internet/internet.so.bc",
  "./src/modules/lapack/lapack.so.bc",
  "./src/modules/X11/R_X11.so.bc",
  "./src/modules/X11/R_de.so.bc",
  "./library/cluster/libs/cluster.so.bc",
  "./library/rpart/libs/rpart.so.bc",
  "./library/tools/libs/tools.so.bc",
  "./library/class/libs/class.so.bc",
  "./library/parallel/libs/parallel.so.bc",
  "./library/mgcv/libs/mgcv.so.bc",
  "./library/Matrix/libs/Matrix.so.bc",
  "./library/KernSmooth/libs/KernSmooth.so.bc",
  "./library/grid/libs/grid.so.bc",
  "./library/tcltk/libs/tcltk.so.bc",
  "./library/nnet/libs/nnet.so.bc",
  "./library/utils/libs/utils.so.bc",
  "./library/splines/libs/splines.so.bc",
  "./library/stats/libs/stats.so.bc",
  "./library/survival/libs/survival.so.bc",
  "./library/grDevices/libs/grDevices.so.bc",
  "./library/grDevices/libs/cairo.so.bc",
  "./library/graphics/libs/graphics.so.bc",
  "./library/nlme/libs/nlme.so.bc",
  "./library/foreign/libs/foreign.so.bc",
  "./library/lattice/libs/lattice.so.bc",
  "./library/methods/libs/methods.so.bc",
  "./library/MASS/libs/MASS.so.bc",
  "./library/spatial/libs/spatial.so.bc",
  "./modules/R_X11.so.bc",
  "./modules/lapack.so.bc",
  "./modules/R_de.so.bc",
  "./modules/internet.so.bc"
};

// There are multiple interning tables involved in CalledModuleTy and ArgInfoTy:
//  - CalledFunctionsOrderedSetTy interned via osTable
//  - CalledFunctionTy interned via calledFunctionsTable
//  - ArgInfosVectorTy interned via argInfoVectorsTable
//  - SymbolArgInfoTy interned via SymbolArgInfoTy::table
//
// In order check whether deserialization correctly restores the sets of CalledFunctionTy,
// we need to compare them without relying on pointers of interned elements.

struct CalledFunctionCmp {
  // uses fun and argInfo for comparison, ignore module and idx as they differ between runs
  bool operator()(const CalledFunctionTy* a, const CalledFunctionTy* b) const {
    return a->getName() < b->getName();
  }
};

// Helper functions
template<typename T, typename Comparator>
std::vector<T> toSortedVector(const std::unordered_set<T>* set, Comparator comparator) {
  std::vector<T> vec(set->begin(), set->end());
  std::sort(vec.begin(), vec.end(), comparator);
  return vec;
}

template<typename SetTy, typename Comparator = std::less<typename SetTy::value_type>>
bool compareSets(const SetTy *original, const SetTy *restored, Comparator cmp = Comparator{}) {
  if (original->size() != restored->size()) {
    errs() << "Set sizes do not match: " << original->size() << " vs " << restored->size() << "\n";
    return false;
  }

  auto originalVec = toSortedVector(original, cmp);
  auto restoredVec = toSortedVector(restored, cmp);

  using ElemTy = typename SetTy::value_type;
  return std::equal(originalVec.begin(), originalVec.end(), restoredVec.begin(),
    [&cmp](ElemTy a, ElemTy b) {
      return !cmp(a, b) && !cmp(b, a);
    });
}

bool compareCalledFunctionsMaps(const CallSiteTargetsTy *original, const CallSiteTargetsTy *restored) {
  if (original->size() != restored->size()) {
    errs() << "Set sizes do not match: " << original->size() << " vs " << restored->size() << "\n";
    return false;
  }

  for (auto const& [key, val] : *original) {
    if (restored->find(key) == restored->end()) {
      return false;
    }
    if (!compareSets(&val, &restored->at(key), CalledFunctionCmp{})) {
      return false;
    }
  }
  return true;
}

bool compareCalledModules(CalledModuleTy *original, CalledModuleTy *restored) {
  bool ok = true;
  if (!compareSets(original->getPossibleCAllocators(), restored->getPossibleCAllocators(), CalledFunctionCmp{})) {
    errs() << "Possible CAllocators do not match\n";
    ok = false;
  }
  if (!compareSets(original->getAllocatingCFunctions(), restored->getAllocatingCFunctions(), CalledFunctionCmp{})) {
    errs() << "Allocating CFunctions do not match\n";
    ok = false;
  }
  if (!compareSets(original->getContextSensitivePossibleAllocators(), restored->getContextSensitivePossibleAllocators())) {
    errs() << "ContextSensitivePossibleAllocators do not match\n";
    ok = false;
  }
  if (!compareSets(original->getContextSensitiveAllocatingFunctions(), restored->getContextSensitiveAllocatingFunctions())) {
    errs() << "ContextSensitiveAllocatingFunctions do not match\n";
    ok = false;
  }
  if (!compareCalledFunctionsMaps(original->getCallSiteTargets(), restored->getCallSiteTargets())) {
    errs() << "CallSiteTargets do not match\n";
    ok = false;
  }
  if (ok) {
    errs() << "All checks passed\n";
  }
  return ok;
}

void runCheck(int argc, char* argv[]) {
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  std::string cacheFile;
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context, &cacheFile);
  
  // create original CalledModuleTy
  CalledModuleTy *original = CalledModuleTy::create(m);
  original->getCallSiteTargets(); // triggers computeCalledAllocators

  // restore CalledModuleTy from cache
  CAllocatorCacheTy cache(cacheFile);
  CalledModuleTy *restored = CalledModuleTy::create(m);
  cache.deserialize(restored);

  errs() << "\n";
  compareCalledModules(original, restored);
}

// checks whether serializing and deserializing R cache works
void checkR() {
  clearCommonCaches();
  const char* argv[] = {execName.c_str(), "--cache", cacheName.c_str(), baseIRFile.c_str()};
  int argc = sizeof(argv) / sizeof(argv[0]);
  errs() << "\nComparing original and restored R cache\n";
  runCheck(argc, const_cast<char**>(argv));
}

// checks whether serializing and deserializing R cache works with survival package
void checkRWithPackage(std::string packageIRFile) {
  clearCommonCaches();
  const char* argv[] = {execName.c_str(), "--cache", cacheName.c_str(), baseIRFile.c_str(), packageIRFile.c_str()};
  int argc = sizeof(argv) / sizeof(argv[0]);
  errs() << "\nComparing original and restored R cache with " << packageIRFile << " package\n";
  runCheck(argc, const_cast<char**>(argv));
}

void malformedArgsCheck() {
  clearCommonCaches();
  const char* argv[] = {execName.c_str(), "--cache", cacheName.c_str(), baseIRFile.c_str()};
  int argc = sizeof(argv) / sizeof(argv[0]);
  errs() << "\nTesting malformed arguments\n";
  runCheck(argc, const_cast<char**>(argv));
}

int main() {
  createRCacheFile(baseIRFile, cacheName);

  checkR();
  
  for (const auto& packageIRFile : packageIRFiles) {
    checkRWithPackage(packageIRFile);
  }
  malformedArgsCheck();
  return 0;
}
