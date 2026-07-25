/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2026-07-25
 * @Description: 树遍历解释器（路线 A）
 */
#ifndef COLLIE_INTERPRETER_H
#define COLLIE_INTERPRETER_H

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "value.h"
#include "environment.h"
#include "../parser/ast.h"
#include "../lexer/token.h"

namespace collie {

/**
 * @brief 运行期错误，携带源位置，由调用方（main / 测试）捕获后上报。
 */
class RuntimeError : public std::runtime_error {
public:
    RuntimeError(const std::string& message, size_t line, size_t column)
        : std::runtime_error(message), line_(line), column_(column) {}

    size_t line() const { return line_; }
    size_t column() const { return column_; }

private:
    size_t line_;
    size_t column_;
};

/**
 * @brief 树遍历解释器
 *
 * 支持：字面量（数字/字符串/布尔）、算术/比较/逻辑运算、变量声明与读写、
 * if / while / for、break / continue、用户自定义函数（声明/调用/return），以及内建函数 print。
 *
 * 暂不支持（会抛 RuntimeError）：类、元组。
 * TODO(interpreter): 后续补齐类型检查/强转、元组等。
 */
class Interpreter : public ExprVisitor, public StmtVisitor {
public:
    /// @param out 程序 print 输出的目标流（main 传 std::cout，测试传 ostringstream）
    explicit Interpreter(std::ostream& out) : out_(out) {}

    /// @brief 解释执行整个程序（顶层语句列表）
    void interpret(const std::vector<std::unique_ptr<Stmt>>& statements);

private:
    // ExprVisitor 接口
    void visitLiteral(const LiteralExpr& expr) override;
    void visitIdentifier(const IdentifierExpr& expr) override;
    void visitBinary(const BinaryExpr& expr) override;
    void visitUnary(const UnaryExpr& expr) override;
    void visitAssign(const AssignExpr& expr) override;
    void visitCall(const CallExpr& expr) override;
    void visitTuple(const TupleExpr& expr) override;
    void visitTupleMember(const TupleMemberExpr& expr) override;
    void visitTernary(const TernaryExpr& expr) override;

    // StmtVisitor 接口
    void visitExpression(const ExpressionStmt& stmt) override;
    void visitVarDecl(const VarDeclStmt& stmt) override;
    void visitBlock(const BlockStmt& stmt) override;
    void visitIf(const IfStmt& stmt) override;
    void visitWhile(const WhileStmt& stmt) override;
    void visitFor(const ForStmt& stmt) override;
    void visitDoWhile(const DoWhileStmt& stmt) override;
    void visitSwitch(const SwitchStmt& stmt) override;
    void visitFunction(const FunctionStmt& stmt) override;
    void visitReturn(const ReturnStmt& stmt) override;
    void visitClass(const ClassStmt& stmt) override;
    void visitBreak(const BreakStmt& stmt) override;
    void visitContinue(const ContinueStmt& stmt) override;

    // 求值 / 执行辅助
    Value evaluate(const Expr* expr);
    void execute(const Stmt* stmt);
    void execute_block(const BlockStmt& block);

    Value eval_binary(const Token& op, const Value& left, const Value& right);
    Value eval_arithmetic(const Token& op, const Value& left, const Value& right);
    Value eval_comparison(const Token& op, const Value& left, const Value& right);
    static bool values_equal(const Value& left, const Value& right);

    // 内建函数
    void call_builtin_print(const CallExpr& expr);

    std::ostream& out_;
    Environment env_;
    Value result_;  ///< 最近一次表达式求值的结果
};

} // namespace collie

#endif // COLLIE_INTERPRETER_H
