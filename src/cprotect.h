#ifndef RCHK_CPROTECT_H
#define RCHK_CPROTECT_H

#include "common.h"

#include <unordered_map>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

using namespace llvm;

enum CPKind {
  CP_CALLER_PROTECT = 0, // function must be called with the argument protected
  CP_CALLEE_PROTECT, // function can be called with arg unprotected and the arg value will not be collected
  CP_CALLEE_SAFE, // function can be called with arg unprotected, but the arg value may be collected when not needed anymore
  CP_TRIVIAL // the argument is not SEXP or the function does not allocate
};
  
typedef std::vector<CPKind> CPArgsTy;
typedef std::unordered_map<Function*, CPArgsTy> CPMapTy;

struct CProtectInfo {
  
  CPMapTy map;
  
  CProtectInfo(): map() {};
  
  bool isCalleeProtect(Function *fun, int argIndex, bool onlyNonTrivially);
    // trivially, non-allocating function or function with no SEXP arguments is callee protect

  bool isCalleeSafe(Function *fun, int argIndex, bool onlyNonTrivially);
    // trivially, callee protect function is callee safe
    // also, trivially callee protect function is callee safe
  
  bool isCalleeProtect(Function *fun, bool onlyNonTrivially);
    // a callee protect function has all its arguments callee protect
    // a non-trivially callee protect function has at least one of its arguments
    //   callee-protect non-trivially
    
  bool isCalleeSafe(Function *fun, bool onlyNonTrivially);
  
  bool isNonTrivial(Function *fun);
    // does it have any argument with non-trivial protection (callee safe, callee protect or caller protect)?
    
};

CProtectInfo findCalleeProtectFunctions(Module *m, FunctionsSetTy& allocatingFunctions);

#endif
