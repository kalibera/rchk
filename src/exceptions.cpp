
#include "exceptions.h"

// some manually added exceptions that so far seem too hard to find automatically
bool isKnownNonAllocator(Function *f) {
  if (isInstall(f)) return true;

  if (!f) return false;

  if (f->getName() == "mkPRIMSXP") return true; // mkPRIMSXP caches its results internally (and permanently)
  if (f->getName() == "GETSTACK_PTR_TAG") return true; // GETSTACK_PTR_TAG stores the allocated result to the byte-code stack
  
  return false;
}

bool isKnownNonAllocator(const CalledFunctionTy *f) {
  return isKnownNonAllocator(f->fun);
}

bool avoidSEXPGuardsFor(Function *f) {
  if (f->getName() == "bcEval") return true;
  return false;
}

bool avoidSEXPGuardsFor(const CalledFunctionTy *f) {
  return avoidSEXPGuardsFor(f->fun);
}

bool avoidIntGuardsFor(Function *f) {
  if (f->getName() == "_controlify") return true;
  return false;
}

bool avoidIntGuardsFor(const CalledFunctionTy *f) {
  return avoidIntGuardsFor(f->fun);
}
