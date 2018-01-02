
#include "common.h"

#include <cxxabi.h>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/SourceMgr.h>

#include <llvm/Support/raw_ostream.h>

struct {
  bool operator()(Function *a, Function *b) {
  
    if (!b) {
      return false;
    }
    if (!a) {
      return true;
    }
    
    std::string aname = funName(a);
    std::string bname = funName(b);
    
    int cmp = aname.compare(bname);
    if (cmp < 0) {
      return true;
    }
    if (cmp > 0) {
      return false;
    }
    return a < b; // FIXME
  }
} FunctionLess;

// sort functions in the set by name, return the order as vector
void sortFunctionsByName(FunctionsOrderedSetTy& functionsOfInterestSet, FunctionsVectorTy& functionsOfInterestVector) {

  functionsOfInterestVector.insert(functionsOfInterestVector.begin(), functionsOfInterestSet.begin(), functionsOfInterestSet.end());
  std::sort(functionsOfInterestVector.begin(), functionsOfInterestVector.end(), FunctionLess);
}

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
Module *parseArgsReadIR(int argc, char* argv[], FunctionsOrderedSetTy& functionsOfInterestSet, FunctionsVectorTy& functionsOfInterestVector, LLVMContext& context) {

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
  
  Module* base = parseIRFile(baseFname, error, context).release();
  if (!base) {
    errs() << "ERROR: Cannot read base IR file " << baseFname << "\n";
    error.print(argv[0], errs());
    exit(1);
  }
  
  if (argc == 1 || argc == 2) {
    // only a single input file
    for(Module::iterator f = base->begin(), fe = base->end(); f != fe; ++f) {
      Function *fun = &*f;
      functionsOfInterestSet.insert(fun);
    }
    sortFunctionsByName(functionsOfInterestSet, functionsOfInterestVector);
    return base;
  }
  
  // have two input files
  std::string moduleFname = argv[2];
  std::unique_ptr<Module> module = parseIRFile(moduleFname, error, context);
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
  
  std::vector<std::string> functionNames;
  for(Module::iterator f = module->begin(), fe = module->end(); f != fe; ++f) {
    if (!f->isDeclaration()) {
      functionNames.push_back(f->getName().str());
    }
  }  
  
  
  if (Linker::linkModules(*base, move(module))) {
    errs() << "Linking module " << moduleFname << " with base " << baseFname << " resulted in an error.\n";
  }
  
  for(std::vector<std::string>::iterator ni = functionNames.begin(), ne = functionNames.end(); ni != ne; ++ni) {
    std::string name = *ni;
    Function *fun = base->getFunction(name);
    if (fun)
      functionsOfInterestSet.insert(fun);
      
    // fun may be NULL when a package defines a function (e.g. latin1locale
    // in package tau), but R has the same symbol as non-function
  }

  sortFunctionsByName(functionsOfInterestSet, functionsOfInterestVector);
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
  
  if (!debugLoc) {
    path = "/unknown";
    line = 0;
    return false;
  }

  line = debugLoc.getLine();  
  if (DIScope *scope = dyn_cast<DIScope>(debugLoc.getScope())) {
    if (sys::path::is_absolute(scope->getFilename())) {
      path = scope->getFilename().str();
    } else {
      path = scope->getDirectory().str() + "/" + scope->getFilename().str();
    }
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
      if (in->getDebugLoc()) {
        instWithDI = &*in;
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

typedef std::map<const AllocaInst*, std::string> VarNamesTy;

std::string computeVarName(const AllocaInst *var) {
  if (!var) return "NULL";
  std::string name = var->getName().str();
  if (!name.empty()) {
    return name;
  }

  const Function *f = var->getParent()->getParent();

  // there ought be a simpler way in LLVM, but it seems there is not  
  for(const_inst_iterator ii = inst_begin(*f), ie = inst_end(*f); ii != ie; ++ii) {
    const Instruction *in = &*ii;
  
    if (const DbgDeclareInst *ddi = dyn_cast<DbgDeclareInst>(in)) {
      if (ddi->getAddress() == var) {
        return ddi->getVariable()->getName();
      }
    } else if (const DbgValueInst *dvi = dyn_cast<DbgValueInst>(in)) {
      if (dvi->getValue() == var) {
        return dvi->getVariable()->getName();
      }
    }
  }
  return "<unnamed var: " + instructionAsString(var) + ">";
}

std::string varName(const AllocaInst *var) {

  static VarNamesTy cache;
  
  auto vsearch = cache.find(var);
  if (vsearch != cache.end()) {
    return vsearch->second;
  }
  
  std::string name = computeVarName(var);
  cache.insert({var, name});
  return name;
}

bool isPointerToStruct(Type* type, std::string name) {
  if (!PointerType::classof(type)) {
    return false;
  }
  Type *etype = (cast<PointerType>(type))->getPointerElementType();
  if (!StructType::classof(etype)) {
    return false;
  }
  StructType *estr = cast<StructType>(etype);
  if (!estr->hasName() || estr->getName() != name) {
    return false;
  }
  return true;
}

bool isSEXP(Type* type) {
  return isPointerToStruct(type, "struct.SEXPREC");
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

bool isProtectingFunction(Function *f) {
  return f && (f->getName() == "Rf_protect" || f->getName() == "R_ProtectWithIndex" ||
    f->getName() == "R_PreserveObject" || f->getName() == "R_Reprotect");
}

bool isSetterFunction(Function *f) {
  if (!f) return false;
  
  /* Note some setters below are not always setters, conversion may happen
     before they set, or they may do something else when the length of some
     argument is zero.  As a heuristic, they are still treated as setters,
     because if they convert, code using the original objects later will be
     wrong anyway, and hopefully the zero-length objects won't be reused. */
  
  if (f->getName() == "Rf_setAttrib") return true; // TODO: not always true, names conversion may happen
  if (f->getName() == "Rf_namesgets") return true; // TODO: not true when names is a pairlist, conversion will then happen!
  if (f->getName() == "Rf_dimnamesgets") return true; // TODO: not always true, conversion may happen, or length zero
  if (f->getName() == "Rf_dimgets") return true;  // TODO: not always true, conversion may happen
  if (f->getName() == "Rf_classgets") return true; // not completely true - not true when of length zero
  if (f->getName() == "SET_ATTRIB") return true;
  if (f->getName() == "SET_STRING_ELT") return true;
  if (f->getName() == "SET_VECTOR_ELT") return true;
  if (f->getName() == "SET_TAG") return true;
  if (f->getName() == "SETCAR") return true;
  if (f->getName() == "SETCDR") return true;
  if (f->getName() == "SETCADR") return true;
  if (f->getName() == "SETCADDR") return true;
  if (f->getName() == "SETCADDDR") return true;  
  if (f->getName() == "SETCAD4R") return true;
  if (f->getName() == "SET_FORMALS") return true;
  if (f->getName() == "SET_BODY") return true;
  if (f->getName() == "SET_CLOENV") return true;
  if (f->getName() == "R_set_altrep_data1") return true;
  if (f->getName() == "R_set_altrep_data2") return true;
  return false;
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
    // these functions test the type field and nothing more
    //   there are more complicated type tests, too, like isInteger or isVector, etc
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

void myassert_fail (const char *assertion, const char *file, unsigned int line, const char *function) {
  errs() << "RCHK assertion failed: " << assertion << ", in function " << function << " at " << file << ":" << line << "\n";
  abort();
}