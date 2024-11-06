
#include "patterns.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

bool isAllocVectorOfKnownType(Value *inst, unsigned& type) {

  if (!CallBase::classof(inst))
    return false;
  CallBase *cs = cast<CallBase>(inst);
  if (!cs) {
    return false;
  }
  Function *tgt = cs->getCalledFunction();
  if (!tgt || tgt->getName() != "Rf_allocVector") {
    return false;
  }
  Value *arg = cs->getArgOperand(0);
  if (!ConstantInt::classof(arg)) {
    return false;
  }
  
  ConstantInt *ctype = cast<ConstantInt>(arg);
  type = ctype->getZExtValue();
  return true;
}

bool isCallPassingVar(Value *inst, AllocaInst*& var, std::string& fname) {

  if (!CallBase::classof(inst))
    return false;
  CallBase *cs = cast<CallBase>(inst);
  if (!cs) {
    return false;
  }

  Function *tgt = cs->getCalledFunction();
  if (!tgt) {
    return false;
  }
  
  Value *lval = cs->getArgOperand(0);
  if (!LoadInst::classof(lval)) {
    return false;
  }
  
  Value *lvar = cast<LoadInst>(lval)->getPointerOperand();
  if (!AllocaInst::classof(lvar)) {
    return false;
  }
  
  var = cast<AllocaInst>(lvar);
  fname = tgt->getName().str();
  return true;
}

bool isBitCastOfVar(Value *inst, AllocaInst*& var, Type*& type) {

  if (!BitCastInst::classof(inst)) {
    return false;
  }
  BitCastInst* bc = cast<BitCastInst>(inst);
  
  Value *lvar = bc->getOperand(0);
  if (!LoadInst::classof(lvar)) {
    return false;
  }
  Value *avar = cast<LoadInst>(lvar)->getPointerOperand();
  if (!AllocaInst::classof(avar)) {
    return false;
  }
  
  var = cast<AllocaInst>(avar);
  type = cast<Type>(bc->getDestTy());
  return true;
}

// this is useful e.g. for detecting when a variable is stored into the node stack
//   isStoreToStructureElement(in, "struct.R_bcstack_t", "union.ieee_double", protectedVar)

bool isStoreToStructureElement(Value *inst, std::string structType, std::string elementType, AllocaInst*& var) {

  // [] %431 = load %struct.SEXPREC** %__v__7, align 8, !dbg !152225 ; [#uses=1 type=%struct.SEXPREC*] [debug line = 4610:5]
  // %432 = load %struct.R_bcstack_t** %3, align 8, !dbg !152225 ; [#uses=1 type=%struct.R_bcstack_t*] [debug line = 4610:5]
  // %433 = getelementptr inbounds %struct.R_bcstack_t* %432, i32 0, i32 1, !dbg !152225 ; [#uses=1 type=%union.ieee_double*] [debug line = 4610:5]
  // %434 = bitcast %union.ieee_double* %433 to %struct.SEXPREC**, !dbg !152225 ; [#uses=1 type=%struct.SEXPREC**] [debug line = 4610:5]
  // store %struct.SEXPREC* %431, %struct.SEXPREC** %434, align 8, !dbg !152225 ; [debug line = 4610:5]
          
  StoreInst *si = dyn_cast<StoreInst>(inst);
  if (!si) {
    return false;
  }
  
  LoadInst *li = dyn_cast<LoadInst>(si->getValueOperand());
  if (!li) {
    return false;
  }
  
  AllocaInst *pvar = dyn_cast<AllocaInst>(li->getPointerOperand());
  if (!pvar) {
    return false;
  }
  
  BitCastInst *bc = dyn_cast<BitCastInst>(si->getPointerOperand());
  if (!bc) {
    return false;
  }
  
  if (!isPointerToStruct(bc->getSrcTy(), elementType)) {
    return false;
  }
  
  GetElementPtrInst* gep = dyn_cast<GetElementPtrInst>(bc->getOperand(0));
  if (!gep || !gep->isInBounds() || !isPointerToStruct(gep->getPointerOperandType(), structType)) {
    return false;
  }
  
  var = pvar;
  return true;
}

// detect if variable proxyVar when used at instruction "useInst" has the same value as 
//   some other variable origVar
//
// this is very primitive form of alias analysis, intended for cases like
//
// #define SETSTACK_PTR(s, v) do {
//    SEXP __v__ = (v);
//    (s)->tag = 0;
//    (s)->u.sxpval = __v__;
// } while (0)
//
// when we need to know the real name of variable "v"
            
bool aliasesVariable(Value *useInst, AllocaInst *proxyVar, AllocaInst*& origVar) {

  StoreInst *si = NULL;
  if (!findOnlyStoreTo(proxyVar, si)) {
    return false;
  }
  
  LoadInst *li = dyn_cast<LoadInst>(si->getValueOperand());
  if (!li) {
    return false;
  }
  
  AllocaInst *ovar = dyn_cast<AllocaInst>(li->getPointerOperand());
  if (!ovar) {
    return false;
  }
  
  // ovar may be the original variable...
  // but we need to check that ovar is not overwritten between the store (si) and the use (useInst)
  

  Instruction *ui = dyn_cast<Instruction>(useInst);
  if (!ui) {
    return false;
  }
  BasicBlock *bb = si->getParent();
  if (bb != ui->getParent()) {
    return false;
  }
  
  bool reachedStore = false;
  for(BasicBlock::iterator ii = bb->begin(), ie = bb->end(); ii != ie; ++ii) {
    Instruction *in = &*ii;
    
    if (in == si) {
      reachedStore = true;
      continue;
    }
    
    if (in == ui) {
      if (reachedStore) {
        origVar = ovar;
        return true;
      }
      return false;
    }
    
    // FIXME: check if the variable(s) have address taken
    if (reachedStore) {
      if (StoreInst *s = dyn_cast<StoreInst>(in)) {
        if (s->getPointerOperand() == ovar) {
          // detected interleacing write
          return false;
        }
      }
    }
  }
  // not reached really
  return false;
}

bool findOnlyStoreTo(AllocaInst* var, StoreInst*& definingStore) {

  StoreInst *si = NULL;
  for(Value::user_iterator ui = var->user_begin(), ue = var->user_end(); ui != ue; ++ui) {
    User *u = *ui;
    if (StoreInst *s = dyn_cast<StoreInst>(u)) {
      if (s->getPointerOperand() == var) {
        if (si == NULL) {
          si = s;
        } else {
          // more than one store
          return false;
        }
      }
    }
  }
  
  if (si == NULL) {
    return false;
  }
  
  definingStore = si;
  return true;
}

// find if operand of inst comes from a variable, and return that variable if it does
bool operandComesFromVariable(Instruction *inst, Value *operand, AllocaInst*& var) {

  if (LoadInst::classof(operand)) {
    // operand is directly a load from variable
    Value *varv = cast<LoadInst>(operand)->getPointerOperand();
    if (!AllocaInst::classof(varv)) {
      return false;
    }
    var = cast<AllocaInst>(varv);
    return true;
  }
  
  // handle assignment within a call, such as
  // %42 = call %struct.SEXPREC* @CAR(%struct.SEXPREC* noundef %41), !dbg !100075 ; [#uses=2 type=%struct.SEXPREC*] [debug line = 1477:9]
  // store %struct.SEXPREC* %42, %struct.SEXPREC** %9, align 8, !dbg !100075 ; [debug line = 1477:9]
  // %43 = getelementptr inbounds %struct.SEXPREC, %struct.SEXPREC* %42, i32 0, i32 0, !dbg !100075 ; [#uses=1 type=%struct.sxpinfo_struct*] [debug line = 1477:9]
  
  if (!inst->getParent() || !Instruction::classof(operand) || inst->getParent() != cast<Instruction>(operand)->getParent())
    return false; 

  // find the latest store of the value to a varible, which is still before the instruction
  StoreInst *latests = NULL;
  AllocaInst *latestsvar = NULL;
  for(Value::user_iterator ui = operand->user_begin(), ue = operand->user_end(); ui != ue; ++ui) {
    User *u = *ui;
    if (StoreInst::classof(u)) {
      StoreInst *si = cast<StoreInst>(u);
      if (si->getParent() && si->getParent() == inst->getParent() && si->comesBefore(inst)
          && si->getValueOperand() == operand) {
        Value* storePointer = si->getPointerOperand();
        if (AllocaInst::classof(storePointer) && (!latests || latests->comesBefore(si))) {
          latests = si;
          latestsvar = cast<AllocaInst>(storePointer);
        }
      }
    }
  }

  if (!latests)
    return false;
  
   // now make sure there is no later store (of another value) to the same variable before the instruction
   for(Value::user_iterator ui = latests->user_begin(), ue = latests->user_end(); ui != ue; ++ui) {
     User *u = *ui;
     if (StoreInst::classof(u)) {
       StoreInst *si = cast<StoreInst>(u);
       if (si->getParent() && si->getParent() == inst->getParent() && si->comesBefore(inst)
           && latests->comesBefore(si) && latests->getPointerOperand() == si->getPointerOperand())
         return false;
     }
   }
    
  var = latestsvar;
  return true;
}

// just does part of a type check
static bool isTypeExtraction(Value *inst, AllocaInst*& var) {

  // %33 = load %struct.SEXPREC** %2, align 8, !dbg !21240 ; [#uses=1 type=%struct.SEXPREC*] [debug line = 1097:0]
  // %34 = getelementptr inbounds %struct.SEXPREC* %33, i32 0, i32 0, !dbg !21240 ; [#uses=1 type=%struct.sxpinfo_struct*] [debug line = 1097:0]
  // %35 = bitcast %struct.sxpinfo_struct* %34 to i32*, !dbg !21240 ; [#uses=1 type=i32*] [debug line = 1097:0]
  // %36 = load i32* %35, align 4, !dbg !21240       ; [#uses=1 type=i32] [debug line = 1097:0]
  // %37 = and i32 %36, 31, !dbg !21240              ; [#uses=1 type=i32] [debug line = 1097:0]
  
  // it is more complicated with assignment within a call, such as
  //   isString(x = foo())
  //
  
  // %42 = call %struct.SEXPREC* @CAR(%struct.SEXPREC* noundef %41), !dbg !100075 ; [#uses=2 type=%struct.SEXPREC*] [debug line = 1477:9]
  // store %struct.SEXPREC* %42, %struct.SEXPREC** %9, align 8, !dbg !100075 ; [debug line = 1477:9]
  // %43 = getelementptr inbounds %struct.SEXPREC, %struct.SEXPREC* %42, i32 0, i32 0, !dbg !100075 ; [#uses=1 type=%struct.sxpinfo_struct*] [debug line = 1477:9]
  // %44 = bitcast %struct.sxpinfo_struct* %43 to i64*, !dbg !100075 ; [#uses=1 type=i64*] [debug line = 1477:9]
  // %45 = load i64, i64* %44, align 8, !dbg !100075 ; [#uses=1 type=i64] [debug line = 1477:9]
  // %46 = and i64 %45, 31, !dbg !100075             ; [#uses=1 type=i64] [debug line = 1477:9]



  BinaryOperator* andv = dyn_cast<BinaryOperator>(inst);
  if (!andv) {
    return false;
  }
  
  if (andv->getOpcode() != Instruction::And) {
    return false;
  }
  
  LoadInst* bitsLoad;
  ConstantInt* cmask;
  
  if (LoadInst::classof(andv->getOperand(0)) && ConstantInt::classof(andv->getOperand(1))) {
    bitsLoad = cast<LoadInst>(andv->getOperand(0));
    cmask = cast<ConstantInt>(andv->getOperand(1));
  } else if (LoadInst::classof(andv->getOperand(1)) && ConstantInt::classof(andv->getOperand(0))) {
    bitsLoad = cast<LoadInst>(andv->getOperand(1));
    cmask = cast<ConstantInt>(andv->getOperand(0));
  } else {
    return false;
  } 
  
  if (!cmask->equalsInt(31)) {
    // NOTE: getZExtValue() may assert that the value fits 64 bits
    return false;
  }
  
  if (!BitCastInst::classof(bitsLoad->getPointerOperand())) {
    return false;
  }
  Value *gepv = cast<BitCastInst>(bitsLoad->getPointerOperand())->getOperand(0);
  
  if (!GetElementPtrInst::classof(gepv)) {
    return false;
  }

  GetElementPtrInst *gep = cast<GetElementPtrInst>(gepv);
  if (!gep->isInBounds() || !gep->hasAllZeroIndices() || !isSEXP(gep->getPointerOperandType())) {
    return false;
  }
  
  return operandComesFromVariable(gep, gep->getPointerOperand(), var);
  

}

bool isTypeCheck(Value *inst, bool& positive, AllocaInst*& var, unsigned& type) {

  // %33 = load %struct.SEXPREC** %2, align 8, !dbg !21240 ; [#uses=1 type=%struct.SEXPREC*] [debug line = 1097:0]
  // %34 = getelementptr inbounds %struct.SEXPREC* %33, i32 0, i32 0, !dbg !21240 ; [#uses=1 type=%struct.sxpinfo_struct*] [debug line = 1097:0]
  // %35 = bitcast %struct.sxpinfo_struct* %34 to i32*, !dbg !21240 ; [#uses=1 type=i32*] [debug line = 1097:0]
  // %36 = load i32* %35, align 4, !dbg !21240       ; [#uses=1 type=i32] [debug line = 1097:0]
  // %37 = and i32 %36, 31, !dbg !21240              ; [#uses=1 type=i32] [debug line = 1097:0]
  // %38 = icmp eq i32 %37, 22, !dbg !21240          ; [#uses=1 type=i1] [debug line = 1097:0]
  
    // but since ALTREP header changes, the pattern is
  
  // %42 = load %struct.SEXPREC*, %struct.SEXPREC** %5, align 8, !dbg !44519 ; [#uses=1 type=%struct.SEXPREC*] [debug line = 164:9]
  // %43 = getelementptr inbounds %struct.SEXPREC, %struct.SEXPREC* %42, i32 0, i32 0, !dbg !44519 ; [#uses=1 type=%struct.sxpinfo_struct*] [debug line = 164:9]
  // %44 = bitcast %struct.sxpinfo_struct* %43 to i64*, !dbg !44519 ; [#uses=1 type=i64*] [debug line = 164:9]
  // %45 = load i64, i64* %44, align 8, !dbg !44519  ; [#uses=1 type=i64] [debug line = 164:9]
  // %46 = and i64 %45, 31, !dbg !44519              ; [#uses=1 type=i64] [debug line = 164:9]
  // %47 = trunc i64 %46 to i32, !dbg !44519         ; [#uses=1 type=i32] [debug line = 164:9]  <===== extra truncate
  // %48 = icmp eq i32 %47, 16, !dbg !44519          ; [#uses=1 type=i1] [debug line = 164:9]
  // br i1 %48, label %49, label %53, !dbg !44521    ; [debug line = 164:9]


  if (!CmpInst::classof(inst)) {
    return false;
  }
  CmpInst *ci = cast<CmpInst>(inst);
  if (!ci->isEquality()) {
    return false;
  }
  
  positive = ci->isTrueWhenEqual();
  
  ConstantInt* ctype;
  Value *other;
  
  if (ConstantInt::classof(ci->getOperand(0))) {
    ctype = cast<ConstantInt>(ci->getOperand(0));
    other = ci->getOperand(1);
  } else if (ConstantInt::classof(ci->getOperand(1))) {
    ctype = cast<ConstantInt>(ci->getOperand(1));
    other = ci->getOperand(0);  
  } else {
    return false;
  }

  if (TruncInst::classof(other)) {
    other = (cast<TruncInst>(other))->getOperand(0);
  }
  
  BinaryOperator* andv;
  if (BinaryOperator::classof(other)) {
    andv = cast<BinaryOperator>(other);
  } else {
    return false;
  }
  if (isTypeExtraction(andv, var)) {
    type = ctype->getZExtValue();
    return true;
  }
  
  return false;
}

bool isTypeSwitch(Value *inst, AllocaInst*& var, BasicBlock*& defaultSucc, TypeSwitchInfoTy& info) {

  //  switch (TYPEOF(var)) {
  //    case ...
  //    case ....

  // %187 = load %struct.SEXPREC** %4, align 8, !dbg !195121 ; [#uses=1 type=%struct.SEXPREC*] [debug line = 407:13]
  // %188 = getelementptr inbounds %struct.SEXPREC* %187, i32 0, i32 0, !dbg !195121 ; [#uses=1 type=%struct.sxpinfo_struct*] [debug line = 407:13]
  // %189 = bitcast %struct.sxpinfo_struct* %188 to i32*, !dbg !195121 ; [#uses=1 type=i32*] [debug line = 407:13]
  // %190 = load i32* %189, align 4, !dbg !195121    ; [#uses=1 type=i32] [debug line = 407:13]
  // %191 = and i32 %190, 31, !dbg !195121           ; [#uses=1 type=i32] [debug line = 407:13]
  // switch i32 %191, label %224 [
  //   i32 0, label %192 <==== NILSXP
  //   i32 2, label %192 <==== LISTSXP
  //   i32 19, label %193 <=== VECSXP
  // ], !dbg !195122                                 ; [debug line = 407:5]

  SwitchInst *si = dyn_cast<SwitchInst>(inst);
  if (!si) {
    return false;
  }

  if (!isTypeExtraction(si->getCondition(), var)) {
    return false;
  }

  info.clear();

  for(SwitchInst::CaseIt ci = si->case_begin(), ce = si->case_end(); ci != ce; ++ci) {
  
#if LLVM_VERSION_MAJOR>=5
    ConstantInt *val = ci->getCaseValue();
    BasicBlock *succ = ci->getCaseSuccessor();
#else  
    ConstantInt *val = ci.getCaseValue();
    BasicBlock *succ = ci.getCaseSuccessor();
#endif
    
    info.insert({succ, val->getZExtValue()});
  }
  
  defaultSucc = si->getDefaultDest();
  
  return true;
}

bool isCallThroughPointer(Value *inst) {
  if (CallInst* ci = dyn_cast<CallInst>(inst)) {
    return LoadInst::classof(ci->getCalledOperand());
  } else {
    return false;
  }
}

ValuesSetTy valueOrigins(Value *inst) {

  ValuesSetTy origins;
  origins.insert(inst);
  bool insertedValue = true;
  
  while(insertedValue) {
    insertedValue = false;
    for(ValuesSetTy::iterator vi = origins.begin(), ve = origins.end(); vi != ve; ++vi) {
      Value *v = *vi;
      
      if (!Instruction::classof(v) || CallInst::classof(v) || InvokeInst::classof(v) || AllocaInst::classof(v)) {
        continue;
      }
      Instruction *inst = cast<Instruction>(v);
      for(Instruction::op_iterator oi = inst->op_begin(), oe = inst->op_end(); oi != oe; ++oi) {
        Value *op = *oi;
        auto vinsert = origins.insert(op);
        if (vinsert.second) {
          insertedValue = true;
        }
      }
    }
  }
  return origins;
}

// check if value inst origins from a load of variable var
//   it may directly be the load of var
//   but it may also be a result of a number of non-load and non-call instructions

AllocaInst* originsOnlyFromLoad(Value *inst) {

  if (LoadInst *l = dyn_cast<LoadInst>(inst)) {
    if (AllocaInst *lv = dyn_cast<AllocaInst>(l->getPointerOperand())) { // fast path
      return lv;
    }
  }

  ValuesSetTy origins = valueOrigins(inst);
  
  AllocaInst* onlyVar = NULL;
  for(ValuesSetTy::iterator vi = origins.begin(), ve = origins.end(); vi != ve; ++vi) {
    Value *v = *vi;
    if (CallInst::classof(v) || InvokeInst::classof(v)) {
      return NULL;
    }
    if (AllocaInst *curVar = dyn_cast<AllocaInst>(v)) {
      if (!onlyVar) {
        onlyVar = curVar;
      } else {
        if (onlyVar != curVar) {
          // multiple origins
          return NULL;
        }
      }
    }
    // FIXME: need to handle anything more?
  }
  return onlyVar;
}


