#ifndef RCHK_LINEMSG_H
#define RCHK_LINEMSG_H

#include "common.h"

#include <set>

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>

#include <llvm/Support/StringPool.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

struct LineInfoTy {
  std::string kind;
  std::string message;
  std::string path;
  unsigned line;
  
  public:
    LineInfoTy(std::string kind, std::string message, std::string path, unsigned line): 
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

typedef std::set<LineInfoTy*, LineInfoTyPtr_compare> LineInfoPtrSetTy; // for ordering messages, uniqueness
typedef std::unordered_set<LineInfoTy, LineInfoTy_hash, LineInfoTy_equal> LineInfoSetTy; // for interning table (performance)

class BaseLineMessenger {

  protected:
    const bool DEBUG;
    const bool TRACE;
    const bool UNIQUE_MSG;  
  
  public:
    BaseLineMessenger(bool DEBUG, bool TRACE, bool UNIQUE_MSG):
      DEBUG(DEBUG), TRACE(TRACE), UNIQUE_MSG(UNIQUE_MSG) {};
      
    void trace(std::string msg, Instruction *in);
    void debug(std::string msg, Instruction *in);
    void info(std::string msg, Instruction *in);
    void error(std::string msg, Instruction *in);
    bool debug() { return DEBUG; }
    bool trace() { return TRACE; }
    bool uniqueMsg() { return UNIQUE_MSG; }
    
    void emit(std::string kind, std::string message, Instruction *in);
    virtual void emit(LineInfoTy* li) = 0;
};

class LineMessenger : public BaseLineMessenger {

  LineInfoPtrSetTy lineBuffer;
  LineInfoSetTy internTable;
    // the interning is important for the DelayedLineMessenger
    //   for printing messages directly with LineMessenger, one could easily
    //   do without it
  
  Function *lastFunction;
  std::string lastChecksName;
  LLVMContext& context;
  
  public:
    LineMessenger(LLVMContext& context, bool DEBUG, bool TRACE, bool UNIQUE_MSG):
      BaseLineMessenger(DEBUG, TRACE, UNIQUE_MSG), lineBuffer(), internTable(), lastFunction(NULL), lastChecksName(), context(context)  {};
      
    void flush();
    void clear();
    void newFunction(Function *func, std::string checksName);
    void newFunction(Function *func) { newFunction(func, ""); }
    
    LineInfoTy* intern(const LineInfoTy& li); // intern (but do not emit)
    void emitInterned(LineInfoTy* li); // emit line info interned in internTable
    
    virtual void emit(LineInfoTy* li);
};

// this is a rather special object for conditional/delayed messaging
//   it remembers messages, preparing them for being printed via LineMessenger msg,
//   but only prints them if/when flush() is called
//
// the messages are interned immediatelly with LineMessenger msg (which is
//   for performance of comparisons and for reducing the memory costs, because delayed
//   messengers are indeed part of the checking state

struct DelayedLineMessenger : public BaseLineMessenger {

  LineMessenger* msg; // used to print messages (flush) and to intern them
  LineInfoPtrSetTy delayedLineBuffer;
    
  DelayedLineMessenger(LineMessenger *msg):
    BaseLineMessenger(msg->debug(), msg->trace(), msg->uniqueMsg()), delayedLineBuffer(), msg(msg) {};
      
  void flush();
  bool operator==(const DelayedLineMessenger& other) const;
  virtual void emit(LineInfoTy* li);
  size_t size() { return delayedLineBuffer.size(); }
  void print(std::string prefix);
};

#endif
