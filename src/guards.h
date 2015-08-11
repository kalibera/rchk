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

// per-function state for checking SEXP guards
class IntGuardsChecker {

  typedef IndexedTable<AllocaInst> VarIndexTy; // index of guard variables known so far

  VarIndexTy varIndex;
  VarBoolCacheTy varsCache; // FIXME: could eagerly search all variables and merge var cache with index
  LineMessenger* msg;

  public:
    IntGuardsChecker(LineMessenger* msg): varIndex(), varsCache(), msg(msg) {};

    PackedIntGuardsTy pack(const IntGuardsTy& intGuards);
    IntGuardsTy unpack(const PackedIntGuardsTy& intGuards);
    void hash(size_t& res, const IntGuardsTy& intGuards);

    bool isGuard(AllocaInst* var);
    void handleForNonTerminator(Instruction* in, IntGuardsTy& intGuards);
    bool handleForTerminator(TerminatorInst* t, StateWithGuardsTy& s);
    
    IntGuardState getGuardState(const IntGuardsTy& intGuards, AllocaInst* var);

    void reset(Function *f) {};    
    void clear() { varsCache.clear(); } // FIXME: get rid of this
};


// SEXP - an "R pointer" used as a guard

enum SEXPGuardState {
  SGS_NIL = 0, // R_NilValue
  SGS_SYMBOL,  // A specific symbol, name stored in symbolName
  SGS_VECTOR,  // Anything that LENGTH can be called on (includes numeric vectors, generic vectors, but not things implemented as pair-lists) 
  SGS_NONNIL,
  SGS_UNKNOWN
};
const unsigned SGS_BITS = 3;

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

  typedef std::vector<bool> BitsTy;
  BitsTy bits;
  
  typedef std::vector<std::string> SymbolsTy;
  SymbolsTy symbols;
  
  PackedSEXPGuardsTy(unsigned nvars) : bits(nvars * SGS_BITS), symbols() {};
  bool operator==(const PackedSEXPGuardsTy& other) const { return bits == other.bits && symbols == other.symbols; };
};

  // yikes, need forward type-def
struct ArgInfoTy;
typedef std::vector<const ArgInfoTy*> ArgInfosVectorTy;

// per-function state for checking SEXP guards
class SEXPGuardsChecker {

  typedef IndexedTable<AllocaInst> VarIndexTy; // index of guard variables known so far

  VarIndexTy varIndex;
  VarBoolCacheTy varsCache; // FIXME: could eagerly search all variables and merge var cache with index
  LineMessenger* msg;
  const GlobalsTy* g;
  const FunctionsSetTy* possibleAllocators;
  const SymbolsMapTy* symbolsMap;
  const ArgInfosVectorTy* argInfos;
  
  public:
    SEXPGuardsChecker(LineMessenger* msg, const GlobalsTy* g, const FunctionsSetTy* possibleAllocators, const SymbolsMapTy* symbolsMap, const ArgInfosVectorTy* argInfos):
      varIndex(), varsCache(), msg(msg), g(g), possibleAllocators(possibleAllocators), symbolsMap(symbolsMap), argInfos(argInfos) {};

    PackedSEXPGuardsTy pack(const SEXPGuardsTy& sexpGuards);
    SEXPGuardsTy unpack(const PackedSEXPGuardsTy& sexpGuards);
    void hash(size_t& res, const SEXPGuardsTy& sexpGuards);

    bool isGuard(AllocaInst* var);
    void handleForNonTerminator(Instruction* in, SEXPGuardsTy& sexpGuards);
    bool handleForTerminator(TerminatorInst* t, StateWithGuardsTy& s);
    
    SEXPGuardState getGuardState(const SEXPGuardsTy& sexpGuards, AllocaInst* var);
    SEXPGuardState getGuardState(const SEXPGuardsTy& sexpGuards, AllocaInst* var, std::string& symbolName);

    void reset(Function *f) {};    
    void clear() { varsCache.clear(); } // FIXME: get rid of this
    
  private:
    bool uncachedIsGuard(AllocaInst* var);
    bool handleNullCheck(bool positive, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s);
    bool handleTypeCheck(bool positive, unsigned testedType, SEXPGuardState gs, AllocaInst *guard, BranchInst* branch, StateWithGuardsTy& s);
};

std::string sgs_name(SEXPGuardState sgs);

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
