/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语法分析器的定义，负责将token序列解析为AST
 */
#ifndef COLLIE_PARSER_H
#define COLLIE_PARSER_H

#include <memory>
#include <vector>
#include <stdexcept>
#include "../lexer/lexer.h"
#include "ast.h"

namespace collie {

/**
 * @brief 语法错误异常
 * 用于在解析过程中遇到语法错误时抛出
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
 * 实现递归下降解析算法，将token序列解析为抽象语法树
 */
class Parser {
public:
    /**
     * @brief 构造语法分析器
     * @param lexer 词法分析器的引用
     */
    explicit Parser(Lexer& lexer)
        : lexer_(lexer),
          current_token_(TokenType::INVALID, "", 0, 0),
          previous_token_(TokenType::INVALID, "", 0, 0)
    {
        advance();
    }

    /**
     * @brief 解析入口
     * @return 解析得到的语句
     */
    std::unique_ptr<Stmt> parse();

private:
    // 表达式解析方法
    /**
     * @brief 解析表达式
     * @return 解析得到的表达式节点
     */
    std::unique_ptr<Expr> expression();

    /**
     * @brief 解析赋值表达式
     * @return 解析得到的赋值表达式节点
     */
    std::unique_ptr<Expr> assignment();

    /**
     * @brief 解析逻辑或表达式
     * @return 解析得到的逻辑或表达式节点
     */
    std::unique_ptr<Expr> logical_or();

    /**
     * @brief 解析逻辑与表达式
     * @return 解析得到的逻辑与表达式节点
     */
    std::unique_ptr<Expr> logical_and();

    /**
     * @brief 解析相等表达式
     * @return 解析得到的相等表达式节点
     */
    std::unique_ptr<Expr> equality();

    /**
     * @brief 解析比较表达式
     * @return 解析得到的比较表达式节点
     */
    std::unique_ptr<Expr> comparison();

    /**
     * @brief 解析项表达式
     * @return 解析得到的项表达式节点
     */
    std::unique_ptr<Expr> term();

    /**
     * @brief 解析因子表达式
     * @return 解析得到的因子表达式节点
     */
    std::unique_ptr<Expr> factor();

    /**
     * @brief 解析一元表达式
     * @return 解析得到的一元表达式节点
     */
    std::unique_ptr<Expr> unary();

    /**
     * @brief 解析基础表达式
     * @return 解析得到的基础表达式节点
     */
    std::unique_ptr<Expr> primary();

    // 语句解析方法
    /**
     * @brief 解析声明语句
     * @return 解析得到的声明语句节点
     */
    std::unique_ptr<Stmt> declaration();

    /**
     * @brief 解析变量声明
     * @return 解析得到的变量声明节点
     */
    std::unique_ptr<Stmt> var_declaration(Token type, Token name);

    /**
     * @brief 解析普通语句
     * @return 解析得到的普通语句节点
     */
    std::unique_ptr<Stmt> statement();

    /**
     * @brief 解析表达式语句
     * @return 解析得到的表达式语句节点
     */
    std::unique_ptr<Stmt> expression_statement();

    /**
     * @brief 解析块语句
     * @return 解析得到的块语句节点
     */
    std::unique_ptr<Stmt> block();

    /**
     * @brief 解析块内语句列表
     * @return 解析得到的块内语句列表
     */
    std::vector<std::unique_ptr<Stmt>> block_statements();

    /**
     * @brief 解析if语句
     * @return 解析得到的if语句节点
     */
    std::unique_ptr<Stmt> if_statement();

    /**
     * @brief 解析while语句
     * @return 解析得到的while语句节点
     */
    std::unique_ptr<Stmt> while_statement();

    /**
     * @brief 解析for语句
     * @return 解析得到的for语句节点
     */
    std::unique_ptr<Stmt> for_statement();

    // 函数相关
    /**
     * @brief 解析函数声明
     * @return 解析得到的函数声明节点
     */
    std::unique_ptr<Stmt> function_declaration(Token type, Token name);

    /**
     * @brief 解析函数参数列表
     * @return 解析得到的参数列表
     */
    std::vector<Parameter> parameters();

    /**
     * @brief 完成函数调用的解析
     * @param callee 被调用的函数表达式
     * @return 解析得到的函数调用节点
     */
    std::unique_ptr<Expr> finish_call(std::unique_ptr<Expr> callee);

    // 辅助方法
    /**
     * @brief 消费一个指定类型的token
     * @param type 期望的token类型
     * @param message 错误信息
     * @return 消费的token
     * @throw ParseError 如果当前token类型不匹配
     */
    Token consume(TokenType type, const std::string& message);

    /**
     * @brief 匹配一个指定类型的token
     * @param type 期望的token类型
     * @return 如果当前token类型匹配
     */
    bool match(TokenType type);

    /**
     * @brief 匹配多个指定类型的token
     * @param types 期望的token类型列表
     * @return 如果当前token类型匹配
     */
    bool match(const std::vector<TokenType>& types);

    /**
     * @brief 检查当前token是否是指定类型
     * @param type 期望的token类型
     * @return 如果当前token类型匹配
     */
    bool check(TokenType type) const;

    /**
     * @brief 消费当前token
     * @return 消费的token
     */
    Token advance();

    /**
     * @brief 检查是否到达文件末尾
     * @return 如果到达文件末尾
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

    // 错误处理
    /**
     * @brief 处理错误
     * @param message 错误信息
     * @return 解析得到的表达式节点
     */
    std::unique_ptr<Expr> error(const std::string& message);

    /**
     * @brief 同步解析器
     */
    void synchronize();

    // 词法分析器
    Lexer& lexer_;
    Token current_token_;
    Token previous_token_;
    bool had_error_ = false;
};

} // namespace collie

#endif // COLLIE_PARSER_H