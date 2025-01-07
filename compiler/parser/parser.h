/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语法分析器的定义，负责将token序列解析为AST
 */
#ifndef COLLIE_PARSER_H
#define COLLIE_PARSER_H

#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include "../lexer/token.h"
#include "ast.h"

namespace collie {

// 前向声明
class Type;
class Expr;
class Stmt;
class Token;
class TupleType;
class TupleExpr;

/**
 * @brief 语法错误异常类
 * 解析过程中遇到语法错误时抛出
 */
class ParseError : public std::runtime_error {
public:
    /**
     * @brief 构造语法错误异常
     * @param message 错误信息
     * @param line 错误所在行
     * @param column 错误所在列
     */
    ParseError(const std::string& message, size_t line, size_t column)
        : std::runtime_error(message), line_(line), column_(column) {}

    size_t line() const { return line_; }
    size_t column() const { return column_; }

private:
    size_t line_;
    size_t column_;
};

/**
 * @brief 语法分析器类
 */
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens)
        : tokens_(tokens), current_(0), nesting_depth_(0), in_panic_mode_(false) {}

    /**
     * @brief 解析程序
     * @return AST根节点列表
     */
    std::vector<std::unique_ptr<Stmt>> parse_program();

    /**
     * @brief 解析单个语句（用于测试）
     * @return 语句的AST节点
     */
    std::unique_ptr<Stmt> parse() {
        return parse_declaration();
    }

    /**
     * @brief 获取解析过程中的错误
     * @return 错误列表
     */
    const std::vector<ParseError>& get_errors() const { return errors_; }

private:
    // 类型解析方法
    std::unique_ptr<Type> parse_type();

    // 语句解析方法
    /**
     * @brief 解析声明语句
     * @return 声明语句的AST节点
     * @throws ParseError 如果声明语法不正确
     */
    std::unique_ptr<Stmt> parse_declaration();

    /**
     * @brief 解析变量声明
     * @return 变量声明的AST节点
     * @throws ParseError 如果变量声明语法不正确
     */
    std::unique_ptr<Stmt> parse_var_declaration();

    /**
     * @brief 解析函数声明
     * @return 函数声明的AST节点
     * @throws ParseError 如果函数声明语法不正确
     */
    std::unique_ptr<Stmt> parse_function_declaration();

    /**
     * @brief 解析语句
     * @return 语句的AST节点
     * @throws ParseError 如果语句语法不正确
     *
     * 可以解析的语句类型：
     * - if 语句
     * - while 循环
     * - for 循环
     * - return 语句
     * - break 语句
     * - continue 语句
     * - 块语句
     * - 表达式语句
     */
    std::unique_ptr<Stmt> parse_statement();

    /**
     * @brief 解析if语句
     * @return if语句的AST节点
     * @throws ParseError 如果if语句语法不正确
     *
     * if语句语法：
     * if (condition) thenBranch [else elseBranch]?
     */
    std::unique_ptr<Stmt> parse_if_statement();

    /**
     * @brief 解析while循环语句
     * @return while语句的AST节点
     * @throws ParseError 如果while语句语法不正确
     *
     * while语句语法：
     * while (condition) body
     */
    std::unique_ptr<Stmt> parse_while_statement();

    /**
     * @brief 解析for循环语句
     * @return for语句的AST节点
     * @throws ParseError 如果for语句语法不正确
     *
     * for语句语法：
     * for (initializer; condition; increment) body
     */
    std::unique_ptr<Stmt> parse_for_statement();

    /**
     * @brief 解析块语句
     * @return 块语句的AST节点
     * @throws ParseError 如果块语句语法不正确
     *
     * 块语句语法：
     * { statements* }
     */
    std::unique_ptr<Stmt> parse_block_statement();

    /**
     * @brief 解析return语句
     * @return return语句的AST节点
     * @throws ParseError 如果return语句语法不正确
     *
     * return语句语法：
     * return [expression]? ;
     */
    std::unique_ptr<Stmt> parse_return_statement();

    /**
     * @brief 解析break语句
     * @return break语句的AST节点
     * @throws ParseError 如果break语句语法不正确或在循环外使用
     */
    std::unique_ptr<Stmt> parse_break_statement();

    /**
     * @brief 解析continue语句
     * @return continue语句的AST节点
     * @throws ParseError 如果continue语句语法不正确或在循环外使用
     */
    std::unique_ptr<Stmt> parse_continue_statement();

    /**
     * @brief 解析表达式语句
     * @return 表达式语句的AST节点
     * @throws ParseError 如果表达式语句语法不正确
     *
     * 表达式语句语法：
     * expression ;
     */
    std::unique_ptr<Stmt> parse_expression_statement();

    // 表达式解析方法
    /**
     * @brief 解析表达式
     * @return 表达式的AST节点
     * @throws ParseError 如果表达式语法不正确
     *
     * 表达式解析遵循运算符优先级规则：
     * 1. 赋值
     * 2. 逻辑或
     * 3. 逻辑与
     * 4. 相等性比较
     * 5. 关系比较
     * 6. 加减
     * 7. 乘除
     * 8. 一元运算
     * 9. 基本表达式
     */
    std::unique_ptr<Expr> parse_expression();

    /**
     * @brief 解析赋值表达式
     * @return 赋值表达式的AST节点
     * @throws ParseError 如果赋值表达式语法不正确
     *
     * 赋值表达式语法：
     * IDENTIFIER "=" assignment
     */
    std::unique_ptr<Expr> parse_assignment();

    /**
     * @brief 解析逻辑或表达式
     * @return 逻辑或表达式的AST节点
     * @throws ParseError 如果逻辑或表达式语法不正确
     *
     * 逻辑或表达式语法：
     * logicalAnd ("||" logicalAnd)*
     */
    std::unique_ptr<Expr> parse_logical_or();

    /**
     * @brief 解析逻辑与表达式
     * @return 逻辑与表达式的AST节点
     * @throws ParseError 如果逻辑与表达式语法不正确
     *
     * 逻辑与表达式语法：
     * equality ("&&" equality)*
     */
    std::unique_ptr<Expr> parse_logical_and();

    /**
     * @brief 解析相等性比较表达式
     * @return 相等性比较表达式的AST节点
     * @throws ParseError 如果相等性比较表达式语法不正确
     *
     * 相等性比较表达式语法：
     * comparison (("==" | "!=") comparison)*
     */
    std::unique_ptr<Expr> parse_equality();

    /**
     * @brief 解析关系比较表达式
     * @return 关系比较表达式的AST节点
     * @throws ParseError 如果关系比较表达式语法不正确
     *
     * 关系比较表达式语法：
     * term ((">" | ">=" | "<" | "<=") term)*
     */
    std::unique_ptr<Expr> parse_comparison();

    /**
     * @brief 解析加减表达式
     * @return 加减表达式的AST节点
     * @throws ParseError 如果加减表达式语法不正确
     *
     * 加减表达式语法：
     * factor (("+" | "-") factor)*
     */
    std::unique_ptr<Expr> parse_term();

    /**
     * @brief 解析乘除表达式
     * @return 乘除表达式的AST节点
     * @throws ParseError 如果乘除表达式语法不正确
     *
     * 乘除表达式语法：
     * unary (("*" | "/" | "%") unary)*
     */
    std::unique_ptr<Expr> parse_factor();

    /**
     * @brief 解析一元表达式
     * @return 一元表达式的AST节点
     * @throws ParseError 如果一元表达式语法不正确
     *
     * 一元表达式语法：
     * ("!" | "-") unary | primary
     */
    std::unique_ptr<Expr> parse_unary();

    /**
     * @brief 解析基本表达式
     * @return 基本表达式的AST节点
     * @throws ParseError 如果基本表达式语法不正确
     *
     * 基本表达式语法：
     * NUMBER | STRING | BOOL | IDENTIFIER | "(" expression ")"
     */
    std::unique_ptr<Expr> parse_primary();

    /**
     * @brief 完成函数调用的解析
     * @param callee 函数名token
     * @return 函数调用表达式的AST节点
     * @throws ParseError 如果函数调用语法不正确
     *
     * 函数调用语法：
     * IDENTIFIER "(" arguments? ")"
     */
    std::unique_ptr<Expr> finish_call(const Token& callee);

    // 错误处理辅助方法
    /**
     * 处理解析错误
     */
    void handle_error(const ParseError& error);

    /**
     * @brief 记录解析错误
     * @param error 要记录的错误
     */
    void report_error(const ParseError& error);

    /**
     * @brief 进行错误恢复
     *
     * 尝试找到下一个安全的同步点，以便继续解析。
     * 同步点包括：
     * - 语句结束（分号）
     * - 块结束（右花括号）
     * - 新的声明开始
     */
    void panic_mode_error_recovery();

    /**
     * @brief 检查是否在语句边界
     * @return 如果当前位置是语句边界返回true
     */
    bool is_statement_boundary() const;

    /**
     * @brief 检查是否在声明边界
     * @return 如果当前位置是声明边界返回true
     */
    bool is_declaration_boundary() const;

    /**
     * @brief 格式化错误消息
     * @param token 错误发生的token
     * @param message 错误消息
     * @return 格式化后的错误消息字符串
     */
    std::string format_error_message(const Token& token, const std::string& message) const;

    /**
     * @brief 创建解析错误
     * @param token 错误发生的token
     * @param message 错误消息
     * @return ParseError对象
     */
    ParseError error(const Token& token, const std::string& message);

    /**
     * @brief 同步到下一个安全点
     *
     * 跳过当前错误的语句，直到找到一个可以继续解析的位置
     */
    void synchronize();

    /**
     * @brief 同步到指定类型的token
     * @param type 目标token类型
     */
    void synchronize_until(TokenType type);

    // Token 管理辅助方法
    /**
     * @brief 检查是否到达输入结束
     * @return 如果到达输入结束返回true
     */
    bool is_at_end() const;

    /**
     * @brief 获取当前token
     * @return 当前token
     */
    Token peek() const;

    /**
     * @brief 获取前一个token
     * @return 前一个token
     */
    Token previous() const;

    /**
     * @brief 前进到下一个token
     * @return 前一个token
     */
    Token advance();

    /**
     * @brief 匹配当前token类型
     * @param type 要匹配的类型
     * @return 如果匹配成功返回true并前进到下一个token
     */
    bool match(TokenType type);

    /**
     * @brief 匹配当前token类型（多个类型之一）
     * @param types 要匹配的类型列表
     * @return 如果匹配成功返回true并前进到下一个token
     */
    bool match(std::initializer_list<TokenType> types);

    /**
     * @brief 消费指定类型的token
     * @param type 期望的token类型
     * @param message 错误消息
     * @return 消费的token
     * @throws ParseError 如果当前token类型不匹配
     */
    Token consume(TokenType type, const std::string& message);

    /**
     * @brief 检查嵌套深度是否超过限制
     * @throws ParseError 如果超过最大嵌套深度
     */
    void check_max_nesting_depth();

    /// @brief 解析元组类型
    std::unique_ptr<Type> parse_tuple_type();

    /// @brief 解析元组表达式
    std::unique_ptr<Expr> parse_tuple_expr();

    /// @brief 解析后缀表达式（包括元组成员访问）
    std::unique_ptr<Expr> parse_postfix();

    /**
     * @brief 检查当前 token 是否匹配预期类型
     * @param type 预期的 token 类型
     * @return 如果匹配返回 true
     */
    bool check(TokenType type) const;

    /**
     * @brief 判断是否是字面量 token
     * @param token 要判断的 token
     * @return 如果是字面量返回 true
     */
    bool is_literal_token(const Token& token) const;

private:
    const std::vector<Token>& tokens_;
    size_t current_;
    size_t nesting_depth_;
    bool in_panic_mode_;  // 是否处于恐慌模式
    std::vector<ParseError> errors_;

    static constexpr size_t MAX_NESTING_DEPTH = 256;
};

} // namespace collie

#endif // COLLIE_PARSER_H