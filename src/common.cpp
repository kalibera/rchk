
#include "common.h"

#include <vector>

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/Demangle/Demangle.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
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
  
  
  if (Linker::linkModules(*base, std::move(module))) {
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
  return llvm::demangle(f->getName().str());
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

    for (DbgVariableRecord dvr : filterDbgVars(in->getDbgRecordRange())) {
      if (dvr.getAddress() == var) {
        return dvr.getVariable()->getName().str();
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

// strips const, volatile, restrict, atomic, immutable and reference qualifiers
// does not strip typedef, pointer and member
DIType *stripQualifiers(DIType *type) {
  if (!type) return nullptr;
  
  if (auto *derivedType = dyn_cast<DIDerivedType>(type)){
    switch (derivedType->getTag()) {
      case dwarf::DW_TAG_reference_type:
      case dwarf::DW_TAG_const_type:
      case dwarf::DW_TAG_volatile_type:
      case dwarf::DW_TAG_restrict_type:
      case dwarf::DW_TAG_atomic_type:
      case dwarf::DW_TAG_immutable_type:
        return stripQualifiers(derivedType->getBaseType());
      default:
        return type;
    }
  }
  return type;
}

// SEXP is defined via typedef struct SEXPREC *SEXP;
bool isSEXP(DIType *type) {
  if (!type) return false;
  type = stripQualifiers(type);
  if (!type) return false;

  // check typedef SEXP
  if (auto *derivedType = dyn_cast<DIDerivedType>(type)){
    if (derivedType->getTag() != dwarf::DW_TAG_typedef) return false;
    if (derivedType->getName() != "SEXP") return false;

    type = stripQualifiers(derivedType->getBaseType());
    if (!type) return false;
  }
  else return false;

  // check pointer
  if (auto *derivedType = dyn_cast<DIDerivedType>(type)){
    if (derivedType->getTag() != dwarf::DW_TAG_pointer_type) return false;
    type = stripQualifiers(derivedType->getBaseType());
    if (!type) return false;
  }
  else return false;

  // check SEXPREC
  if (auto *compositeType = dyn_cast<DICompositeType>(type)) {
    if (compositeType->getTag() == dwarf::DW_TAG_structure_type) {
      // check for typedef struct SEXPREC and for struct SEXPREC, as both are used in R sources
      if (compositeType->getName() == "SEXPREC" || compositeType->getName() == "struct SEXPREC") {
        return true;
      }
    }
  }
  return false;
}

// Check if type is a struct with exactly one SEXP member
// as Clang lowers struct { SEXPREC* } to SEXPREC*
bool isSEXPWrapper(DIType *type) {
  if (!type) return false;
  type = stripQualifiers(type);
  if (!type) return false;

  if (auto *derived = dyn_cast<DIDerivedType>(type)) {
    if (derived->getTag() == dwarf::DW_TAG_typedef)
      type = stripQualifiers(derived->getBaseType());
  }
  if (!type) return false;

  if (auto *composite = dyn_cast<DICompositeType>(type)) {
    if (composite->getTag() != dwarf::DW_TAG_structure_type) return false;
    auto elements = composite->getElements();
    if (elements.size() != 1) return false;

    if (auto *type = dyn_cast<DIDerivedType>(elements[0])) {
      if (type->getTag() == dwarf::DW_TAG_member)
        return isSEXP(type->getBaseType());
    }
  }

  return false;
}

bool isPointeeStruct(Type* pointee, std::string name) {
  if (auto *estr = dyn_cast<StructType>(pointee)) {
    return estr->hasName() && estr->getName().str() == name;
  }
  return false;
}

// check if GEP source is SEXP
bool isGEPSourceSEXP(GetElementPtrInst* gep) {
  if (!gep) return false;

  if (isPointeeStruct(gep->getSourceElementType(), "struct.SEXPREC")) {
    return true;
  }
  return false;
}

// check type array in DI on index functionTypeIndex: index 0 is return type, argument i has index i+1
bool isFunctionSEXP(Function *fun, int functionTypeIndex) {
  if (!fun) return false;

  if (DISubprogram *sp = fun->getSubprogram()) {
    DISubroutineType *srt = sp->getType();
    if (functionTypeIndex < 0 || functionTypeIndex>= srt->getTypeArray().size()) {
      return false;
    }
    auto *functionType = srt->getTypeArray()[functionTypeIndex];

    // type may be wrapped due to ABI lowering
    if (isSEXP(functionType) || isSEXPWrapper(functionType)) {
      return true;
    }
  }
  return false;
}

bool isFunctionRetSEXP(Function *fun) {
  if (!fun) return false;
  return isFunctionSEXP(fun, 0); // 0 is the return type index
}

// finds IR argument corresponding to the DI variable
Argument* getArgFromDebugVar(DbgVariableRecord *DVR) {
  if (!DVR) return nullptr;

  // first try the direct case: argument directly in DVI
  auto *arg = dyn_cast<Argument>(DVR->getVariableLocationOp(0));
  if (arg) return arg;

  // if not there are two ways
  if (auto *alloca = dyn_cast<AllocaInst>(DVR->getVariableLocationOp(0))) {
    for (auto *user : alloca->users()) {
      // direct: alloca -> store (for non-coerced structs)
      if (auto *store = dyn_cast<StoreInst>(user)) {
        arg = dyn_cast<Argument>(store->getValueOperand());
        if (arg) break;
      }
      // via GEP: alloca -> GEP -> store (for coerced structs)
      if (auto *gep = dyn_cast<GetElementPtrInst>(user)) {
        for (auto *gepUser : gep->users()) {
            if (auto *store = dyn_cast<StoreInst>(gepUser)) {
                arg = dyn_cast<Argument>(store->getValueOperand());
                if (arg) break;
            }
        }
        if (arg) break;
      }
    }
  }
  return arg;
}

// build map from IR function type index to DI type index for given function
typedef std::map<unsigned, unsigned> IRToDIIndexMapTy;
IRToDIIndexMapTy buildIRToDIIndexMap(Function *fun) {
  IRToDIIndexMapTy map;

  // iterate over instructions in function
  for (auto &inst : instructions(fun)) {

    // look for DbgVariableRecord, which has variable and type info
    for (DbgVariableRecord DVR : filterDbgVars(inst.getDbgRecordRange())) {      
      auto *var = DVR.getVariable();
      if (!var) continue;
      
      unsigned DIArgIndex = var->getArg();
      if (DIArgIndex == 0) continue; // DIArgIndex == 0 means return type or local variable, we are only interested in arguments
      
      Argument *arg = getArgFromDebugVar(&DVR);
      if (!arg) continue;
      
      map[arg->getArgNo()] = DIArgIndex;
    }
  }
  return map;
}

// expect argIndex in IR numbering, which includes return type when sret present
bool isFunctionArgSEXP(Function *fun, int argIndex) {
  if (!fun) return false;
  if (argIndex < 0 || argIndex >= fun->arg_size()) {
    return false;
  }

  IRToDIIndexMapTy map = buildIRToDIIndexMap(fun);
  auto search = map.find(argIndex);
  if (search != map.end()) {
    unsigned DIArgIndex = search->second;
    return isFunctionSEXP(fun, DIArgIndex);
  }

  return false;
}

bool isSEXP(Argument *arg) {
  if (!arg) return false;

  auto *fun = arg->getParent();
  auto argIndex = arg->getArgNo();

  return isFunctionArgSEXP(fun, argIndex);
}

bool isSEXP(GlobalVariable *gv) {
  if (!gv) return false;

  SmallVector<DIGlobalVariableExpression *> debugInfoVector;
  gv->getDebugInfo(debugInfoVector);

  for (auto debugInfo : debugInfoVector) {
    if (!debugInfo) continue;
    DIGlobalVariable *variable = debugInfo->getVariable();

    if (!variable) continue;
    DIType *type = variable->getType();
    
    if (isSEXP(type)) {
      return true;
    }
  }
  return false;
}

bool TRVS_DBG = false;
typedef std::pair<Value*, int> ValueDepthPair;
// checks whether value can be traced to SEXP
// depth 0 checks for SEXP, depth 1 checks for SEXP*, etc.
bool traverseToSEXP(Value* start, int depth) {
  if (!start) return false;

  static std::map<ValueDepthPair, bool> cache;
  auto search = cache.find({start, depth});
  if (search != cache.end()) {
    return search->second;
  }

  std::set<ValueDepthPair> visited;
  SmallVector<ValueDepthPair, 32> toVisit = { {start, depth} };
  bool result = false;
  bool ended = false;

  if (TRVS_DBG) errs() << "\nStarting traverseToSEXP from value: " << *start << " with depth " << depth << "\n";
  while (!toVisit.empty()) {
    auto pair = toVisit.pop_back_val();
    Value *curr = pair.first;
    int currDepth = pair.second;

    // prune non-pointer values
    if (!curr->getType()->isPointerTy()) continue;

    // check global cache
    auto cached = cache.find({curr, currDepth});
    if (cached != cache.end() && cached->second) {
      if (TRVS_DBG) errs() << "Found SEXP source in cache: " << *curr << "\n";
      result = true;
      ended = true;
      break;
    }

    // check local visited set
    if (!visited.insert({curr, currDepth}).second || currDepth < 0) {
      continue;
    }
    if (TRVS_DBG) errs() << "Visiting value: " << *curr << " with depth " << currDepth << "\n";

    if (!curr->hasUseList()) continue; // ConstantData asserts this when iterating over uses
    for (Use &use : curr->uses()) {
      User *user = use.getUser();

      if (auto *gep = dyn_cast<GetElementPtrInst>(user)){
        if (currDepth == 0 && gep->getPointerOperand() == curr && isGEPSourceSEXP(gep)) {
          if (TRVS_DBG) errs() << "Found SEXP source in GEP: " << *gep << "\n";
          result = true;
          ended = true;
          break;
        }
      }
      else if (auto *ret = dyn_cast<ReturnInst>(user)) {
        if (currDepth == 0 && isFunctionRetSEXP(ret->getFunction())) {
          if (TRVS_DBG) errs() << "Found SEXP return in function: " << funName(ret->getFunction()) << "\n";
          result = true;
          ended = true;
          break;
        }
      }
      else if (auto *ci = dyn_cast<CallInst>(user)) {
        if (ci->isArgOperand(&use)) {
          int argIndex = ci->getArgOperandNo(&use);
          if (currDepth == 0 && isFunctionArgSEXP(ci->getCalledFunction(), argIndex)) {
            if (TRVS_DBG) errs() << "Found SEXP argument in call: " << *ci << "\n";
            result = true;
            ended = true;
            break;
          }
        }
      }
      else if (auto *phi = dyn_cast<PHINode>(user)) toVisit.push_back({phi, currDepth});
      else if (auto *sel = dyn_cast<SelectInst>(user)) toVisit.push_back({sel, currDepth});
      else if (auto *li = dyn_cast<LoadInst>(user)) {
        // LoadInst dereferences pointer
        if (currDepth > 0) toVisit.push_back({li, currDepth - 1});
      }
      else if (auto *si = dyn_cast<StoreInst>(user)) {
        // curr is being stored into some pointer — follow that pointer at depth+1
        if (si->getValueOperand() == curr){
          toVisit.push_back({si->getPointerOperand(), currDepth + 1});
        }
        // curr is a pointer being stored into — follow the value at depth-1
        else if (si->getPointerOperand() == curr && currDepth > 0){
          toVisit.push_back({si->getValueOperand(), currDepth - 1});
        }
      }
      else {
        if (TRVS_DBG) {
          errs() << "Unhandled instruction type in traverseToSEXP: ";
          user->print(errs());
          errs() << "\n";
        }
      }
    }
    if (ended) break;
  }
  cache[{start, depth}] = result;
  return result;
}

bool isSEXP(AllocaInst* ai) {
  if (!ai) return false;
  if (ai->isArrayAllocation() /* need to check this? */) {
    return false;
  }

  SmallVector<DbgVariableRecord *, 32> variablesVector;
  findDbgUsers(ai, variablesVector); // with -O0 returns only dbg.declare

  for (auto *variable : variablesVector) {
    if (!variable) continue;
    DILocalVariable *localVariable = variable->getVariable();

    if (!localVariable) continue;
    DIType *type = localVariable->getType();

    if (isSEXP(type)) {
      return true;
    }
  }
  // alloca is pointer to allocated type -> check for SEXP* -> set depth to 1
  return traverseToSEXP(ai, 1);
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
    outs().flush();
    errs() << "  Function " << name << " not found in module (won't check its use).\n";
  }
  return f;
}
    
GlobalVariable* GlobalsTy::getSpecialVariable(Module *m, std::string name) {
  GlobalVariable *v = m->getGlobalVariable(name, true);
  if (!v) {
    outs().flush();
    errs() << "  Variable " << name << " not found in module (won't check its use).\n";
  }
  return v;
}

void myassert_fail (const char *assertion, const char *file, unsigned int line, const char *function) {
  errs() << "RCHK assertion failed: " << assertion << ", in function " << function << " at " << file << ":" << line << "\n";
  abort();
}