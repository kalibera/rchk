
#include "linemsg.h"

using namespace llvm;

std::string BaseLineMessenger::withTrace(const std::string& msg, Instruction *in) const {
  if (TRACE) {
    return msg + instructionAsString(in);
  } 
  return msg;
}

void BaseLineMessenger::trace(const std::string& msg, Instruction *in) {
  if (TRACE) {
    emit("TRACE", withTrace(msg, in), in);
  }
}

void BaseLineMessenger::debug(const std::string& msg, Instruction *in) {
  if (DEBUG) {
    emit("DEBUG", withTrace(msg, in), in);
  }
}

void BaseLineMessenger::info(const std::string& msg, Instruction *in) {
  emit(DEBUG ? "INFO" : "", withTrace(msg, in), in);
}

void BaseLineMessenger::error(const std::string& msg, Instruction *in) {
  emit("ERROR", withTrace(msg, in), in);
}

void BaseLineMessenger::emit(const std::string& kind, const std::string& message, Instruction *in) {
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
  emit(&li);
}


// -----------------------------

void LineInfoTy::print() const {
  outs() << "  ";
  if (!kind.empty()) {
    outs()  << kind << ": ";
  }
  if (path.empty()) {
    outs() << message << "\n";
  } else {
    outs() << message << " " << path << ":" << line << "\n";
  }
}

bool LineInfoTyPtr_compare::operator() (const LineInfoTy* lhs, const LineInfoTy* rhs) const {
  int cmp;
  cmp = lhs->path.compare(rhs->path);
  if (cmp) {
    return cmp < 0;
  }
  if (lhs->line != rhs->line) {
    return lhs->line < rhs->line;
  }
  cmp = lhs->message.compare(rhs->message);
  if (cmp) {
    return cmp < 0;
  }
  cmp = lhs->kind.compare(rhs->kind);
  return cmp < 0;
}

size_t LineInfoTy_hash::operator()(const LineInfoTy& t) const {
  return t.line;
}

bool LineInfoTy_equal::operator() (const LineInfoTy& lhs, const LineInfoTy& rhs) const {
  return lhs.line == rhs.line && lhs.message == rhs.message && lhs.path == rhs.path && lhs.kind == rhs.kind;
}

// ----------------------------- 

void LineMessenger::flush() {
  if (lastFunction != NULL && !lineBuffer.empty()) {
    outs() << "\nFunction " << funName(lastFunction) << lastChecksName << "\n";
    for(LineInfoPtrSetTy::const_iterator liBuf = lineBuffer.begin(), liEbuf = lineBuffer.end(); liBuf != liEbuf; ++liBuf) {
      const LineInfoTy* li = *liBuf;
      li->print();
    }
    lineBuffer.clear();
  }
  internTable.clear();
  lastFunction = NULL;
}

void LineMessenger::newFunction(Function *func, const std::string& checksName) {
  if (!UNIQUE_MSG) {
    outs() << "\nFunction " << funName(func) << checksName << "\n";
  } else {
    flush();
  }
  lastChecksName = checksName;
  lastFunction = func;
}

void LineMessenger::emitInterned(const LineInfoTy* li) {
  if (!UNIQUE_MSG) {
    li->print();
  } else {
    lineBuffer.insert(li);
  }
}

void LineMessenger::emit(const LineInfoTy* li) {
  emitInterned(intern(*li));
}

const LineInfoTy* LineMessenger::intern(const LineInfoTy& li) {
  return internTable.intern(li);
}

void LineMessenger::clear() {
  if (!UNIQUE_MSG) {
    outs() << " ---- restarting checking for function " << funName(lastFunction) << " (previous messages for it to be ignored) ----\n";
  } else {
    lineBuffer.clear();
    // not clearing the intern table
  }
}

// ----------------------------- 

void DelayedLineMessenger::emit(const LineInfoTy *li) {
  delayedLineBuffer.insert(msg->intern(*li));
}

void DelayedLineMessenger::flush() {
  for(LineInfoPtrSetTy::const_iterator bi = delayedLineBuffer.begin(), be = delayedLineBuffer.end(); bi != be; ++bi) {
    msg->emitInterned(*bi);
  }
  delayedLineBuffer.clear();
}

void DelayedLineMessenger::print(const std::string& prefix) {
  for(LineInfoPtrSetTy::const_iterator bi = delayedLineBuffer.begin(), be = delayedLineBuffer.end(); bi != be; ++bi) {
    const LineInfoTy *li = *bi;
    outs() << prefix;
    li->print();
  }
}

bool DelayedLineMessenger::operator==(const DelayedLineMessenger& other) const {
  return delayedLineBuffer == other.delayedLineBuffer && msg == other.msg;
}
