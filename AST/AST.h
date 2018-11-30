#ifndef AST_H
#define AST_H
#include "llvm/IR/DerivedTypes.h"
using namespace llvm;
class NodeAST {
  public:
    virtual ~NodeAST() {}
    virtual Value *Codegen() = 0;
};

class Scopable {
  public:
    virtual ~Scopable() = default;
    NodeAST const* scope() const { return m_scope; }
    void setScope(NodeAST const* _scope) { 
      m_scope = _scope;
    }
  protected:
    NodeAST const* m_scope = nullptr;
};

class Declaration: public NodeAST, public Scopable {
  public:
    virtual ~Declaration() {};
    char* getName() const { return m_name; }
  protected:
    char* m_name;
};

class ContractAST: public Declaration {
  public: 
    ContractAST(
      std::vector<NodeAST*> const& _subNodes
    ): 
      m_subNodes(_subNodes) 
    {}
    std::vector<NodeAST*> const& subNodes() const { return m_subNodes; }
  private:
    std::vector<NodeAST*> m_subNodes;
};

#endif