
#include "linemsg.h"

using namespace llvm;

void BaseLineMessenger::trace(std::string msg, Instruction *in) {
  if (TRACE) {
    lineInfo("TRACE", msg + instructionAsString(in), in);
  }
}

void BaseLineMessenger::debug(std::string msg, Instruction *in) {
  if (TRACE) {
    msg = msg + instructionAsString(in);
  }
  if (DEBUG) {
    lineInfo("DEBUG", msg, in);
  }
}

void BaseLineMessenger::info(std::string msg, Instruction *in) {
  if (TRACE) {
    msg = msg + instructionAsString(in);
  }
  lineInfo(DEBUG ? "INFO " : "", msg, in);
}

void BaseLineMessenger::error(std::string msg, Instruction *in) {
  if (TRACE) {
    msg = msg + instructionAsString(in);
  }
  lineInfo("ERROR", msg, in);
}

void BaseLineMessenger::lineInfo(std::string kind, std::string message, Instruction *in) {
  if (kind == "DEBUG" && !DEBUG) {
    return;
  }
  if (kind == "TRACE" && !TRACE) {
    return;
  }

  std::string path;
  unsigned line;
  sourceLocation(in, path, line);
  LineInfoTy li(kind, message, path, line);
  lineInfo(li, in->getParent()->getParent());
}


// -----------------------------

void LineInfoTy::print() const {
  errs() << "  ";
  if (!kind.empty()) {
    errs()  << kind << ": ";
  }
  errs() << message << " " << path << ":" << line << "\n";
}

bool LineInfoTy_compare::operator() (const LineInfoTy& lhs, const LineInfoTy& rhs) const {
  int cmp;
  cmp = lhs.path.compare(rhs.path);
  if (cmp) {
    return cmp < 0;
  }
  if (lhs.line != rhs.line) {
    return lhs.line < rhs.line;
  }
  cmp = lhs.message.compare(rhs.message);
  if (cmp) {
    return cmp < 0;
  }
  cmp = lhs.kind.compare(rhs.kind);
  return cmp < 0;
}

// ----------------------------- 

void LineMessenger::flush() {
  if (lastFunction != NULL && !lineBuffer.empty()) {
    errs() << "\nFunction " << demangle(lastFunction->getName()) << "\n";
    for(LineBufferTy::iterator liBuf = lineBuffer.begin(), liEbuf = lineBuffer.end(); liBuf != liEbuf; ++liBuf) {
      liBuf->print();
    }
    lineBuffer.clear();
  }
  lastFunction = NULL;
}

void LineMessenger::lineInfo(LineInfoTy& li, Function *func) {
  if (!UNIQUE_MSG) {
    if (lastFunction != func) {
      errs() << "\nFunction " << demangle(func->getName()) << "\n";
      lastFunction = func;
    }
    li.print();
  } else {
    if (lastFunction != func) {
      flush();
      lastFunction = func;
    }
    lineBuffer.insert(li);
  }
}


void LineMessenger::clearForFunction(Function *func) {
  if (!UNIQUE_MSG) {
    if (lastFunction == func) {
      errs() << " ---- restarting checking for function " << demangle(func->getName()) << " (previous messages for it to be ignored) ----\n";
    }
    return;
  }
  if (lastFunction == func) {
    lineBuffer.clear();  
  }
}

// ----------------------------- 

void DelayedLineMessenger::lineInfo(LineInfoTy& li, Function *func) {
  lineBuffer.insert(li);
}

void DelayedLineMessenger::flushTo(BaseLineMessenger& msg, Function *func) {
  for(LineBufferTy::iterator bi = lineBuffer.begin(), be = lineBuffer.end(); bi != be; ++bi) {
    LineInfoTy li(*bi); // FIXME: extra copy
    msg.lineInfo(li, func);
  }
  lineBuffer.clear();
}
