
#include "vectors.h"
#include "patterns.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

bool isVectorGuard(Function *f) {
  if (!f) return false;
  return f->getName() == "Rf_isPrimitive" || f->getName() == "Rf_isList" || f->getName() == "Rf_isFunction" ||
    f->getName() == "Rf_isPairList" || f->getName() == "Rf_isLanguage" || f->getName() == "Rf_isVector" ||
    f->getName() == "Rf_isVectorList" || f->getName() == "Rf_isVectorAtomic";
}

bool trueForVector(Function *f) { // myvector => true branch
  return f->getName() == "Rf_isVector";
}

bool trueForNonVector(Function *f) { // !myvector => true branch
  return false;
}

bool falseForVector(Function *f) { // myvector => false branch
  return f->getName() == "Rf_isPrimitive" || f->getName() == "Rf_isList" || f->getName() == "Rf_isFunction" || 
    f->getName() == "Rf_isPairList" || f->getName() == "Rf_isLanguage";
}

bool falseForNonVector(Function *f) { // !myvector => false branch
  return f->getName() == "Rf_isVector" || f->getName() == "Rf_isVectorList" || f->getName() == "Rf_isVectorAtomic";
}

bool impliesVectorWhenTrue(Function *f) {
  return f->getName() == "Rf_isVector" || f->getName() == "Rf_isVectorList" || f->getName() == "Rf_isVectorAtomic";
}

bool impliesVectorWhenFalse(Function *f) {
  return false;
}

bool isVectorType(unsigned type) {
  switch(type) {
    case RT_LOGICAL:
    case RT_INT:
    case RT_REAL:
    case RT_COMPLEX:
    case RT_STRING:
    case RT_VECTOR:
    case RT_INTCHAR:
    case RT_RAW:
    case RT_EXPRESSION:
    case RT_CHAR:
      return true;
    default:
      return false;
  }
}

bool isVectorProducingCall(Value *inst) {
  unsigned type;
  
  if (isAllocVectorOfKnownType(inst, type)) {
    return isVectorType(type);
  }
  
  return false;
}

