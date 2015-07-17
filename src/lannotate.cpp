
#include "lannotate.h"

#include <llvm/Support/raw_ostream.h>

using namespace llvm;

void annotateLine(LinesTy& lines, const Instruction* in) {
  std::string path;
  unsigned line;
  sourceLocation(cast<Instruction>(in), path, line);
  LineTy l(path, line);
  lines.insert(l);
}

void printLineAnnotations(LinesTy& lines) {
  for(LinesTy::const_iterator li = lines.begin(), le = lines.end(); li != le; ++li) {
    const LineTy& l = *li;
    outs() << l.path << " " << std::to_string(l.line) << "\n";
  }
}
