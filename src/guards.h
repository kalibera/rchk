#ifndef RCHK_GUARDS_H
#define RCHK_GUARDS_H

#include "common.h"
#include "callocators.h"
#include "linemsg.h"
#include "state.h"
#include "symbols.h"
#include "table.h"

#include <map>
#include <unordered_set>

#include <llvm/IR/Instruction.h>

using namespace llvm;

// integer variable used as a guard

enum IntGuardState {
  IGS_ZERO = 0,
  IGS_NONZERO,
  IGS_UNKNOWN
};
const unsigned IGS_BITS = 2;

typedef std::map<AllocaInst*,IntGuardState> IntGuardsTy;

struct PackedIntGuardsTy {

  typedef std::vector<bool> BitsTy;
  BitsTy bits;
  
  PackedIntGuardsTy(unsigned nvars) : bits(nvars * IGS_BITS) {};
  bool operator==(const PackedIntGuardsTy& other) const { return bits == other.bits; };
};

struct StateWithGuardsTy;

std::string igs_name(IntGuardState igs);
IntGuardState getIntGuardState(IntGuardsTy& intGuards, AllocaInst* var);

// per-function state for checking SEXP guards
class IntGuardsChecker {

  struct IntGuardsTy_hash {
    size_t operator()(const IntGuardsTy& t) const;
  };

  typedef std::unordered_map<IntGuardsTy, PackedIntGuardsTy, IntGuardsTy_hash> IntGuardsTableTy;
  typedef IndexedTable<AllocaInst> VarIndexTy;

  IntGuardsTableTy igTable;
  VarIndexTy varIndex;
  VarBoolCacheTy varsCache; // FIXME: could eagerly search all variables and merge var cache with index
  LineMessenger* msg;

  public:
    IntGuardsChecker(LineMessenger* msg): igTable(), varIndex(), varsCache(), msg(msg) {};

    PackedIntGuardsTy pack(const IntGuardsTy& intGuards);
    IntGuardsTy unpack(const PackedIntGuardsTy& intGuards);
    void hash(size_t& res, const IntGuardsTy& intGuards);

    bool isGuard(AllocaInst* var);
    void handleForNonTerminator(Instruction* in, IntGuardsTy& intGuards);
    bool handleForTerminator(TerminatorInst* t, StateWithGuardsTy& s);

    void reset(Function *f) {};    
    void clear() { igTable.clear(); varsCache.clear(); } // FIXME: get rid of this
};


// SEXP - an "R pointer" used as a guard

enum SEXPGuardState {
  SGS_NIL = 0, // R_NilValue
  SGS_SYMBOL,  // A specific symbol, name stored in symbolName
  SGS_NONNIL,
  SGS_UNKNOWN
};

struct SEXPGuardTy {
  SEXPGuardState state;
  std::string symbolName;
  
  SEXPGuardTy(SEXPGuardState state, const std::string& symbolName): state(state), symbolName(symbolName) {}
  SEXPGuardTy(SEXPGuardState state): state(state), symbolName() { assert(state != SGS_SYMBOL); }
  SEXPGuardTy() : SEXPGuardTy(SGS_UNKNOWN) {};
  
  bool operator==(const SEXPGuardTy& other) const { return state == other.state && symbolName == other.symbolName; };
  
};

typedef std::map<AllocaInst*,SEXPGuardTy> SEXPGuardsTy;

struct PackedSEXPGuardsTy {
  const SEXPGuardsTy* sexpGuards; // pointer to intern table
  
  PackedSEXPGuardsTy(const SEXPGuardsTy* sexpGuards): sexpGuards(sexpGuards) {};
  bool operator==(const PackedSEXPGuardsTy& other) const { return sexpGuards == other.sexpGuards; };
};

// per-function state for checking SEXP guards
struct SEXPGuardsCheckerTy {

  struct SEXPGuardsTy_hash {
    size_t operator()(const SEXPGuardsTy& t) const;
  };

  typedef InterningTable<SEXPGuardsTy, SEXPGuardsTy_hash> SEXPGuardsTableTy;
  SEXPGuardsTableTy sgTable;

  SEXPGuardsCheckerTy() : sgTable() {};

  PackedSEXPGuardsTy pack(const SEXPGuardsTy& sexpGuards);
  SEXPGuardsTy unpack(const PackedSEXPGuardsTy& sexpGuards);
  void hash(size_t& res, const SEXPGuardsTy& sexpGuards);
  
  void reset(Function *f) {};
  void clear() { sgTable.clear(); };
  
};

  // yikes, need forward type-def
struct ArgInfoTy;
typedef std::vector<const ArgInfoTy*> ArgInfosVectorTy;

std::string sgs_name(SEXPGuardState sgs);
SEXPGuardState getSEXPGuardState(SEXPGuardsTy& sexpGuards, AllocaInst* var, std::string& symbolName);
bool isSEXPGuardVariable(AllocaInst* var, GlobalsTy* g, Function* isNullFunction);
bool isSEXPGuardVariable(AllocaInst* var, GlobalsTy* g, Function* isNullFunction, VarBoolCacheTy& cache);
void handleSEXPGuardsForNonTerminator(Instruction* in, VarBoolCacheTy& sexpGuardVarsCache, SEXPGuardsTy& sexpGuards,
  GlobalsTy *g, const ArgInfosVectorTy* argInfos, SymbolsMapTy* symbolsMap, LineMessenger& msg, FunctionsSetTy* possibleAllocators);
bool handleSEXPGuardsForTerminator(TerminatorInst* t, VarBoolCacheTy& sexpGuardVarsCache, StateWithGuardsTy& s, 
  GlobalsTy *g, const ArgInfosVectorTy* argInfos, SymbolsMapTy* symbolsMap, LineMessenger& msg);

// checking state with guards

struct StateWithGuardsTy : virtual public StateBaseTy {
  IntGuardsTy intGuards;
  SEXPGuardsTy sexpGuards;
  
  StateWithGuardsTy(BasicBlock *bb, const IntGuardsTy& intGuards, const SEXPGuardsTy& sexpGuards): StateBaseTy(bb), intGuards(intGuards), sexpGuards(sexpGuards) {};
  StateWithGuardsTy(BasicBlock *bb): StateBaseTy(bb), intGuards(), sexpGuards() {};
  
  virtual StateWithGuardsTy* clone(BasicBlock *newBB) = 0;
  
  void dump(bool verbose);
};

struct PackedStateWithGuardsTy : virtual public PackedStateBaseTy {
  const PackedIntGuardsTy intGuards;
  const PackedSEXPGuardsTy sexpGuards;
  
  PackedStateWithGuardsTy(BasicBlock *bb, const PackedIntGuardsTy& intGuards, const PackedSEXPGuardsTy& sexpGuards):
    PackedStateBaseTy(bb), intGuards(intGuards), sexpGuards(sexpGuards) {};
};

#endif
