#ifndef RCHK_CGCLOSURE_H
#define RCHK_CGCLOSURE_H

#include <map>
#include <set>
#include <vector>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>

using namespace llvm;

struct FunctionInfo;

struct CallInfo {
  const Instruction* const instruction;
  std::set<FunctionInfo*> targets;
  
  public:
  CallInfo(const Instruction* const i): instruction(i) {};
};

struct FunctionInfo {  
  const Function* const function;
  std::vector<CallInfo*> callInfos;
  std::vector<bool>* callsFunctionMap;
  std::vector<FunctionInfo*> calledFunctionsList;
  unsigned long index;
  
  public:
  FunctionInfo(const Function* const f, unsigned long id): function(f), index(id), callsFunctionMap(NULL) {};
};

typedef std::map<Function*, FunctionInfo*> FunctionsInfoMapTy;

void buildCGClosure(Module *m, FunctionsInfoMapTy& functionsMap, bool ignoreErrorPaths = true);

#endif
