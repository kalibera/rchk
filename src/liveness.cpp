
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
typedef std::unordered_map<BasicBlock*, VarMapTy> UsedAfterTy;
typedef std::unordered_set<BasicBlock*> BlockSetTy;

static void applyInstruction(Instruction *in, VarMapTy& used, VarIndexTy& varIndex) {

  if (StoreInst* si = dyn_cast<StoreInst>(in)) {
    if (AllocaInst* var = dyn_cast<AllocaInst>(si->getPointerOperand())) { // variable is killed
      unsigned vi = varIndex.indexOf(var);
      used[vi] = false;
    }
  }
  if (LoadInst* li = dyn_cast<LoadInst>(in)) {
    if (AllocaInst* var = dyn_cast<AllocaInst>(li->getPointerOperand())) { // variable is used
      unsigned vi = varIndex.indexOf(var);
      used[vi] = true;
    }
  }
}

LiveVarsTy findLiveVariables(Function *f) {

  VarIndexTy varIndex = indexVariables(f);
  size_t nvars = varIndex.size();
  
  UsedAfterTy usedAfter;
  BlockSetTy changed;
  
  // add basic blocks with return statement
  for(Function::iterator bi = f->begin(), be = f->end(); bi != be; ++bi) {
    BasicBlock *bb = bi;
    
    // note: ignoring "error blocks" (unreachable terminators)
    if (ReturnInst *ri = dyn_cast<ReturnInst>(bb->getTerminator())) {
      usedAfter.insert({bb, VarMapTy(nvars)});
      changed.insert(bb);
    }
  }
  
  // find variables possibly used after each block
  while(!changed.empty()) {
    BlockSetTy::iterator bi = changed.begin();
    BasicBlock* bb = *bi;
    changed.erase(bi);
    
    assert(usedAfter.find(bb) != usedAfter.end());
    VarMapTy used = usedAfter.find(bb)->second;
    
    // compute variables live at block start
    for(BasicBlock::reverse_iterator ii = bb->rbegin(), ie = bb->rend();  ii != ie; ++ii) {
      Instruction *in = &*ii;
      applyInstruction(in, used, varIndex);     
    }
    
    // merge into block predecessors
    for(pred_iterator pi = pred_begin(bb), pe = pred_end(bb); pi != pe; ++pi) {
      BasicBlock* pb = *pi;
      
      auto usearch = usedAfter.find(pb);
      if (usearch == usedAfter.end()) {
        usedAfter.insert({pb, used});
        changed.insert(pb);
      } else {
        VarMapTy& prevUsed = usearch->second;
        for(unsigned vi = 0; vi < nvars; vi++) { // union of used variables
          if (used[vi] && !prevUsed[vi]) {
            prevUsed[vi] = true;
            changed.insert(pb);
          }
        }
      }
    }
  }
  
  // convert results, and compute for each instruction
  LiveVarsTy live;
  
  for(UsedAfterTy::iterator ui = usedAfter.begin(), ue = usedAfter.end(); ui != ue; ++ui) {
    BasicBlock* bb = ui->first;
    VarMapTy& used = ui->second;
    
    for(BasicBlock::reverse_iterator ii = bb->rbegin(), ie = bb->rend();  ii != ie; ++ii) {
      Instruction *in = &*ii;
      
      // record used vars for instruction
      //   (result liveness info is relevant "after this instruction executes")
      VarsSetTy vars;
      for(unsigned vi = 0; vi < nvars; vi++) {
        if (used[vi]) {
          vars.insert(varIndex.at(vi));
        }
      }
      live.insert({in, vars});
      
      applyInstruction(in, used, varIndex);
    }
  }
  return live;
}
