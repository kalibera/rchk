#include "callocators.h"

#include <fstream>
#include <unordered_map>
#include <vector>

class CAllocatorCacheTy {
  const std::string file;
  // cache file format:
  // where <func> = <functionName>#<hash>;<arg1>;<arg2>...;<argN>
 
  // VERSION;<versionNumber>
  // DONE;<func>
  // CALLS;<func1>;<func2>
  // WRAPS;<func1>;<func2>
  // CSTARGET;<func>:<bbIdx>:<instIdx>;<func1>;<func2>;<func3>;...

  // NOTE:
  // Function identities are encoded as <functionName>#<hash>, where hash is
  // the MD5 digest of the function debug location to disambiguate collisions.

  static constexpr unsigned formatVersion = 3;
  
  static constexpr char delimiter = ';';
  static constexpr char argDelimiter = ',';
  static constexpr char callInstDelimiter = ':';
  static constexpr char functionFingerprintDelimiter = '#';

  // index of functions by fingerprint

  // function fingerprint is a hash of the function debug location
  // used to find the correct function when there are multiple with the same name
  // this happens when linker adds suffix to the function name to disambiguate

  using FunctionFingerprintMapTy = std::unordered_map<std::string, std::vector<Function*>>;

  Module *indexedModule = nullptr;
  FunctionFingerprintMapTy functionFingerprintMap;
  void buildFunctionFingerprintMap(Module* m);

  // encode helper functions

  std::string functionFingerprint(const Function* f);
  std::string encodeFunction(const Function* f);
  std::string encodeArgInfos(const ArgInfosVectorTy* argInfos);
  std::string encodeCalledFunction(const CalledFunctionTy *cf);
  std::string encodeCallInst(const Value *callInst);

  // decode helper functions

  bool splitEncodedFunction(StringRef encoded, StringRef& name, StringRef& fingerprint);
  Function* decodeFunction(StringRef encoded, Module* m);
  const ArgInfoTy* decodeArgInfo(StringRef encoded);
  bool decodeCalledFunction(StringRef encoded, Module* m, Function*& outFn, ArgInfosVectorTy& outArgInfos);
  Value* decodeCallInst(StringRef encoded, Module* m);

public:

  CAllocatorCacheTy(std::string file): file(file) {};
  
  // checks whether the cache file can be opened for writing
  bool writeable() const {
    std::ofstream out(file);
    return out.good();
  }
  bool serialize(CalledModuleTy *cm);
  bool deserialize(CalledModuleTy *cm);
};

bool createRCacheFile(std::string baseIRFile, std::string cacheName);
