/*
  Check which source lines of the C code of GNU-R may call into a GC (are a
  safepoint). 
  
  By default this ignores error paths, because due to runtime checking,
  pretty much anything then would be a safepoint.
*/

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h> 
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include "common.h"
#include "cgclosure.h"

using namespace llvm;

const std::string gcFunction = "R_gc_internal";

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterest;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterest, context);
  
  Function *gcf = m->getFunction(gcFunction);
  if (!gcf) {
    outs() << "Cannot find function " << gcFunction << ", cannot annotate safepoints.\n";
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
  
  errs() << "List of functions and callsites calling (recursively) into " << gcFunction << ":\n";

  std::string lastFile = "";
  std::string lastDirectory = "";
  unsigned lastLine = 0;
  
  for(FunctionsInfoMapTy::iterator FI = functionsMap.begin(), FE = functionsMap.end(); FI != FE; ++FI) {
    if (functionsOfInterest.find(FI->first) == functionsOfInterest.end()) {
      continue;
    }
    FunctionInfo *finfo = FI->second;

    for(std::vector<CallInfo*>::iterator CI = finfo->callInfos.begin(), CE = finfo->callInfos.end(); CI != CE; ++CI) {
      CallInfo* cinfo = *CI;
      for(std::set<FunctionInfo*>::iterator MFI = cinfo->targets.begin(), MFE = cinfo->targets.end(); MFI != MFE; ++MFI) {
        FunctionInfo *middleFinfo = *MFI;
        
        for(std::vector<FunctionInfo*>::iterator TFI = middleFinfo->calledFunctionsList.begin(), TFE = middleFinfo->calledFunctionsList.end(); TFI != TFE; ++TFI) {
          FunctionInfo *targetFinfo = *TFI;
          
          if ((*targetFinfo->callsFunctionMap)[gcFunctionIndex]) {
            const DebugLoc &callDebug = cinfo->instruction->getDebugLoc();
            const MDNode* scope = callDebug.getScopeNode(context);
            DILocation loc(scope);
            
            unsigned line = callDebug.getLine();
            std::string directory = loc.getDirectory().str();
            std::string file = loc.getFilename().str();
            
            if (line != lastLine || file != lastFile || directory != lastDirectory) {
              outs() << directory << "/" << file << " " << line << "\n";
              errs() << "  " << demangle(finfo->function->getName()) << " " << loc.getDirectory() << "/" << loc.getFilename() << ":" << callDebug.getLine() << "\n"; 
              lastFile = file;
              lastDirectory = directory;
              lastLine = line;
            } else {
              errs() << "  (GC point on another call at line) " << demangle(finfo->function->getName()) << " " << loc.getDirectory() << "/" 
                << loc.getFilename() << ":" << callDebug.getLine() << "\n"; 
            }
            break;
          }
        }
      }
    }
  }

  delete m;
}
