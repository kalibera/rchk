
#include "liveness.h"
#include "table.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/InstIterator.h>

using namespace llvm;

typedef IndexedTable<AllocaInst> VarIndexTy;

static VarIndexTy indexVariables(Function *f) {

  VarIndexTy varIndex;
  
  for(inst_iterator ii = inst_begin(*f), ie = inst_end(*f); ii != ie; ++ii) {
    Instruction *in = &*ii;
    if (AllocaInst* var = dyn_cast<AllocaInst>(in)) {
      varIndex.indexOf(var);
    }
  }
  
  return varIndex;
}
typedef std::vector<bool> VarMapTy;

struct BlockStateTy {
  VarMapTy usedAfter;
  VarMapTy killedAfter;
  
  BlockStateTy(VarMapTy usedAfter, VarMapTy killedAfter): usedAfter(usedAfter), killedAfter(killedAfter) {};
};

typedef std::unordered_map<BasicBlock*, BlockStateTy> BlockStatesTy;
typedef std::unordered_set<BasicBlock*> BlockSetTy;

static void applyInstruction(Instruction *in, VarMapTy& used, VarMapTy& killed, VarIndexTy& varIndex) {

  if (StoreInst* si = dyn_cast<StoreInst>(in)) {
    if (AllocaInst* var = dyn_cast<AllocaInst>(si->getPointerOperand())) { // variable is killed
      unsigned vi = varIndex.indexOf(var);
      used[vi] = false;
      killed[vi] = true;
    }
  }
  if (LoadInst* li = dyn_cast<LoadInst>(in)) {
    if (AllocaInst* var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // variable is used
      unsigned vi = varIndex.indexOf(var);
      used[vi] = true;
      killed[vi] = false;
    }
  }
}

LiveVarsTy findLiveVariables(Function *f) {

  VarIndexTy varIndex = indexVariables(f);
  size_t nvars = varIndex.size();
  
  BlockStatesTy blockStates;
  BlockSetTy changed;
  
  // add basic blocks with return statement
  for(Function::iterator bi = f->begin(), be = f->end(); bi != be; ++bi) {
    BasicBlock *bb = bi;
    
    // note: ignoring "error blocks" (unreachable terminators)
    if (ReturnInst *ri = dyn_cast<ReturnInst>(bb->getTerminator())) {
      VarMapTy usedAfter = VarMapTy(nvars, false);
      VarMapTy killedAfter = VarMapTy(nvars, true);
      blockStates.insert({bb, BlockStateTy(usedAfter, killedAfter)});
      changed.insert(bb);
    }
  }
  
  // find variables possibly used/killed after each block
  while(!changed.empty()) {
    BlockSetTy::iterator bi = changed.begin();
    BasicBlock* bb = *bi;
    changed.erase(bi);
    
    auto bsearch = blockStates.find(bb);
    assert(bsearch != blockStates.end());
    BlockStateTy& s = bsearch->second;
    
    VarMapTy used = s.usedAfter; // copy
    VarMapTy killed = s.killedAfter; // copy
    
    // compute variables live at block start
    for(BasicBlock::reverse_iterator ii = bb->rbegin(), ie = bb->rend();  ii != ie; ++ii) {
      Instruction *in = &*ii;
      applyInstruction(in, used, killed, varIndex);     
    }
    
    // merge into block predecessors
    for(pred_iterator pi = pred_begin(bb), pe = pred_end(bb); pi != pe; ++pi) {
      BasicBlock* pb = *pi;

      auto bsearch = blockStates.find(pb);
      if (bsearch == blockStates.end()) {
        blockStates.insert({pb, BlockStateTy(used, killed)});
        changed.insert(pb);
      } else {
        BlockStateTy& ps = bsearch->second;
        VarMapTy& prevUsed = ps.usedAfter;
        VarMapTy& prevKilled = ps.killedAfter;
        
        for(unsigned vi = 0; vi < nvars; vi++) {
          bool change = false;
          if (used[vi] && !prevUsed[vi]) { // union of used variables
            prevUsed[vi] = true;
            change = true;
          }
          if (killed[vi] && !prevKilled[vi]) { // union of killed variables
            prevKilled[vi] = true;
            change = true;
          }          
          if (change) {
            changed.insert(pb);
          }
        }
      }
    }
  }
  
  // convert results, and compute for each instruction
  LiveVarsTy live;
  
  for(BlockStatesTy::iterator si = blockStates.begin(), se = blockStates.end(); si != se; ++si) {
    BasicBlock* bb = si->first;
    BlockStateTy& s = si->second;
    VarMapTy used = s.usedAfter;
    VarMapTy killed = s.killedAfter;
    
    for(BasicBlock::reverse_iterator ii = bb->rbegin(), ie = bb->rend();  ii != ie; ++ii) {
      Instruction *in = &*ii;
      
      // record used vars for instruction
      //   (result liveness info is relevant "after this instruction executes")
      
      VarsLiveness vars;
      for(unsigned vi = 0; vi < nvars; vi++) {
        if (used[vi]) {
          vars.possiblyUsed.insert(varIndex.at(vi));
        }
        if (killed[vi]) {
          vars.possiblyKilled.insert(varIndex.at(vi));
        }
      }
      live.insert({in, vars});
      
      applyInstruction(in, used, killed, varIndex);
    }
  }
  return live;
}
