#ifndef RCHK_LINEMSG_H
#define RCHK_LINEMSG_H

#include "common.h"

#include <set>

#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>

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

struct LineInfoTy_compare {
  bool operator() (const LineInfoTy& lhs, const LineInfoTy& rhs) const;
};

typedef std::set<LineInfoTy, LineInfoTy_compare> LineBufferTy;

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
    
    void lineInfo(std::string kind, std::string message, Instruction *in);
    virtual void lineInfo(LineInfoTy& li, Function *func) = 0;
};

class LineMessenger : public BaseLineMessenger {

  LineBufferTy lineBuffer;
  Function *lastFunction;
  LLVMContext& context;
  
  public:
    LineMessenger(LLVMContext& context, bool DEBUG, bool TRACE, bool UNIQUE_MSG):
      BaseLineMessenger(DEBUG, TRACE, UNIQUE_MSG), lineBuffer(), lastFunction(NULL), context(context) {};
      
    void flush();
    void clearForFunction(Function *func);
    virtual void lineInfo(LineInfoTy& li, Function *func);
};

class DelayedLineMessenger : public BaseLineMessenger {

  LineBufferTy lineBuffer;
  
  public:
    DelayedLineMessenger(bool DEBUG, bool TRACE, bool UNIQUE_MSG):
      BaseLineMessenger(DEBUG, TRACE, UNIQUE_MSG), lineBuffer() {};
      
    void flushTo(BaseLineMessenger& msg, Function *func);
    bool operator==(const DelayedLineMessenger& other) const {
      return lineBuffer == other.lineBuffer && DEBUG == other.DEBUG && TRACE == other.TRACE && UNIQUE_MSG == other.UNIQUE_MSG;
    }
    virtual void lineInfo(LineInfoTy& li, Function *func = NULL);
    size_t size() { return lineBuffer.size(); }
    void print(std::string prefix);
};

#endif
