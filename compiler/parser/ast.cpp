/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "ast.h"

namespace collie {

void LiteralExpr::accept(ExprVisitor& visitor) const {
    visitor.visitLiteral(*this);
}

void IdentifierExpr::accept(ExprVisitor& visitor) const {
    visitor.visitIdentifier(*this);
}

void BinaryExpr::accept(ExprVisitor& visitor) const {
    visitor.visitBinary(*this);
}

void UnaryExpr::accept(ExprVisitor& visitor) const {
    visitor.visitUnary(*this);
}

void ExpressionStmt::accept(StmtVisitor& visitor) const {
    visitor.visitExpression(*this);
}

void VarDeclStmt::accept(StmtVisitor& visitor) const {
    visitor.visitVarDecl(*this);
}

void BlockStmt::accept(StmtVisitor& visitor) const {
    visitor.visitBlock(*this);
}

void AssignExpr::accept(ExprVisitor& visitor) const {
    visitor.visitAssign(*this);
}

void IfStmt::accept(StmtVisitor& visitor) const {
    visitor.visitIf(*this);
}

void WhileStmt::accept(StmtVisitor& visitor) const {
    visitor.visitWhile(*this);
}

void ForStmt::accept(StmtVisitor& visitor) const {
    visitor.visitFor(*this);
}

void CallExpr::accept(ExprVisitor& visitor) const {
    visitor.visitCall(*this);
}

void FunctionStmt::accept(StmtVisitor& visitor) const {
    visitor.visitFunction(*this);
}

FunctionStmt::FunctionStmt(Token type, Token name,
                         std::vector<Parameter> parameters,
                         std::unique_ptr<BlockStmt> body)
    : return_type_(type),
      name_(name),
      parameters_(std::move(parameters)),
      body_(std::move(body)) {}

CallExpr::CallExpr(std::unique_ptr<Expr> callee,
                   Token paren,
                   std::vector<std::unique_ptr<Expr>> arguments)
    : callee_(std::move(callee)),
      paren_(paren),
      arguments_(std::move(arguments)) {}

IfStmt::IfStmt(std::unique_ptr<Expr> condition,
               std::unique_ptr<Stmt> thenBranch,
               std::unique_ptr<Stmt> elseBranch)
    : condition_(std::move(condition)),
      then_branch_(std::move(thenBranch)),
      else_branch_(std::move(elseBranch)) {}

WhileStmt::WhileStmt(std::unique_ptr<Expr> condition,
                     std::unique_ptr<Stmt> body)
    : condition_(std::move(condition)),
      body_(std::move(body)) {}

ForStmt::ForStmt(std::unique_ptr<Stmt> initializer,
                 std::unique_ptr<Expr> condition,
                 std::unique_ptr<Expr> increment,
                 std::unique_ptr<Stmt> body)
    : initializer_(std::move(initializer)),
      condition_(std::move(condition)),
      increment_(std::move(increment)),
      body_(std::move(body)) {}

} // namespace collie