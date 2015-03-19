
#include "linemsg.h"

using namespace llvm;

void BaseLineMessenger::trace(std::string msg, Instruction *in) {
  if (TRACE) {
    emit("TRACE", msg + instructionAsString(in), in);
  }
}

void BaseLineMessenger::debug(std::string msg, Instruction *in) {
  if (TRACE) {
    msg = msg + instructionAsString(in);
  }
  if (DEBUG) {
    emit("DEBUG", msg, in);
  }
}

void BaseLineMessenger::info(std::string msg, Instruction *in) {
  if (TRACE) {
    msg = msg + instructionAsString(in);
  }
  emit(DEBUG ? "INFO" : "", msg, in);
}

void BaseLineMessenger::error(std::string msg, Instruction *in) {
  if (TRACE) {
    msg = msg + instructionAsString(in);
  }
  emit("ERROR", msg, in);
}

void BaseLineMessenger::emit(std::string kind, std::string message, Instruction *in) {
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
  outs() << message << " " << path << ":" << line << "\n";
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
    for(LineInfoPtrSetTy::iterator liBuf = lineBuffer.begin(), liEbuf = lineBuffer.end(); liBuf != liEbuf; ++liBuf) {
      LineInfoTy* li = *liBuf;
      li->print();
    }
    lineBuffer.clear();
  }
  internTable.clear();
  lastFunction = NULL;
}

void LineMessenger::newFunction(Function *func, std::string checksName) {
  if (!UNIQUE_MSG) {
    outs() << "\nFunction " << funName(func) << checksName << "\n";
  } else {
    flush();
  }
  lastChecksName = checksName;
  lastFunction = func;
}

void LineMessenger::emitInterned(LineInfoTy* li) {
  if (!UNIQUE_MSG) {
    li->print();
  } else {
    lineBuffer.insert(li);
  }
}

void LineMessenger::emit(LineInfoTy* li) {
  emitInterned(intern(*li));
}

LineInfoTy* LineMessenger::intern(const LineInfoTy& li) {
  auto linsert = internTable.insert(li);
  const LineInfoTy *ili = &*linsert.first;
  return const_cast<LineInfoTy*>(ili);
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

void DelayedLineMessenger::emit(LineInfoTy *li) {
  delayedLineBuffer.insert(msg->intern(*li));
}

void DelayedLineMessenger::flush() {
  for(LineInfoPtrSetTy::iterator bi = delayedLineBuffer.begin(), be = delayedLineBuffer.end(); bi != be; ++bi) {
    msg->emitInterned(*bi);
  }
  delayedLineBuffer.clear();
}

void DelayedLineMessenger::print(std::string prefix) {
  for(LineInfoPtrSetTy::iterator bi = delayedLineBuffer.begin(), be = delayedLineBuffer.end(); bi != be; ++bi) {
    LineInfoTy *li = *bi;
    outs() << prefix;
    li->print();
  }
}

bool DelayedLineMessenger::operator==(const DelayedLineMessenger& other) const {
  return delayedLineBuffer == other.delayedLineBuffer && msg == other.msg;
}
