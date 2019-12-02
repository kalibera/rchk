/*
  This tool is to detect problems in foreign function interfaces.  It is
  primarily written to check return type of .Call and .External function
  registered via the C registration API.
*/ 

#include "common.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/raw_ostream.h>

#include <unordered_set>

#include "symbols.h"

using namespace llvm;

void check_type(Value *v) {

  #define CTSTRINGIFY(x) #x
  #define CTTOSTRING(x) CTSTRINGIFY(x)
  #define CT(x)  do { if (dyn_cast<x>(v) != NULL) { errs() << CTTOSTRING(x)  << "\n"; } } while(0);
  
  CT(Constant)
  
  CT(BlockAddress)
  CT(ConstantAggregateZero)
  CT(ConstantArray)
  CT(ConstantDataSequential)
  CT(ConstantExpr)
  CT(ConstantFP)
  CT(ConstantInt)
  CT(ConstantPointerNull)
  CT(ConstantStruct)
  CT(ConstantVector)
  CT(GlobalValue)
  CT(GetElementPtrInst)
  CT(UndefValue)
  CT(ConstantDataArray)
  CT(GlobalVariable)
  CT(Function)
  CT(GlobalIFunc)
    
  #undef CT
  #undef CTTOSTRING
  #undef CTSTRINGIFY
}

bool checkTable(Value *v) {
  if (ConstantExpr *ce = dyn_cast<ConstantExpr>(v)) {
    if (GlobalVariable *gv = dyn_cast<GlobalVariable>(ce->getOperand(0))) {
 
      int nfuns = -1;
      if (PointerType *pt = dyn_cast<PointerType>(gv->getType())) {
        if (ArrayType *at = dyn_cast<ArrayType>(pt->getElementType())) {
          nfuns = (int) at->getNumElements();
        }
      }
      
      if (nfuns == -1) {
        errs() << "ERROR: did not get the number of elements in function table\n";
        return false;    
      }
      
      errs() << "Functions: " << nfuns << "\n";
    
    
      if (ConstantArray *ca = dyn_cast<ConstantArray>(gv->getInitializer())) {
        for(unsigned i = 0; i < nfuns; i++) {
          ConstantStruct *cstr = dyn_cast<ConstantStruct>(ca->getAggregateElement(i));
          if (!cstr) {
            if (i == nfuns - 1)
              break;
            else {
              /* could check it is NULL */
              errs() << "ERROR: invalid entry in function table\n";
              return false;
            }
          }
          
          int64_t arity = -1;
          if (ConstantInt *ci = dyn_cast<ConstantInt>(cstr->getAggregateElement(2U))) {
            arity = ci->getSExtValue();
          } else {
            errs() << "ERROR: invalid arity in function table\n";
            return false;
          }
          
          std::string fname = "";
          if (ConstantExpr *ce = dyn_cast<ConstantExpr>(cstr->getAggregateElement(0U))) {
            if (GlobalVariable *ngv = dyn_cast<GlobalVariable>(ce->getOperand(0))) {
              if (ConstantDataArray *nda = dyn_cast<ConstantDataArray>(ngv->getInitializer())) {
                fname = nda->getAsCString();
              }
            }
          }
          if (fname.length() == 0) {
            errs() << "ERROR: invalid function name string in function table\n";
            return false;
          }
          
          Function *fun = NULL;
          if (ConstantExpr *ce = dyn_cast<ConstantExpr>(cstr->getAggregateElement(1U))) {
            fun = dyn_cast<Function>(ce->getOperand(0));
          }
          if (!fun) {
            errs() << "ERROR: invalid function in function table\n";
            return false;
          }
          
          if (!isSEXP(fun->getReturnType())) {
            errs() << "ERROR: Function " << fname << " (" << funName(fun) << ") does not return SEXP\n";
          }
          
          int64_t real_arity = fun->getFunctionType()->getNumParams();
          if (arity != real_arity) {
            errs() << "ERROR: Function " << fname << " (" << funName(fun) << ") has arity " << real_arity << " but registered arity " << arity << "\n";
          }
          
          /* errs() << "checked function " << fname << " (" << funName(fun) << ") arity " << arity << "\n"; */
        }
      }
    }
  }
  
  return true; /* successful parsing */
}

int main(int argc, char* argv[])
{
  LLVMContext context;
  FunctionsOrderedSetTy functionsOfInterestSet;
  FunctionsVectorTy functionsOfInterestVector;
  
  Module *m = parseArgsReadIR(argc, argv, functionsOfInterestSet, functionsOfInterestVector, context);
    // NOTE: functionsOfInterest ignored but (re-)analyzing the R core is necessary
 
  std::string initfn = "";
    
  for(FunctionsVectorTy::iterator fi = functionsOfInterestVector.begin(), fe = functionsOfInterestVector.end(); fi != fe; ++fi) {
    Function *fun = *fi;
    std::string fn = funName(fun);
    if (fn.find("R_init_"))
      continue;
      
    if (initfn.length() > 0) {
      errs() << "ERROR: Multiple initialization functions, now " << fn << " before " << initfn << "\n";
      return 1;
    } else {
      initfn = fn;
    }
  }
  
  if (!initfn.length()) {
    errs() << "ERROR: Did not find initialization function\n";
    return 1;
  }
    
  errs() << "Initialization function: " << initfn << "\n";
  Function* initf = m->getFunction(initfn);

  Function *regf = m->getFunction("R_registerRoutines");
  if (!regf) {
    errs() << "ERROR: Cannot get R_registerRoutines()\n";
    return 1;
  }
  
  bool checked = false;
  for(inst_iterator ini = inst_begin(*initf), ine = inst_end(*initf); ini != ine; ++ini) {
    Instruction *in = &*ini;
    CallSite cs(in);
    if (!cs) continue;
    
    Function *tgt = cs.getCalledFunction();
    if (tgt != regf)
      continue;
      
    /* call to R_registerRoutines */
    Value *cval = cs.getArgument(2); /* .Call */
    Value *eval = cs.getArgument(4); /* .External */
    
    /* errs() << "Call to R_registerRoutinets:\n    .Call: " << *cval << "\n    .External " << *eval << "\n"; */
    
    checkTable(cval);
    checked = true;
  }
  
  errs() << "Checked call to R_registerRoutinets: " << checked << "\n";
  
  delete m;
  return 0;
}
