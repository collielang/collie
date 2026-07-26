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
#include <unordered_map>
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
 * if / while / for、break / continue、用户自定义函数（声明/调用/return）、
 * class 基础（字段/方法/构造器/new/this），以及内建函数 print。
 *
 * 暂不支持（会抛 RuntimeError）：元组。
 * TODO(interpreter): 后续补齐类型检查/强转、元组、继承等。
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
    void visitArrayLiteral(const ArrayLiteralExpr& expr) override;
    void visitIndex(const IndexExpr& expr) override;
    void visitIndexAssign(const IndexAssignExpr& expr) override;
    void visitMethodCall(const MethodCallExpr& expr) override;
    void visitProperty(const PropertyExpr& expr) override;
    void visitPropertyAssign(const PropertyAssignExpr& expr) override;
    void visitNew(const NewExpr& expr) override;
    void visitThis(const ThisExpr& expr) override;

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

    /// @brief 归一化索引（支持负索引，-1 为末尾），越界抛 RuntimeError
    static size_t normalize_index(const Value& index, size_t size,
                                  const Token& bracket);

    /// @brief 统计 UTF-8 字符串的码点数（而非字节数）
    static size_t utf8_length(const std::string& s);

    /// @brief 取 UTF-8 字符串第 i 个码点（前置：i < utf8_length(s)），返回单字符子串
    static std::string utf8_char_at(const std::string& s, size_t i);

    /// @brief 取第 index 个码点的字节偏移（index >= 码点数时返回 s.size()）
    static size_t utf8_byte_offset(const std::string& s, size_t index);

    // 内建函数
    void call_builtin_print(const CallExpr& expr);
    void call_builtin_len(const CallExpr& expr);
    void call_builtin_to_string(const CallExpr& expr);
    void call_builtin_to_number(const CallExpr& expr);

    /// @brief 把值转为 number（string/bool/number），失败抛 RuntimeError；
    /// toNumber 内建函数与 .toNumber() 方法共用
    static Value to_number_value(const Value& v, size_t line, size_t column);

    /// @brief 在类成员中查找指定名字的方法（含构造器），未找到返回 nullptr
    static const FunctionStmt* find_method(const ClassStmt* klass,
                                           const std::string& name);

    /// @brief 执行类方法/构造器：新作用域内绑定 this 与形参，捕获 return
    Value call_class_method(const Value& instance, const FunctionStmt* method,
                            const std::vector<Value>& args,
                            size_t line, size_t column);

    /// @brief 按声明类型校验/隐式转换值（string ← number/bool 转字符串，
    /// object/类名等动态类型放行），不兼容抛 RuntimeError
    static Value coerce_to_declared(TokenType declared, const Value& value,
                                    size_t line, size_t column);

    std::ostream& out_;
    Environment env_;
    Value result_;  ///< 最近一次表达式求值的结果
    std::unordered_map<std::string, const ClassStmt*> classes_;  ///< 已登记的类
};

} // namespace collie

#endif // COLLIE_INTERPRETER_H
