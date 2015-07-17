#ifndef RCHK_LANNOTATE_H
#define RCHK_LANNOTATE_H

#include "common.h"

#include <set>

#include <llvm/IR/Instructions.h>

using namespace llvm;

struct LineTy {
  std::string path;
  unsigned line;
  
  LineTy(std::string path, unsigned line): path(path), line(line) {}
};

struct LineTy_compare {
  bool operator() (const LineTy& lhs, const LineTy& rhs) const {
    int cmp = lhs.path.compare(rhs.path);
    if (cmp) {
      return cmp < 0;
    }
    return lhs.line < rhs.line;
  }
};

typedef std::set<LineTy, LineTy_compare> LinesTy;

void annotateLine(LinesTy& lines, const Instruction* in);
void printLineAnnotations(LinesTy& lines);

#endif
