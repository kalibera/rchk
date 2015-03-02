
#include "common.h"

#include <cxxabi.h>

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
    functionsOfInterest.insert(base->getFunction(f->getName()));
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

std::string instructionAsString(Instruction *in) {
  std::string str;
  raw_string_ostream os(str);
  os << *in;
  return str;
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
