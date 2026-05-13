#include "cache.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/MD5.h>

#include <optional>
#include <sstream>

// Helper to compute the MD5 hash of the given string. (inspired by LLVM's MD5Hash)
inline StringRef computeMD5Hash(StringRef Str) {

  MD5 hash;
  hash.update(Str);
  MD5::MD5Result result;
  hash.final(result);
  return result.digest().str();
}

std::string CAllocatorCacheTy::functionFingerprint(const Function *f) {
  if (!f) {
    return "";
  }

  std::string location = funLocation(f);
  return computeMD5Hash(location).str();
}

bool CAllocatorCacheTy::splitEncodedFunction(StringRef encoded, StringRef &name, StringRef &fingerprint) {
  SmallVector<StringRef, 2> parts;
  encoded.split(parts, functionFingerprintDelimiter);
  if (parts.size() != 2) {
    return false;
  }

  name = parts[0];
  fingerprint = parts[1];
  return true;
}

// Encoding

std::string CAllocatorCacheTy::encodeFunction(const Function* f) {
  if (!f) {
    myassert("Cannot encode NULL function");
    return "";
  }

  std::string encoded = f->getName().str();
  std::string fingerprint = functionFingerprint(f);

  if (fingerprint.empty()) return encoded;
  return encoded + functionFingerprintDelimiter + fingerprint;
}

std::string CAllocatorCacheTy::encodeArgInfos(const ArgInfosVectorTy* argInfos) {
  if (!argInfos) {
    myassert("Cannot encode NULL argInfos");
    return "";
  }
  std::ostringstream oss;
  for (size_t i = 0; i < argInfos->size(); ++i) {
    if (i > 0) oss << argDelimiter;
    const ArgInfoTy* a = (*argInfos)[i];
    if (a && a->isSymbol()) {
      oss << "S:" << static_cast<const SymbolArgInfoTy*>(a)->symbolName;
    } else if (a && a->isVector()) {
      oss << "V";
    } else {
      oss << "?";
    }
  }
  return oss.str();
}

std::string CAllocatorCacheTy::encodeCalledFunction(const CalledFunctionTy *cf) {
  std::string result = encodeFunction(cf->fun);
  std::string args = encodeArgInfos(cf->argInfo);
  if (!args.empty()) {
    result += argDelimiter;
    result += args;
  }
  return result;
}

std::string CAllocatorCacheTy::encodeCallInst(const Value *callSite) {
  auto *cb = dyn_cast<CallBase>(callSite);
  if (!cb) {
    myassert("CallSite should be a CallBase");
    return "";
  }

  const Function *f = cb->getParent()->getParent();
  const BasicBlock *parent = cb->getParent();
  unsigned bbIdx = 0, instIdx = 0;

  for (auto &bb : *f) {
    if (&bb == parent) break;
    bbIdx++;
  }
  for (auto &i : *parent) {
    if (&i == cb) break;
    instIdx++;
  }

  return encodeFunction(f) + callInstDelimiter
       + std::to_string(bbIdx) + callInstDelimiter
       + std::to_string(instIdx);
}

// Serialization

bool CAllocatorCacheTy::serialize(CalledModuleTy *cm) {
  std::ofstream out(file);
  if (!out) {
    errs() << "[CACHE] Error opening cache file for writing: " << file << "\n";
    return false;
  }

  out << "VERSION" << delimiter << formatVersion << "\n";

  const CalledModuleTy::CAllocatorSeedDataTy *seed = cm->getCAllocatorSeedData(); // triggers computeCalledAllocators
  if (!seed) {
    errs() << "[CACHE] Missing seed data for serialization\n";
    return false;
  }

  for (const CalledFunctionTy* cf : seed->getDoneCallers()) {
    out << "DONE" << delimiter << encodeCalledFunction(cf) << "\n";
  }
  for (const auto& e : seed->getCallEdges()) {
    out << "CALLS" << delimiter << encodeCalledFunction(e.first) << delimiter << encodeCalledFunction(e.second) << "\n";
  }
  for (const auto& e : seed->getWrapEdges()) {
    out << "WRAPS" << delimiter << encodeCalledFunction(e.first) << delimiter << encodeCalledFunction(e.second) << "\n";
  }

  for (const auto& [callInst, calledFunctionSet] : *cm->getCallSiteTargets()) {
    out << "CSTARGET" << delimiter << encodeCallInst(callInst);
    for (const CalledFunctionTy* cf : calledFunctionSet) {
      out << delimiter << encodeCalledFunction(cf);
    }
    out << "\n";
  }

  return true;
}

// Decoding

void CAllocatorCacheTy::buildFunctionFingerprintMap(Module* m) {
  if (indexedModule == m || !m) {
    return;
  }

  indexedModule = m;
  functionFingerprintMap.clear();

  for (Function &candidate : *m) {
    std::string fingerprint = functionFingerprint(&candidate);
    if (fingerprint.empty()) {
      continue;
    }
    functionFingerprintMap[fingerprint].push_back(&candidate);
  }
}

Function* CAllocatorCacheTy::decodeFunction(StringRef encoded, Module* m) {
  StringRef name;
  StringRef fingerprint;
  if (!splitEncodedFunction(encoded, name, fingerprint)) {
    errs() << "[CACHE] Invalid function encoding: " << encoded << "\n";
    return nullptr;
  }

  // try to find the function by name in the module first
  Function *fce = m->getFunction(name);
  if (fce && fingerprint == functionFingerprint(fce)) {
    return fce;
  }

  // if not found, try to find by fingerprint in the index
  buildFunctionFingerprintMap(m);
  auto fsearch = functionFingerprintMap.find(fingerprint.str());
  if (fsearch == functionFingerprintMap.end()) {
    return nullptr;
  }
  
  std::vector<Function*> candidates = fsearch->second;
  if (candidates.size() > 1) {
    errs() << "[CACHE] Ambiguous function fingerprint for " << encoded << "\n";
    return nullptr;
  }
  return candidates.front();
}

const ArgInfoTy* CAllocatorCacheTy::decodeArgInfo(StringRef encoded) {
  if (encoded.starts_with("S:")) {
    return SymbolArgInfoTy::create(encoded.substr(2).str());
  } else if (encoded == "V") {
    return VectorArgInfoTy::get();
  } else {
    return nullptr;
  }
}

bool CAllocatorCacheTy::decodeCalledFunction(StringRef encoded, Module* m, Function*& outFn, ArgInfosVectorTy& outArgInfos) {
  // encoded = "funcName,arg1,arg2,...,argN"
  SmallVector<StringRef, 16> parts;
  encoded.split(parts, argDelimiter);
  if (parts.empty()) return false;

  outFn = decodeFunction(parts[0], m);
  if (!outFn) {
    errs() << "[CACHE] Function not found: " << parts[0] << "\n";
    return false;
  }

  // args are parts[1..N], skip empty trailing parts
  for (size_t i = 1; i < parts.size(); ++i) {
    StringRef part = parts[i];
    if (part.empty()) continue;
    outArgInfos.push_back(decodeArgInfo(part));
  }
  return true;
}


Value* CAllocatorCacheTy::decodeCallInst(StringRef encoded, Module* m) {
  // encoded = "funcName:bbIdx:instIdx"
  SmallVector<StringRef, 4> parts;
  encoded.split(parts, callInstDelimiter);
  if (parts.size() != 3) {
    errs() << "[CACHE] Invalid call instruction encoding: " << encoded << "\n";
    return nullptr;
  }

  Function* f = decodeFunction(parts[0], m);
  if (!f) {
    errs() << "[CACHE] Function not found for call inst: " << parts[0] << "\n";
    return nullptr;
  }

  unsigned bbIdx, instIdx;
  if (parts[1].getAsInteger(10, bbIdx) || parts[2].getAsInteger(10, instIdx)) {
    errs() << "[CACHE] Invalid indices in call inst: " << encoded << "\n";
    return nullptr;
  }

  // Navigate to the instruction via basic block
  unsigned curBB = 0;
  for (auto &bb : *f) {
    if (curBB == bbIdx) {
      unsigned curInst = 0;
      for (auto &inst : bb) {
        if (curInst == instIdx) return &inst;
        curInst++;
      }
      errs() << "[CACHE] Instruction index " << instIdx << " out of range in " << parts[0] << "\n";
      return nullptr;
    }
    curBB++;
  }

  errs() << "[CACHE] Basic block index " << bbIdx << " out of range in " << parts[0] << "\n";
  return nullptr;
}

// Deserialization

bool CAllocatorCacheTy::deserialize(CalledModuleTy *cm) {
  std::ifstream in(file);
  if (!in) {
    errs() << "ERROR: Cache file " << file << " failed to load or does not exist.\n";
    return false;
  }

  CalledModuleTy::CAllocatorSeedDataTy *seed = new CalledModuleTy::CAllocatorSeedDataTy(cm);
  std::optional<int> version;

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;

    SmallVector<StringRef, 16> parts;
    StringRef(line).split(parts, delimiter);

    if (parts.size() < 2) {
      errs() << "[CACHE] Invalid cache line: " << line << "\n";
      continue;
    }

    StringRef type = parts[0];

    if (!version) {
      // read version from the first line or return error
      if (type != "VERSION" || parts.size() != 2) {
        errs() << "[CACHE] Unsupported cache format in " << file << ", please regenerate the cache.\n";
        delete seed;
        return false;
      }

      int v = 0;
      if (parts[1].getAsInteger(10, v) || v != formatVersion) {
        errs() << "[CACHE] Unsupported cache version in " << file << ", please regenerate the cache. Current version is " << formatVersion << "\n";
        delete seed;
        return false;
      }
      version = v;
      continue;
    }

    if (type == "DONE") {
      Function* callerFn;
      ArgInfosVectorTy callerArgs;
      if (!decodeCalledFunction(parts[1], cm->getModule(), callerFn, callerArgs)) continue;
      seed->addDone(callerFn, callerArgs);

    } else if (type == "CALLS") {
      if (parts.size() != 3) {
        errs() << "[CACHE] CALLS needs caller and callee: " << line << "\n";
        continue;
      }
      Function* callerFn;
      ArgInfosVectorTy callerArgs;
      Function* calleeFn;
      ArgInfosVectorTy calleeArgs;
      if (!decodeCalledFunction(parts[1], cm->getModule(), callerFn, callerArgs)) continue;
      if (!decodeCalledFunction(parts[2], cm->getModule(), calleeFn, calleeArgs)) continue;
      seed->addCallEdge(callerFn, callerArgs, calleeFn, calleeArgs);

    } else if (type == "WRAPS") {
      if (parts.size() != 3) {
        errs() << "[CACHE] WRAPS needs caller and callee: " << line << "\n";
        continue;
      }
      Function* callerFn;
      ArgInfosVectorTy callerArgs;
      Function* calleeFn;
      ArgInfosVectorTy calleeArgs;
      if (!decodeCalledFunction(parts[1], cm->getModule(), callerFn, callerArgs)) continue;
      if (!decodeCalledFunction(parts[2], cm->getModule(), calleeFn, calleeArgs)) continue;
      seed->addWrapEdge(callerFn, callerArgs, calleeFn, calleeArgs);

    } else if (type == "CSTARGET") {
      // CSTARGET;func:bbIdx:instIdx;calledFunc1,args...;calledFunc2,args...;...
      if (parts.size() < 3) {
        errs() << "[CACHE] CSTARGET needs at least a call site and one target: " << line << "\n";
        continue;
      }

      Value* callInst = decodeCallInst(parts[1], cm->getModule());
      if (!callInst) {
        errs() << "[CACHE] Failed to decode call instruction: " << parts[1] << "\n";
        continue;
      }

      // parts[2..N] are each an encoded CalledFunction
      for (size_t i = 2; i < parts.size(); ++i) {
        if (parts[i].empty()) continue;

        Function* targetFn;
        ArgInfosVectorTy argInfos;
        if (!decodeCalledFunction(parts[i], cm->getModule(), targetFn, argInfos)) continue;
        cm->addToCallSiteTarget(callInst, targetFn, argInfos);
      }

    } else {
      errs() << "[CACHE] Unknown cache entry type: " << type << "\n";
    }
  }

  cm->setCAllocatorSeedData(std::move(seed));
  return true;
}

// Cache generation

// Creates cache file for R (without any package)
bool createRCacheFile(std::string baseIRFile, std::string cacheName) {
  errs() << "Creating R cache file\n";

  const char* argv[] = {"cachegen", "--cache", cacheName.c_str(), baseIRFile.c_str()};
  int argc = sizeof(argv) / sizeof(argv[0]);

  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  std::string cacheFile;
  Module *m = parseArgsReadIR(argc, const_cast<char**>(argv), functionsOfInterestSet, functionsOfInterestVector, context, &cacheFile);

  CAllocatorCacheTy cache(cacheFile);
  if (!cache.writeable()) {
    errs() << "Failed to open cache file for writing: " << cacheFile << "\n";
    return false;
  }

  CalledModuleTy *original = CalledModuleTy::create(m);
  if (cache.serialize(original)) {
    errs() << "Cache file created: " << cacheFile << "\n";
  } else {
    errs() << "Failed to serialize cache to file: " << cacheFile << "\n";
    return false;
  }
  return true;
}
