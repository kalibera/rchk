/*
  Check for a particular call pattern, multiple allocating arguments, that
  is (used to?) be a common source of PROTECT errors. Calls such as
  
  cons(install("x"), ScalarInt(1))
  
  where at least two arguments are given as immediate results of allocating
  functions.  It does not matter that cons protects is arguments - if
  ScalarInt is evaluated before install, then install may allocate,
  thrashing that scalar integer.
  
  By default the checking ignores error paths.

*/

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h> 
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

#include <llvm/Support/raw_ostream.h>

#include "cgclosure.h"

using namespace llvm;

const std::string gcFunction = "R_gc_internal";

int main(int argc, char* argv[])
{
  LLVMContext context;
  SMDiagnostic error;
  
  std::string fname;
  if (argc == 2) {
    fname = argv[1];
  } else {
    fname = "R.bin.bc";
    outs() << "Input file not specified, using the default " << fname << "\n";
  }
  
  errs() << "Input file: " << fname << "\n";

  Module *m = ParseIRFile(fname, error, context);
  if (!m) {
    error.print("maacheck", errs());
    return 1;
  }
  
  Function *gcf = m->getFunction(gcFunction);
  if (!gcf) {
    outs() << "Cannot find function " << gcFunction << ".\n";
    return 1;
  }
  
  FunctionsInfoMapTy functionsMap;
  buildCGClosure(m, functionsMap, true /* ignore error paths */);
  
  auto fsearch = functionsMap.find(gcf);
  if (fsearch == functionsMap.end()) {
    outs() << "Cannot find function info in callgraph for function " << gcFunction << ", internal error?\n";
    return 1;
  }
  unsigned gcFunctionIndex = fsearch->second->index;
  
  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    FunctionInfo *finfo = FI->second;

    for(std::vector<CallInfo*>::iterator CI = finfo->callInfos.begin(), CE = finfo->callInfos.end(); CI != CE; ++CI) {
      CallInfo* cinfo = *CI;
      for(std::set<FunctionInfo*>::iterator MFI = cinfo->targets.begin(), MFE = cinfo->targets.end(); MFI != MFE; ++MFI) {
        FunctionInfo *middleFinfo = *MFI;
        
        const Instruction* inst = cinfo->instruction;
        unsigned nFreshObjects = 0;
        
        for(unsigned u = 0, nop = inst->getNumOperands(); u < nop; u++) {
          Value* o = inst->getOperand(u);
          
          if (!CallInst::classof(o)) continue; // argument does not come (immediatelly) from a call
          
          CallInst *cinst = cast<CallInst>(o);
          Function *fun = cinst->getCalledFunction();
          if (!fun) continue;
          
          if (!PointerType::classof(fun->getReturnType())) continue; // argument is not a pointer
          Type *etype = fun->getReturnType()->getPointerElementType();
          if (!StructType::classof(etype)) continue;
          StructType *estr = cast<StructType>(etype);
          if (!estr->hasName() || estr->getName() == "SEXPREC") continue; // argument is not SEXP

          auto fsearch = functionsMap.find(const_cast<Function*>(fun));
          if (fsearch == functionsMap.end()) continue; // should not happen
          FunctionInfo *finfo = fsearch->second;
          
          if (!(*finfo->callsFunctionMap)[gcFunctionIndex]) continue; 
             // argument does not come from a call to an allocating function
             
          nFreshObjects++;
        }
        
        if (nFreshObjects > 1) {
          const DebugLoc &callDebug = inst->getDebugLoc();
          const MDNode* scope = callDebug.getScopeNode(context);
          DILocation loc(scope);
           
          outs() << "WARNING Suspicious call (two or more unprotected arguments) at " << finfo->function->getName() << " " << loc.getDirectory() << "/"
            << loc.getFilename() << ":" << callDebug.getLine() << "\n"; 
        }
      }
    }
  }
  

  delete m;
}
