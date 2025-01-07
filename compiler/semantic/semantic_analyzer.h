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

    /**
     * @brief 访问 break 语句
     * @param stmt break 语句节点
     * @throw SemanticError 当在循环外使用 break 时
     */
    void visitBreak(const BreakStmt& stmt) override;

    /**
     * @brief 访问 continue 语句
     * @param stmt continue 语句节点
     * @throw SemanticError 当在循环外使用 continue 时
     */
    void visitContinue(const ContinueStmt& stmt) override;

private:
    // 类型检查辅助方法
    TokenType check_type(const Expr& expr);
    bool is_numeric_type(TokenType type) const;
    bool is_compatible_type(TokenType expected, TokenType actual) const;
    void ensure_boolean(const Expr& expr, const std::string& context);

    // 当前分析状态
    Symbol* current_function_ = nullptr;  ///< 当前正在分析的函数
    SymbolTable symbols_;                 ///< 符号表
    TokenType current_type_ = TokenType::INVALID;  ///< 当前表达式的类型
    size_t loop_depth_ = 0;              ///< 当前循环嵌套深度
    bool has_return_ = false;            ///< 标记当前函数是否有返回值

    // 类型检查和转换
    bool is_bit_type(TokenType type) const;
    /**
     * @brief 检查类型是否可以进行字符串连接操作
     * @param type 要检查的类型
     * @return 如果类型可以进行字符串连接则返回 true
     */
    bool is_string_concatenable(TokenType type) const;

    /**
     * @brief 检查类型是否支持比较运算
     * @param type 要检查的类型
     * @return 如果类型支持比较运算则返回 true
     */
    bool is_ordered_type(TokenType type) const;
    bool is_comparable_type(TokenType left, TokenType right) const;
    bool is_numeric_convertible(TokenType type) const;
    bool is_string_convertible(TokenType type) const;

    /**
     * @brief 检查两个类型是否可以进行隐式转换
     * @param from 源类型
     * @param to 目标类型
     * @return 如果可以进行隐式转换则返回 true
     */
    bool can_implicit_convert(TokenType from, TokenType to) const;

    /**
     * @brief 获取两个类型的共同类型
     * @param t1 第一个类型
     * @param t2 第二个类型
     * @return 共同类型，如果不存在则返回 INVALID
     */
    TokenType common_type(TokenType t1, TokenType t2) const;

    // 辅助方法
    bool in_loop() const { return loop_depth_ > 0; }

    /**
     * @brief 将类型转换为字符串表示
     * @param type 要转换的类型
     * @return 类型的字符串表示
     */
    std::string type_to_string(TokenType type) const;

    /**
     * @brief 检查函数签名是否相同
     * @param func1 已定义的函数符号
     * @param func2 新的函数声明
     * @return 如果签名相同返回 true
     */
    bool is_same_signature(const Symbol& func1, const FunctionStmt& func2);

    /**
     * @brief 查找最匹配的重载函数
     * @param overloads 所有可能的重载函数
     * @param arg_types 实际参数类型列表
     * @param error_location 错误位置信息
     * @return 最匹配的函数符号指针，如果没有匹配返回 nullptr
     */
    Symbol* find_best_overload(
        const std::vector<Symbol*>& overloads,
        const std::vector<TokenType>& arg_types,
        const Token& error_location);

    /**
     * @brief 计算函数重载的匹配得分
     * @param func 函数符号
     * @param arg_types 实际参数类型列表
     * @return 匹配得分，-1 表示不匹配
     */
    int calculate_overload_score(const Symbol& func, const std::vector<TokenType>& arg_types);
};

} // namespace collie

#endif // COLLIE_SEMANTIC_ANALYZER_H