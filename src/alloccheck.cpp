/* 
  This tool attempts to detect "allocators". An allocator is a function that
  returns a newly allocated pointer.  An allocator may indeed be a wrapper
  for other allocators, so there is a lot of allocators in the R source code.
*/

#include "common.h"
       
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "allocators.h"
#include "callocators.h"
#include "errors.h"
#include "cprotect.h"

using namespace llvm;

int main(int argc, char* argv[])
{
  LLVMContext context;

  FunctionsOrderedSetTy functionsOfInterest;
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
    CalledModuleTy *cm = CalledModuleTy::create(m);

  FunctionsSetTy *possibleAllocators = cm->getPossibleAllocators();
  FunctionsSetTy *allocatingFunctions = cm->getAllocatingFunctions();
  const CalledFunctionsIndexTy* calledFunctions = cm->getCalledFunctions();

  outs() << "Callee protect functions: \n";
  CProtectInfo cprotect = findCalleeProtectFunctions(m, *allocatingFunctions);
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *fun = fi;
    if (functionsOfInterest.find(fun) == functionsOfInterest.end()) {
      continue;
    }
    if (cprotect.isCalleeProtect(fun, true /* non-trivially */)) {
      outs() << "  " << funName(fun) << "\n";
    }
  }
  outs() << "\n";

  outs() << "Callee safe functions (non-trivially, excluding callee-protect): \n";
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *fun = fi;
    if (functionsOfInterest.find(fun) == functionsOfInterest.end()) {
      continue;
    }
    if (cprotect.isCalleeSafe(fun, true /* non-trivially */)) {
      outs() << "  " << funName(fun) << "\n";
    }
  }
  outs() << "\n";
  
  outs() << "Mixed callee-protect/callee-safe functions [ callee-[S]afe callee-[P]rotect caller-protect[!] non-SEXP[-] ]: \n";
  for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
    Function *fun = fi;
    if (functionsOfInterest.find(fun) == functionsOfInterest.end()) {
      continue;
    }
    if (cprotect.isCalleeSafe(fun, true) || cprotect.isCalleeProtect(fun, true)) {
      continue;
    }
    if (!cprotect.isNonTrivial(fun)) {
      continue;
    }
    auto fsearch = cprotect.map.find(fun);
    assert(fsearch != map.end()); 
    CPArgsTy& cpargs = fsearch->second;
  
    unsigned nargs = cpargs.size();
    
    bool seenNonTrivialNonCallerProtect = false;
    for(unsigned i = 0; i < nargs; i++) {
      CPKind k = cpargs.at(i);
      if (k == CP_CALLER_PROTECT || k == CP_TRIVIAL) {
        continue;
      }
      seenNonTrivialNonCallerProtect = true;
    }
    if (!seenNonTrivialNonCallerProtect) {
      continue; // only show interesting functions
    }
    
    outs() << "  " << funName(fun) << " ";
    for(unsigned i = 0; i < nargs; i++) {
      CPKind k = cpargs.at(i);
      switch(k) {
        case CP_TRIVIAL: outs() << "-"; break;
        case CP_CALLEE_SAFE: outs() << "S"; break;
        case CP_CALLEE_PROTECT: outs() << "P"; break;
        case CP_CALLER_PROTECT: outs() << "!"; break;
        default:
          assert(false);
      }
    }
    outs() << "\n";
  }
  outs() << "\n";

  if (0) {  
    outs() << "Detected called functions: \n";
    for(CalledFunctionsIndexTy::const_iterator fi = calledFunctions->begin(), fe = calledFunctions->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "  called function " << funName(f) << "\n";
    }
  }

  const FunctionsSetTy *csPossibleAllocators = cm->getContextSensitivePossibleAllocators();
  const FunctionsSetTy *csAllocatingFunctions = cm->getContextSensitiveAllocatingFunctions();

  if (1) {
    for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
      Function *fun = fi;
      if (functionsOfInterest.find(fun) == functionsOfInterest.end()) {
        continue;
      }
      if (csPossibleAllocators->find(fun) != csPossibleAllocators->end()) {
        outs() << "CS-ALLOCATOR: " << funName(fun) << "\n";
      }
    }
    outs() << "\n";
  }
  
  if (1) {
    for(Module::iterator fi = m->begin(), fe = m->end(); fi != fe; ++fi) {
      Function *fun = fi;
      if (functionsOfInterest.find(fun) == functionsOfInterest.end()) {
        continue;
      }
      if (csAllocatingFunctions->find(fun) != csAllocatingFunctions->end()) {
        outs() << "CS-ALLOCATING: " << funName(fun) << "\n";
      }
    }
    outs() << "\n";
  }

  const CalledFunctionsSetTy *possibleCAllocators = cm->getPossibleCAllocators();
  const CalledFunctionsSetTy *allocatingCFunctions = cm->getAllocatingCFunctions();

  if(1) {
    for(CalledFunctionsSetTy::const_iterator fi = possibleCAllocators->begin(), fe = possibleCAllocators->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "C-ALLOCATOR: " << funName(f) << "\n";
    }
  }

  if(1) {
    for(CalledFunctionsSetTy::const_iterator fi = allocatingCFunctions->begin(), fe = allocatingCFunctions->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "C-ALLOCATING: " << funName(f) << "\n";
    }
  }

  if(1) {
    for(FunctionsSetTy::iterator fi = possibleAllocators->begin(), fe = possibleAllocators->end(); fi != fe; ++fi) {
      Function *f = *fi;
      if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "ALLOCATOR: " << funName(f) << "\n";
    }
  }
  

  if(1) {
    for(FunctionsSetTy::iterator fi = allocatingFunctions->begin(), fe = allocatingFunctions->end(); fi != fe; ++fi) {
      Function *f = *fi;
      if (functionsOfInterest.find(f) == functionsOfInterest.end()) {
        continue;
      }
      outs() << "ALLOCATING: " << funName(f) << "\n";
    }
  }
  
  // check for which functions the context gave more precise result
  if (1) {  
    for(CalledFunctionsIndexTy::const_iterator fi = calledFunctions->begin(), fe = calledFunctions->end(); fi != fe; ++fi) {
      const CalledFunctionTy *f = *fi;
      if (functionsOfInterest.find(f->fun) == functionsOfInterest.end()) {
        continue;
      }
      bool callocator = possibleCAllocators->find(f) != possibleCAllocators->end();
      bool callocating = allocatingCFunctions->find(f) != allocatingCFunctions->end();
      bool allocator = possibleAllocators->find(f->fun) != possibleAllocators->end();
      bool allocating = allocatingFunctions->find(f->fun) != allocatingFunctions->end();
      
      if (!callocator && allocator) {
        outs() << "GOOD: NOT-CALLOCATOR but ALLOCATOR: " << funName(f) << "\n";
      }
      if (!callocating && allocating) {
        outs() << "GOOD: NOT-CALLOCATING but ALLOCATING: " << funName(f) << "\n";
      }
      if (callocator && !callocating) {
        outs() << "ERROR: NOT-CALLOCATING but CALLOCATOR: " << funName(f) << "\n";
      }
      if (allocator && !allocating) {
        outs() << "ERROR: NOT-ALLOCATING but ALLOCATOR: " << funName(f) << "\n";
      }
      if (callocator && !allocator) {
        outs() << "ERROR: C-ALLOCATOR but not ALLOCATOR: " << funName(f) << "\n";
      }
      if (callocating && !allocating) {
        outs() << "ERROR: C-ALLOCATING but not ALLOCATING: " << funName(f) << "\n";
      }
    }
  }

  CalledModuleTy::release(cm);  
  delete m;
}
