#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/IRBuilder.h"
#include <fstream> 
#include <iostream> 
#include <string>   
#include <vector>  
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <iostream>
#include "AST/AST.h"

using namespace std;
using namespace llvm;
static const char *FName="./test.sol";
static ifstream *in;

enum Token {
  tok_eof = -1,
  tok_function = -2, tok_extern = -3,
  tok_identifier = -4, tok_number = -5,

  tok_contract = -6
};

static std::string IdentifierStr; 
static double NumVal;             

static int gettok(){
    static int LastChar = ' ';

    while (isspace(LastChar))
        LastChar = char(in->get());

    if (isalpha(LastChar)) { 
        IdentifierStr = LastChar;
        while (isalnum((LastChar = char(in->get()))))
        IdentifierStr += LastChar;

        if (IdentifierStr == "function") return tok_function;
        if (IdentifierStr == "contract")  return tok_contract;
        return tok_identifier;
    }

    if (isdigit(LastChar) || LastChar == '.') { 
        std::string NumStr;
        do {
        NumStr += LastChar;
        LastChar = char(in->get());
        } while (isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(), 0);
        return tok_number;
    }

    if (LastChar == '#') {
        do LastChar = char(in->get());
        while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
        
        if (LastChar != EOF)
        return gettok();
    }
    
    if (LastChar == EOF)
        return tok_eof;

    int ThisChar = LastChar;
    LastChar = char(in->get());
    return ThisChar;
}

class ExprAST: public NodeAST {
public:
  virtual ~ExprAST() {}
  virtual Value *Codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double val) : Val(val) {}
  virtual Value *Codegen();
};

class VariableExprAST : public ExprAST {
  std::string Name;
public:
  VariableExprAST(const std::string &name) : Name(name) {}
  virtual Value *Codegen();
};

class BinaryExprAST : public ExprAST {
  char Op;
  ExprAST *LHS, *RHS;
public:
  BinaryExprAST(char op, ExprAST *lhs, ExprAST *rhs) 
    : Op(op), LHS(lhs), RHS(rhs) {}
  virtual Value *Codegen();
};

class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<ExprAST*> Args;
public:
  CallExprAST(const std::string &callee, std::vector<ExprAST*> &args)
    : Callee(callee), Args(args) {}
  virtual Value *Codegen();
};

class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
public:
  PrototypeAST(const std::string &name, const std::vector<std::string> &args)
    : Name(name), Args(args) {}
  Function *Codegen();
};

class BlockAST {
    std::vector<ExprAST*> *Exprs;
public:
    BlockAST(std::vector<ExprAST*> *exprs) : Exprs(exprs) {}
    Function *Codegen();
};

class FunctionAST : public NodeAST {
  PrototypeAST *Proto;
  ExprAST *Body;
public:
  FunctionAST(PrototypeAST *proto, ExprAST *body)
    : Proto(proto), Body(body) {}  
  Function *Codegen();
};

static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}

static std::map<char, int> BinopPrecedence;

static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;
  
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}

ExprAST *ErrorBase(const char *Str) { fprintf(stderr, "Error: %s\n", Str);return 0;}
PrototypeAST *ErrorP(const char *Str) { ErrorBase(Str); return 0; }
FunctionAST *ErrorF(const char *Str) { ErrorBase(Str); return 0; }
ContractAST *ErrorC(const char *Str) { ErrorBase(Str); return 0; }

static ExprAST *ParseExpression();

static ExprAST *ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  
  getNextToken();  
  
  if (CurTok != '(') {
    return new VariableExprAST(IdName);
  }
  
  getNextToken();  
  std::vector<ExprAST*> Args;
  if (CurTok != ')') {
    while (1) {
      ExprAST *Arg = ParseExpression();
      if (!Arg) return 0;
      Args.push_back(Arg);

      if (CurTok == ')') break;

      if (CurTok != ',')
        return ErrorBase("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  getNextToken();
  
  return new CallExprAST(IdName, Args);
}

static ExprAST *ParseNumberExpr() {
  ExprAST *Result = new NumberExprAST(NumVal);
  getNextToken();
  return Result;
}

static ExprAST *ParseParenExpr() {
  getNextToken();  
  ExprAST *V = ParseExpression();
  if (!V) return 0;
  
  if (CurTok != ')')
    return ErrorBase("expected ')'");
  getNextToken();  
  return V;
}

static ExprAST *ParsePrimary() {
  switch (CurTok) {
  default: return ErrorBase("unknown token when expecting an expression");
  case tok_identifier: return ParseIdentifierExpr();
  case tok_number:     return ParseNumberExpr();
  case '(':            return ParseParenExpr();
  case '}': return 0;
  }
}

static ExprAST *ParseBinOpRHS(int ExprPrec, ExprAST *LHS) {
  while (1) {
    int TokPrec = GetTokPrecedence();
    if (TokPrec < ExprPrec)
      return LHS;
    int BinOp = CurTok;
    getNextToken();  // eat binop
    
    ExprAST *RHS = ParsePrimary();
    if (!RHS) return 0;
    
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec+1, RHS);
      if (RHS == 0) return 0;
    }
    
    LHS = new BinaryExprAST(BinOp, LHS, RHS);
  }
}

static ExprAST *ParseExpression() {
  ExprAST *LHS = ParsePrimary();
  if (!LHS) return 0;
  
  return ParseBinOpRHS(0, LHS);
}

static ExprAST *ParseBlock() {

}

static PrototypeAST *ParsePrototype() {
  if (CurTok != tok_identifier)
    return ErrorP("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken();
  
  if (CurTok != '(')
    return ErrorP("Expected '(' in prototype");

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return ErrorP("Expected ')' in prototype");
  
  getNextToken(); 
  
  return new PrototypeAST(FnName, ArgNames);
}

static FunctionAST *ParseDefinition() {
  getNextToken();
  PrototypeAST *Proto = ParsePrototype();
  if (Proto == 0) return 0;

    if (CurTok != '{')
        return ErrorF("Expected '{' in function");
    getNextToken();

  ExprAST *E = ParseExpression();

  if (CurTok != '}')
    return ErrorF("Expected '}' in function");
getNextToken();

    return new FunctionAST(Proto, E);
}

static FunctionAST *ParseTopLevelExpr() {

  if (ExprAST *E = ParseExpression()) {
    PrototypeAST *Proto = new PrototypeAST("", std::vector<std::string>());
    return new FunctionAST(Proto, E);
  }
  return 0;
}

static PrototypeAST *ParseExtern() {
  getNextToken();
  return ParsePrototype();
}

static ContractAST *ParseContract() {
  getNextToken();
  if (CurTok != tok_identifier)
    return ErrorC("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken();

  if (CurTok != '{')
    return ErrorC("Expected '{' in function");
  getNextToken();

  std::vector<NodeAST*> exprs;
  NodeAST *ex;
  while((ex = ParseDefinition()) != 0){
      cerr << "1";
      exprs.push_back(ex);
  }

  if (CurTok != '}') {
    cerr << CurTok;
    return ErrorC("Expected '}' in function");
  }
  getNextToken();
}

static Module *TheModule;
static LLVMContext MyGlobalContext;
static IRBuilder<> Builder(MyGlobalContext);
static std::map<std::string, Value*> NamedValues;

Value *ErrorV(const char *Str) { ErrorBase(Str); return 0; }

Value *NumberExprAST::Codegen() {
  return ConstantFP::get(MyGlobalContext, APFloat(Val));
}

Value *VariableExprAST::Codegen() {
  Value *V = NamedValues[Name];
  return V ? V : ErrorV("Unknown variable name");
}

Value *BinaryExprAST::Codegen() {
  Value *L = LHS->Codegen();
  Value *R = RHS->Codegen();
  if (L == 0 || R == 0) return 0;
  
  switch (Op) {
  case '+': return Builder.CreateFAdd(L, R, "addtmp");
  case '-': return Builder.CreateFSub(L, R, "subtmp");
  case '*': return Builder.CreateFMul(L, R, "multmp");
  case '<':
    L = Builder.CreateFCmpULT(L, R, "cmptmp");
    return Builder.CreateUIToFP(L, Type::getDoubleTy(MyGlobalContext),
                                "booltmp");
  default: return ErrorV("invalid binary operator");
  }
}

Value *CallExprAST::Codegen() {
  Function *CalleeF = TheModule->getFunction(Callee);
  if (CalleeF == 0)
    return ErrorV("Unknown function referenced");
  
  if (CalleeF->arg_size() != Args.size())
    return ErrorV("Incorrect # arguments passed");

  std::vector<Value*> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->Codegen());
    if (ArgsV.back() == 0) return 0;
  }
  
  return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::Codegen() {
  const std::vector<const Type*> Doubles(Args.size(),
                                   Type::getDoubleTy(MyGlobalContext));
  ArrayRef<Type*> DoublesArr;
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(MyGlobalContext),
                                       DoublesArr, false);
  
  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule);
  
  if (F->getName() != Name) {
    F->eraseFromParent();
    F = TheModule->getFunction(Name);
    
    if (!F->empty()) {
      ErrorF("redefinition of function");
      return 0;
    }
    
    if (F->arg_size() != Args.size()) {
      ErrorF("redefinition of function with different # args");
      return 0;
    }
  }
  
  unsigned Idx = 0;
  for (Function::arg_iterator AI = F->arg_begin(); Idx != Args.size();
       ++AI, ++Idx) {
    cerr << Args[Idx];
    AI->setName(Args[Idx]);
    
    NamedValues[Args[Idx]] = AI;
  }
  
  return F;
}

Function *FunctionAST::Codegen() {
  NamedValues.clear();
  
  Function *TheFunction = Proto->Codegen();
  if (TheFunction == 0)
    return 0;
  
  BasicBlock *BB = BasicBlock::Create(MyGlobalContext, "entry", TheFunction);
  Builder.SetInsertPoint(BB);
  
  if (Body == nullptr) {
    Builder.CreateRetVoid();

    verifyFunction(*TheFunction);

    return TheFunction;
  }

  if (Value *RetVal = Body->Codegen()) {
    Builder.CreateRetVoid();

    verifyFunction(*TheFunction);

    return TheFunction;
  }
  
  TheFunction->eraseFromParent();
  return 0;
}

static void HandleDefinition() {
  if (FunctionAST *F = ParseDefinition()) {
    if (Function *LF = F->Codegen()) {
      fprintf(stderr, "Read function definition:");
      LF->dump();
    }
  } else {
    getNextToken();
  }
}

static void HandleContract() {
  if (ParseContract()) {
    fprintf(stderr, "Parsed a cotract definition.\n");
  } else {
    getNextToken();
  }
}



static void HandleExtern() {
  if (PrototypeAST *P = ParseExtern()) {
    if (Function *F = P->Codegen()) {
      fprintf(stderr, "Read extern: ");
      F->dump();
    }
  } else {
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  if (FunctionAST *F = ParseTopLevelExpr()) {
    if (Function *LF = F->Codegen()) {
      fprintf(stderr, "Read top-level expression:");
      LF->dump();
    }
  } else {
    getNextToken();
  }
}

static void MainLoop() {
  while (1) {
    switch (CurTok) {
    case tok_eof:    return;
    case ';':        getNextToken(); break; 
    case tok_function:    HandleDefinition(); break;
    case tok_extern: HandleExtern(); break;
    case tok_contract: HandleContract(); break;
    default:         HandleTopLevelExpression(); break;
    }
  }
}

int main() {
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;  // highest.

  in = new ifstream(FName);
  getNextToken();

  TheModule = new Module("Contract", MyGlobalContext);
  MainLoop();

  in->close();
  TheModule->dump();

  return 0;
}
