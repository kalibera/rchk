/*
  Check which source lines of the C code of GNU-R may call into a GC (are a
  safepoint). 
  
  By default this ignores error paths, because due to runtime checking,
  pretty much anything then would be a safepoint.
  
  This is a more precise variant of sfpcheck, allocator detection is
  somewhat context-aware (e.g.  taking into account some constant arguments
  being passed to functions, which makes a big difference for calls like
  getAttrib)
*/

#include "common.h"
#include "callocators.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

struct LineTy {
  std::string path;
  unsigned line;
  
  LineTy(std::string path, unsigned line): path(path), line(line) {}
};

struct LineTy_compare {
  bool operator() (const LineTy& lhs, const LineTy& rhs) const {
    int cmp = lhs.path.compare(rhs.path);
    if (cmp) {
      return cmp < 0;
    }
    return lhs.line < rhs.line;
  }
};

typedef std::set<LineTy, LineTy_compare> LinesTy;

int main(int argc, char* argv[])
{
  LLVMContext context;

  FunctionsOrderedSetTy functionsOfInterest;
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  CalledModuleTy *cm = CalledModuleTy::create(m);

  const CallSiteTargetsTy *callSiteTargets = cm->getCallSiteTargets();
  const CalledFunctionsSetTy *allocatingCFunctions = cm->getAllocatingCFunctions();
  
  LinesTy sfpLines;
  
  for(CallSiteTargetsTy::const_iterator ci = callSiteTargets->begin(), ce = callSiteTargets->end(); ci != ce; ++ci) {
    Value *inst = ci->first;
    Function *csFun = cast<Instruction>(inst)->getParent()->getParent();
    if (functionsOfInterest.find(csFun) == functionsOfInterest.end()) {
        continue;
    }
    const CalledFunctionsSetTy& funcs = ci->second;
    
    for(CalledFunctionsSetTy::const_iterator fi = funcs.begin(), fe = funcs.end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      
      if (allocatingCFunctions->find(f) != allocatingCFunctions->end()) {
        std::string path;
        unsigned line;
        sourceLocation(cast<Instruction>(inst), path, line);
        LineTy l(path, line);
        sfpLines.insert(l);
      }
    }
  }

  for(LinesTy::const_iterator li = sfpLines.begin(), le = sfpLines.end(); li != le; ++li) {
    const LineTy& l = *li;
    outs() << l.path << " " << std::to_string(l.line) << "\n";
  }
  
  CalledModuleTy::release(cm);  
  delete m;
}  

