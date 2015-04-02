#ifndef RCHK_TABLE_H
#define RCHK_TABLE_H

#include <unordered_set>
#include <vector>

template <
  class Member,
  class Hash = std::hash<Member>,
  class KeyEqual = std::equal_to<Member>,
  class Allocator = std::allocator<Member>
  
> class InterningTable {

  typedef std::unordered_set<const Member, Hash, KeyEqual, Allocator> Table;
  Table table;
  
  public:
    const Member* intern(const Member& m) {
      auto minsert = table.insert(m);
      return &*minsert.first;
    }
    
    const Member* intern(const Member *m) {
      if (!m) {
        return NULL;
      }
      return intern(*m);
    }
    
    void clear() {
      table.clear();
    }
};

template <
  class Member,
  class Hash = std::hash<Member>,
  class KeyEqual = std::equal_to<Member>,
  class Allocator = std::allocator<Member>
  
> class IndexedInterningTable {

  typedef std::unordered_set<const Member, Hash, KeyEqual, Allocator> Table;
  typedef std::vector<const Member*> Index;

  Table table;
  Index index;
  
  public:
    const Member* intern(const Member& m) {
      auto msearch = table.find(m);
      if (msearch != table.end()) {
        return &*msearch;
      }
      
      Member n = m;
      n.idx = index.size();
      const Member *intr = &*table.insert(n).first;
      index.push_back(intr);
      return intr;
    }
    
    const Member* intern(const Member *m) {
      if (!m) {
        return NULL;
      }
      return intern(*m);
    }
    
    const Member* at(unsigned idx) {
      return index.at(idx);
    }
    
    void clear() {
      table.clear();
      index.clear();
    }
    
    const Index* getIndex() const {
      return &index;
    }
};

template <class Member> class IndexedTable {

  public:
    typedef std::vector<Member*> Index;

  private:
    typedef std::unordered_map<Member*, unsigned> Table;
    Table table;
    Index index;
  
  public:
    unsigned indexOf(Member* m) {
      auto msearch = table.find(m);
      if (msearch != table.end()) {
        return msearch->second;
      }
      unsigned idx = index.size();
      index.push_back(m);
      table.insert({m, idx});
      return idx;
    }
    
    Member* at(unsigned idx) {
      return index.at(idx);
    }
    
    void clear() {
      table.clear();
      index.clear();
    }
    
    const Index& getIndex() const {
      return index;
    }
    
    size_t size() {
      return index.size();
    }
};

#endif
