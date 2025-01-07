/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-07
 * @Description: AST 节点的 accept 方法实现
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

void CallExpr::accept(ExprVisitor& visitor) const {
    visitor.visitCall(*this);
}

void FunctionStmt::accept(StmtVisitor& visitor) const {
    visitor.visitFunction(*this);
}

void ReturnStmt::accept(StmtVisitor& visitor) const {
    visitor.visitReturn(*this);
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

void BreakStmt::accept(StmtVisitor& visitor) const {
    visitor.visitBreak(*this);
}

void ContinueStmt::accept(StmtVisitor& visitor) const {
    visitor.visitContinue(*this);
}

void TupleExpr::accept(ExprVisitor& visitor) const {
    visitor.visitTuple(*this);
}

void TupleMemberExpr::accept(ExprVisitor& visitor) const {
    visitor.visitTupleMember(*this);
}

// 构造函数实现
CallExpr::CallExpr(std::unique_ptr<Expr> callee,
                   Token paren,
                   std::vector<std::unique_ptr<Expr>> arguments)
    : callee_(std::move(callee)),
      paren_(paren),
      arguments_(std::move(arguments)) {}

FunctionStmt::FunctionStmt(Token type, Token name,
                          std::vector<Parameter> parameters,
                          std::unique_ptr<BlockStmt> body)
    : return_type_(type),
      name_(name),
      parameters_(std::move(parameters)),
      body_(std::move(body)) {}

} // namespace collie