/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语义分析器的定义，负责类型检查和语义错误检测
 */
#ifndef COLLIE_SEMANTIC_ANALYZER_H
#define COLLIE_SEMANTIC_ANALYZER_H

#include <memory>
#include <string>
#include <stdexcept>
#include "symbol_table.h"
#include "../parser/ast.h"

namespace collie {

// 语义错误异常
class SemanticError : public std::runtime_error {
public:
    SemanticError(const std::string& message, size_t line, size_t column)
        : std::runtime_error(message), line_(line), column_(column) {
        error_message_ = "Line " + std::to_string(line) +
                        ", Column " + std::to_string(column) +
                        ": " + message;
    }

    const char* what() const noexcept override {
        return error_message_.c_str();
    }

    size_t line() const { return line_; }
    size_t column() const { return column_; }

private:
    size_t line_;
    size_t column_;
    std::string error_message_;
};

// 语义分析器
class SemanticAnalyzer : public ExprVisitor, public StmtVisitor {
public:
    SemanticAnalyzer() = default;

    // 分析入口
    void analyze(const std::vector<std::unique_ptr<Stmt>>& statements);

    // 表达式访问方法
    void visitLiteral(const LiteralExpr& expr) override;
    void visitIdentifier(const IdentifierExpr& expr) override;
    void visitBinary(const BinaryExpr& expr) override;
    void visitUnary(const UnaryExpr& expr) override;
    void visitAssign(const AssignExpr& expr) override;
    void visitCall(const CallExpr& expr) override;

    // 语句访问方法
    void visitExpression(const ExpressionStmt& stmt) override;
    void visitVarDecl(const VarDeclStmt& stmt) override;
    void visitBlock(const BlockStmt& stmt) override;
    void visitIf(const IfStmt& stmt) override;
    void visitWhile(const WhileStmt& stmt) override;
    void visitFor(const ForStmt& stmt) override;
    void visitFunction(const FunctionStmt& stmt) override;
    void visitReturn(const ReturnStmt& stmt) override;

private:
    // 类型检查辅助方法
    TokenType check_type(const Expr& expr);
    bool is_numeric_type(TokenType type) const;
    bool is_compatible_type(TokenType left, TokenType right) const;
    void ensure_boolean(const Expr& expr, const std::string& context);

    // 当前分析的函数（用于return语句检查）
    Symbol* current_function_ = nullptr;
    // 符号表
    SymbolTable symbols_;
    // 当前表达式的类型
    TokenType current_type_ = TokenType::INVALID;

    bool is_bit_type(TokenType type) const;

    // 类型检查辅助方法
    bool is_string_concatenable(TokenType type) const;
    bool is_ordered_type(TokenType type) const;
    bool is_comparable_type(TokenType left, TokenType right) const;

    // 类型转换和推导
    bool can_implicit_convert(TokenType from, TokenType to) const;
    TokenType common_type(TokenType t1, TokenType t2) const;
    bool is_numeric_convertible(TokenType type) const;
    bool is_string_convertible(TokenType type) const;
};

} // namespace collie

#endif // COLLIE_SEMANTIC_ANALYZER_H