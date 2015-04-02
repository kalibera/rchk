
#include "common.h"

#include <cxxabi.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/SourceMgr.h>

#include <llvm/Support/raw_ostream.h>

// supported usage
//   tool
//     processes R.bin.bc
//   tool path/R.bin.bc
//     processes one file at given location (R binary)
//   tool patr/R.bin.bc path/module.bc
//     links module agains given base IR file and then checks only functions
//     from that module (but some tools need to do whole-program analysis
//     which also will include functions from the base
//      IR file not included in the module)
Module *parseArgsReadIR(int argc, char* argv[], FunctionsOrderedSetTy& functionsOfInterest, LLVMContext& context) {

  if (argc > 3) {
    errs() << argv[0] << " base_file.bc [module_file.bc]" << "\n";
    exit(1);
  }

  SMDiagnostic error;
  std::string baseFname;
  
  if (argc == 1) {
    baseFname = "R.bin.bc";  
  } else {
    baseFname = argv[1];
  }
  
  Module *base = ParseIRFile(baseFname, error, context);
  if (!base) {
    errs() << "ERROR: Cannot read base IR file " << baseFname << "\n";
    error.print(argv[0], errs());
    exit(1);
  }
  
  if (argc == 1 || argc == 2) {
    // only a single input file
    for(Module::iterator f = base->begin(), fe = base->end(); f != fe; ++f) {
      functionsOfInterest.insert(f);
    }
    return base;
  }
  
  // have two input files
  std::string moduleFname = argv[2];
  Module *module = ParseIRFile(moduleFname, error, context);
  if (!module) {
    errs() << "ERROR: Cannot read module IR file " << moduleFname << "\n";
    error.print(argv[0], errs());
    exit(1);  
  }
  std::string errorMessage;
  
  // turn all the global functions and variables in the module to weak linkage
  // this is to somewhat mimick the behavior of symbol resolution
  // (and mainly of multiply defined symbols not being fatal) during the
  // usual loading of R modules
  
  for(Module::global_iterator gv = module->global_begin(), gve = module->global_end(); gv != gve; ++gv) {
    gv->setLinkage(GlobalValue::WeakAnyLinkage);
  }
  for(Module::iterator f = module->begin(), fe = module->end(); f != fe; ++f) {
    f->setLinkage(GlobalValue::WeakAnyLinkage); 
  }
  
  if (Linker::LinkModules(base, module, Linker::PreserveSource, &errorMessage)) {
    errs() << "Linking module " << moduleFname << " with base " << baseFname << " resulted in error " << errorMessage << ".\n";
  }
  for(Module::iterator f = module->begin(), fe = module->end(); f != fe; ++f) {
    if (!f->isDeclaration()) {
      functionsOfInterest.insert(base->getFunction(f->getName()));
    }
  }
  delete module;

  return base;
}

std::string demangle(std::string name) {
  int status;
  char *dname = abi::__cxa_demangle(name.c_str(), 0, 0, &status);
  if (status == 0) {
    std::string res(dname);
    return res;
  } else {
    return name;
  }
}

bool sourceLocation(const Instruction *in, std::string& path, unsigned& line) {
  if (!in) {
    return false;
  }
  const DebugLoc& debugLoc = in->getDebugLoc();
  
  if (debugLoc.isUnknown()) {
    path = "/unknown";
    line = 0;
    return false;
  }

  line = debugLoc.getLine();  
  DILocation loc(debugLoc.getScopeNode(in->getContext()));

  if (sys::path::is_absolute(loc.getFilename())) {
    path = loc.getFilename().str();
  } else {
    path = loc.getDirectory().str() + "/" + loc.getFilename().str();
  }
  return true;
}

std::string sourceLocation(const Instruction *in) {
  unsigned line;
  std::string path;
  
  if (!sourceLocation(in, path, line)) {
    return "<unknown location>";
  } else {
    return path + ":" + std::to_string(line);
  }
}

std::string funLocation(const Function *f) {
  const Instruction *instWithDI = NULL;
  for(Function::const_iterator bb = f->begin(), bbe = f->end(); !instWithDI && bb != bbe; ++bb) {
    for(BasicBlock::const_iterator in = bb->begin(), ine = bb->end(); !instWithDI && in != ine; ++in) {
      if (!in->getDebugLoc().isUnknown()) {
        instWithDI = in;
      }
    }
  }
  return sourceLocation(instWithDI);
}

std::string instructionAsString(const Instruction *in) {
  std::string str;
  raw_string_ostream os(str);
  os << *in;
  return str;
}

std::string funName(const Function *f) {
  if (!f) {
    return "<unknown function>";
  }
  return demangle(f->getName());
}

std::string varName(const AllocaInst *var) {
  std::string name = var->getName().str();
  if (!name.empty()) {
    return name;
  }
  return "<unnamed var: " + instructionAsString(var) + ">";

}

bool isSEXP(Type* type) {

  if (!PointerType::classof(type)) {
    return false;
  }
  Type *etype = (cast<PointerType>(type))->getPointerElementType();
  if (!StructType::classof(etype)) {
    return false;
  }
  StructType *estr = cast<StructType>(etype);
  if (!estr->hasName() || estr->getName() != "struct.SEXPREC") {
    return false;
  }
  return true;
}

bool isSEXPPtr(Type* type) {
  if (!PointerType::classof(type)) {
    return false;
  }
  return isSEXP(cast<PointerType>(type)->getPointerElementType());
}

bool isSEXP(GlobalVariable *var) {
  return isSEXPPtr(var->getType());
}

bool isSEXP(AllocaInst* var) {
  if (var->isArrayAllocation() /* need to check this? */) {
    return false;
  }
  return isSEXP(var->getAllocatedType());
}

bool isInstall(Function *f) {
  return f && (f->getName() == "Rf_install" || f->getName() == "Rf_installTrChar" ||
    f->getName() == "Rf_installChar" || f->getName() == "Rf_installS3Signature");
}

bool isTypeTest(Function *f, const GlobalsTy* g) {
  return f == g->isNullFunction || f == g->isSymbolFunction || f == g->isLogicalFunction || f == g->isRealFunction ||
    f == g->isComplexFunction || f == g->isExpressionFunction || f == g->isEnvironmentFunction || f == g->isStringFunction;
}


SEXPType GlobalsTy::getTypeForTypeTest(Function *f) const {
  auto tsearch = typesMap.find(f);
  if (tsearch != typesMap.end()) {
    return tsearch->second;
  }
  return RT_UNKNOWN;
}

GlobalsTy::GlobalsTy(Module *m) : typesMap() {
  protectFunction = getSpecialFunction(m, "Rf_protect");
  protectWithIndexFunction = getSpecialFunction(m, "R_ProtectWithIndex");
  unprotectFunction = getSpecialFunction(m, "Rf_unprotect");
  unprotectPtrFunction = getSpecialFunction(m, "Rf_unprotect_ptr");
  ppStackTopVariable = getSpecialVariable(m, "R_PPStackTop");
  nilVariable = getSpecialVariable(m, "R_NilValue");
  
    // mutually exclusive test functions
  isNullFunction = getSpecialFunction(m, "Rf_isNull");
  isSymbolFunction = getSpecialFunction(m, "Rf_isSymbol");
  isLogicalFunction = getSpecialFunction(m, "Rf_isLogical");
  isRealFunction = getSpecialFunction(m, "Rf_isReal");
  isComplexFunction = getSpecialFunction(m, "Rf_isComplex");
  isExpressionFunction = getSpecialFunction(m, "Rf_isExpression");
  isEnvironmentFunction = getSpecialFunction(m, "Rf_isEnvironment");
  isStringFunction = getSpecialFunction(m, "Rf_isString");
  
  typesMap.insert({isNullFunction, RT_NIL});
  typesMap.insert({isSymbolFunction, RT_SYMBOL});
  typesMap.insert({isLogicalFunction, RT_LOGICAL});
  typesMap.insert({isRealFunction, RT_REAL});
  typesMap.insert({isComplexFunction, RT_COMPLEX});
  typesMap.insert({isExpressionFunction, RT_EXPRESSION});
  typesMap.insert({isEnvironmentFunction, RT_ENVIRONMENT});
  typesMap.insert({isStringFunction, RT_STRING});
}
  
Function* GlobalsTy::getSpecialFunction(Module *m, std::string name) {
  Function *f = m->getFunction(name);
  if (!f) {
    errs() << "  Function " << name << " not found in module (won't check its use).\n";
  }
  return f;
}
    
GlobalVariable* GlobalsTy::getSpecialVariable(Module *m, std::string name) {
  GlobalVariable *v = m->getGlobalVariable(name, true);
  if (!v) {
    errs() << "  Variable " << name << " not found in module (won't check its use).\n";
  }
  return v;
}
