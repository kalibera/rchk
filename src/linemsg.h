#ifndef RCHK_LINEMSG_H
#define RCHK_LINEMSG_H

#include "common.h"

#include "table.h"

#include <set>

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>

#include <llvm/Support/StringPool.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

struct LineInfoTy {
  const std::string kind;
  const std::string message;
  const std::string path;
  const unsigned line;
  
  public:
    LineInfoTy(const std::string& kind, const std::string& message, const std::string& path, unsigned line): 
      kind(kind), message(message), path(path), line(line) {}
    
    void print() const;
    bool operator==(const LineInfoTy& other) const {
      return kind == other.kind && message == other.message && path == other.path && line == other.line;
    }
};

struct LineInfoTyPtr_compare {
  bool operator() (const LineInfoTy* lhs, const LineInfoTy* rhs) const;
};

struct LineInfoTy_hash {
  size_t operator()(const LineInfoTy& t) const;
};

struct LineInfoTy_equal {
  bool operator() (const LineInfoTy& lhs, const LineInfoTy& rhs) const;
};

typedef std::set<const LineInfoTy*, LineInfoTyPtr_compare> LineInfoPtrSetTy; // for ordering messages, uniqueness
typedef InterningTable<LineInfoTy, LineInfoTy_hash, LineInfoTy_equal> LineInfoTableTy; // for interning table (performance)

class BaseLineMessenger {

  protected:
    bool DEBUG;
    bool TRACE;
    const bool UNIQUE_MSG;
    
    std::string withTrace(const std::string& msg, Instruction *in) const;
  
  public:
    BaseLineMessenger(bool DEBUG, bool TRACE, bool UNIQUE_MSG):
      DEBUG(DEBUG), TRACE(TRACE), UNIQUE_MSG(UNIQUE_MSG) {};
      
    void trace(const std::string& msg, Instruction *in);
    void debug(const std::string& msg, Instruction *in);
    void info(const std::string& msg, Instruction *in);
    void error(const std::string& msg, Instruction *in);
    bool debug() const { return DEBUG; } 
    bool trace() const { return TRACE; }
    void debug(bool v) { DEBUG = v; }
    void trace(bool v) { TRACE = v; }
    bool uniqueMsg() const { return UNIQUE_MSG; }
    
    void emit(const std::string& kind, const std::string& message, Instruction *in);
    virtual void emit(const LineInfoTy* li) = 0;
};

class LineMessenger : public BaseLineMessenger {

  LineInfoPtrSetTy lineBuffer;
  LineInfoTableTy internTable;
    // the interning is important for the DelayedLineMessenger
    //   for printing messages directly with LineMessenger, one could easily
    //   do without it
  
  Function *lastFunction;
  std::string lastChecksName;
  const LLVMContext& context;
  
  public:
    LineMessenger(LLVMContext& context, bool DEBUG, bool TRACE, bool UNIQUE_MSG):
      BaseLineMessenger(DEBUG, TRACE, UNIQUE_MSG), lineBuffer(), internTable(), lastFunction(NULL), lastChecksName(), context(context)  {};
      
    void flush();
    void clear();
    void newFunction(Function *func, const std::string& checksName);
    void newFunction(Function *func) { newFunction(func, ""); }
    
    const LineInfoTy* intern(const LineInfoTy& li); // intern (but do not emit)
    void emitInterned(const LineInfoTy* li); // emit line info interned in internTable
    
    virtual void emit(const LineInfoTy* li);
};

// this is a rather special object for conditional/delayed messaging
//   it remembers messages, preparing them for being printed via LineMessenger msg,
//   but only prints them if/when flush() is called
//
// the messages are interned immediatelly with LineMessenger msg (which is
//   for performance of comparisons and for reducing the memory costs, because delayed
//   messengers are indeed part of the checking state

struct DelayedLineMessenger : public BaseLineMessenger {

  LineMessenger* const msg; // used to print messages (flush) and to intern them
  LineInfoPtrSetTy delayedLineBuffer; // for interned messages
    // it would not have to be ordered for correctness, but note that the speed of comparison of delayedLineBuffers
    // is more important than the speed of inserting into the buffer
    
  DelayedLineMessenger(LineMessenger *msg):
    BaseLineMessenger(msg->debug(), msg->trace(), msg->uniqueMsg()), delayedLineBuffer(), msg(msg) {};
      
  void flush();
  bool operator==(const DelayedLineMessenger& other) const;
  virtual void emit(const LineInfoTy* li);
  size_t size() const { return delayedLineBuffer.size(); }
  void print(const std::string& prefix);
};

#endif
