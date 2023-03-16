
#include "exceptions.h"

// some manually added exceptions that so far seem too hard to find automatically
bool isKnownNonAllocator(Function *f) {
  if (isInstall(f)) return true;

  if (!f) return false;

  if (f->getName() == "mkPRIMSXP") return true; // mkPRIMSXP caches its results internally (and permanently)
  if (f->getName() == "GETSTACK_PTR_TAG") return true; // GETSTACK_PTR_TAG stores the allocated result to the byte-code stack
  if (f->getName() == "lookupAssignFcnSymbol") return true; // lookupAssignFcnSymbol reads (symbols) from a hashmap, the map cannot have active bindings
  
  return false;
}

bool isKnownNonAllocator(const CalledFunctionTy *f) {

  return isKnownNonAllocator(f->fun);
}

bool isAssertedNonAllocating(Function *f) {
  if (f->getName() == "Rf_envlength") return true; // this impacts also length, xlength, inherits, nthcdr, is*, etc
  if (f->getName() == "Rf_envxlength") return true;
  if (f->getName() == "R_AllocStringBuffer") return true; // perhaps the warning in R code could be turned to error?
  
  if (f->getName() == "INTEGER_GET_REGION") return true;
  if (f->getName() == "REAL_GET_REGION") return true;

  // R_GCEnabled is not supported by the tool
  if (f->getName() == "ALTVEC_DATAPTR") return true;
  if (f->getName() == "ALTVEC_DATAPTR_EX") return true;  
  if (f->getName() == "ALTREP_LENGTH") return true;

  if (f->getName() == "ALTCOMPLEX_ELT") return true;
  if (f->getName() == "ALTINTEGER_ELT") return true;
  if (f->getName() == "ALTLOGICAL_ELT") return true;  
  if (f->getName() == "ALTRAW_ELT") return true;
  if (f->getName() == "ALTREAL_ELT") return true;

  if (f->getName() == "ALTSTRING_ELT") return true;
  
  if (f->getName() == "ALTSTRING_SET_ELT") return true;

  if (f->getName() == "ALTLIST_ELT") return true;

  if (f->getName() == "ALTLIST_SET_ELT") return true;
  
  if (f->getName() == "ALTINTEGER_MIN") return true;  
  if (f->getName() == "ALTINTEGER_MAX") return true;
  if (f->getName() == "ALTREAL_MIN") return true;  
  if (f->getName() == "ALTREAL_MAX") return true;
  return false;
}

bool isKnownVectorReturningFunction(const CalledFunctionTy* f) {
  std::string name = funName(f);
  
  if (name == "Rf_getAttrib(?,S:dimnames)" || name == "Rf_getAttrib(V,S:dimnames)") { // FIXME: the tool now would not be infer this from exception on getAttrib0
    return true;
  }
  return false;
}

bool isAssertedNonAllocating(const CalledFunctionTy *f) {
  return isAssertedNonAllocating(f->fun);
}


bool avoidSEXPGuardsFor(Function *f) {
  if (f->getName() == "bcEval") return true;
  return false;
}

bool avoidSEXPGuardsFor(const CalledFunctionTy *f) {
  return avoidSEXPGuardsFor(f->fun);
}

bool avoidIntGuardsFor(Function *f) {
  if (f->getName() == "_controlify") return true;
  return false;
}

bool avoidIntGuardsFor(const CalledFunctionTy *f) {
  return avoidIntGuardsFor(f->fun);
}

bool protectsArguments(Function *f) {
  if (f->getName() == "Rf_setAttrib") return true; // but may destroy them
  if (f->getName() == "Rf_namesgets") return true; // but may destroy them
  if (f->getName() == "Rf_dimgets") return true; // but may destroy them
  if (f->getName() == "Rf_dimnamesgets") return true; // but may destroy them
  if (f->getName() == "Rf_classgets") return true; // fully
  if (f->getName() == "Rf_tspgets") return true; // but may destroy them
  if (f->getName() == "commentgets") return true; // fully
  if (f->getName() == "row_names_gets") return true; // but may destroy them
  if (f->getName() == "installAttrib") return true; //
  if (f->getName() == "R_NewHashedEnv") return true; // fully
  if (f->getName() == "Rf_defineVar") return true; // not really, not for rho
  if (f->getName() == "Rf_setVar") return true;
  if (f->getName() == "GetRNGkind") return true; // but may destroy them in case they are logically erroneous
  if (f->getName() == "Rf_ScalarString") return true; // fully
  if (f->getName() == "Rf_list1") return true; // fully
  if (f->getName() == "Rf_list2") return true; // fully
  if (f->getName() == "Rf_list3") return true; // fully
  if (f->getName() == "Rf_list4") return true; // fully
  if (f->getName() == "Rf_list5") return true; // fully  
  if (f->getName() == "Rf_lang1") return true; // fully
  if (f->getName() == "Rf_lang2") return true; // fully
  if (f->getName() == "Rf_lang3") return true; // fully
  if (f->getName() == "Rf_lang4") return true; // fully
  if (f->getName() == "Rf_lang5") return true; // fully
  if (f->getName() == "Rf_lang6") return true; // fully
  if (f->getName() == "Rf_lcons") return true; // fully
  if (f->getName() == "Rf_cons") return true; // fully
  if (f->getName() == "Rf_asInteger") return true; // but may destroy it in case of warning
  if (f->getName() == "math2") return true; // but may destroy them in certain cases
  if (f->getName() == "R_PreserveObject") return true; // fully
  if (f->getName() == "Rf_DropDims") return true; // fully
  if (f->getName() == "Rf_duplicate") return true;
  if (f->getName() == "Rf_NewEnvironment") return true; // fully, (trick)
  if (f->getName() == "Rf_VectorToPairList") return true; // fully
  if (f->getName() == "CONS_NR") return true; // fully, (trick)
  if (f->getName() == "mkPROMISE") return true; // fully, (trick)  
  if (f->getName() == "R_mkEVPROMISE") return true; // fully
  if (f->getName() == "R_mkEVPROMISE_NR") return true; // fully  
  if (f->getName() == "asLogicalNoNA") return true;  // fully
  if (f->getName() == "NewWeakRef") return true; // fully
  if (f->getName() == "Rf_mkSYMSXP") return true; // fully
  if (f->getName() == "SetOption") return true; // not quite (tag is not protected, but it is a symbol)
  if (f->getName() == "R_FixupRHS") return true;
  if (f->getName() == "Rf_gsetVar") return true;
  if (f->getName() == "Rf_translateChar") return true; // but may destroy it
  if (f->getName() == "R_FindNamespace") return true; // fully
  if (f->getName() == "Rf_shallow_duplicate") return true;
  if (f->getName() == "R_AddGlobalCache") return true;
  if (f->getName() == "addStackArgsList") return true;
  if (f->getName() == "addS3Var") return true;
  if (f->getName() == "R_getS4DataSlot") return true;
  if (f->getName() == "R_RegisterCFinalizer") return true;
  if (f->getName() == "Rf_installChar") return true;
  if (f->getName() == "Rf_copyMostAttrib") return true;
  return false;
}

bool protectsArguments(const CalledFunctionTy *f) {
  return protectsArguments(f->fun);
}
