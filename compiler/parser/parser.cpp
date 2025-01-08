/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语法分析器的实现，负责构建抽象语法树
 *
 * 语法分析器采用递归下降的方法，按照以下文法规则解析：
 *
 * program     → declaration* EOF
 * declaration → varDecl | funcDecl | statement
 * varDecl     → "var" IDENTIFIER IDENTIFIER ("=" expression)? ";"
 * funcDecl    → "function" IDENTIFIER "(" parameters? ")" IDENTIFIER block
 * statement   → exprStmt | block | ifStmt | whileStmt | forStmt
 *             | returnStmt | breakStmt | continueStmt
 *
 * 表达式的优先级（从高到低）：
 * primary     → NUMBER | STRING | BOOL | IDENTIFIER | "(" expression ")"
 * unary       → ("!" | "-") unary | primary
 * factor      → unary (("*" | "/" | "%") unary)*
 * term        → factor (("+" | "-") factor)*
 * comparison  → term ((">" | ">=" | "<" | "<=") term)*
 * equality    → comparison (("==" | "!=") comparison)*
 * logicalAnd  → equality ("&&" equality)*
 * logicalOr   → logicalAnd ("||" logicalAnd)*
 * assignment  → IDENTIFIER "=" assignment | logicalOr
 * expression  → assignment
 */
#include "parser.h"
#include <cassert>
#include <sstream>
#include <iostream>

namespace collie {

// -----------------------------------------------------------------------------
// 公共接口实现
// -----------------------------------------------------------------------------

/**
 * @brief 解析整个程序
 * @return 包含所有顶层语句的AST节点列表
 *
 * 程序由一系列声明和语句组成，直到文件结束。
 * 每个声明或语句都可能产生错误，但解析器会尝试继续处理后续内容。
 */
std::vector<std::unique_ptr<Stmt>> Parser::parse_program() {
    std::vector<std::unique_ptr<Stmt>> statements;

    while (!is_at_end()) {
        try {
            auto stmt = parse_declaration();
            if (stmt) {
                statements.push_back(std::move(stmt));
            }
        } catch (const ParseError& error) {
            report_error(error);
            synchronize();
        }
    }

    return statements;
}

// -----------------------------------------------------------------------------
// 语句解析方法
// -----------------------------------------------------------------------------

/**
 * @brief 解析声明语句
 * @return 声明语句的AST节点
 *
 * 声明可以是：
 * 1. 变量声明（以类型名开头）
 * 2. 函数声明（以 function 开头）
 * 3. 其他语句
 */
std::unique_ptr<Stmt> Parser::parse_declaration() {
    try {
        // 检查是否是类型名开头的变量声明
        if (match(TokenType::KW_NUMBER) ||
            match(TokenType::KW_STRING) ||
            match(TokenType::KW_BOOL) ||
            match(TokenType::KW_CHARACTER)) {
            Token type = previous();
            return parse_type_declaration();
        }
        // 自定义类型需要特殊处理
        if (check(TokenType::IDENTIFIER)) {
            // 看看下一个 token 是不是也是标识符，如果是，那这是一个类型声明
            Token potential_type = peek();
            Token next = peek_next();
            if (next.type() == TokenType::IDENTIFIER) {
                advance(); // 消费类型名
                return parse_type_declaration();
            }
        }
        if (match(TokenType::KW_FUNCTION)) {
            return parse_function_declaration();
        }
        return parse_statement();
    } catch (const ParseError& error) {
        report_error(error);
        synchronize();
        return nullptr;
    }
}

/**
 * @brief 解析类型声明语句
 * @return 变量声明的AST节点
 */
std::unique_ptr<Stmt> Parser::parse_type_declaration() {
    // 记录类型 token
    Token type = previous();

    // 解析变量名
    Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

    // 解析可选的初始化表达式
    std::unique_ptr<Expr> initializer = nullptr;
    if (match(TokenType::OP_ASSIGN)) {
        try {
            initializer = parse_expression();
        } catch (const ParseError& error) {
            report_error(error);
            synchronize_until(TokenType::DELIMITER_SEMICOLON);
        }
    }

    // 确保语句以分号结束
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after variable declaration.");

    return std::make_unique<VarDeclStmt>(name, type, std::move(initializer));
}

// -----------------------------------------------------------------------------
// 表达式解析方法
// -----------------------------------------------------------------------------

/**
 * @brief 解析表达式
 * @return 表达式的AST节点
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
std::unique_ptr<Expr> Parser::parse_expression() {
    try {
        auto expr = parse_assignment();
        if (!expr) {
            throw error(peek(), "Expect expression.");
        }
        return expr;
    } catch (const ParseError& error) {
        report_error(error);
        synchronize();
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::parse_assignment() {
    auto expr = parse_logical_or();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    if (match(TokenType::OP_ASSIGN)) {
        Token equals = previous();
        auto value = parse_assignment();
        if (!value) {
            throw error(peek(), "Expect expression after '='.");
        }

        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr.get())) {
            Token name = identifier->name();
            return std::make_unique<AssignExpr>(name, std::move(value));
        }

        throw error(equals, "Invalid assignment target.");
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_logical_or() {
    auto expr = parse_logical_and();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match(TokenType::OP_OR)) {
        Token op = previous();
        auto right = parse_logical_and();
        if (!right) {
            throw error(peek(), "Expect expression after '||'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_logical_and() {
    auto expr = parse_equality();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match(TokenType::OP_AND)) {
        Token op = previous();
        auto right = parse_equality();
        if (!right) {
            throw error(peek(), "Expect expression after '&&'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_equality() {
    auto expr = parse_comparison();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match({TokenType::OP_EQUAL, TokenType::OP_NOT_EQUAL})) {
        Token op = previous();
        auto right = parse_comparison();
        if (!right) {
            throw error(peek(), "Expect expression after equality operator.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_comparison() {
    auto expr = parse_term();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match({
        TokenType::OP_GREATER,
        TokenType::OP_GREATER_EQ,
        TokenType::OP_LESS,
        TokenType::OP_LESS_EQ
    })) {
        Token op = previous();
        auto right = parse_term();
        if (!right) {
            throw error(peek(), "Expect expression after comparison operator.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_term() {
    auto expr = parse_factor();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match({TokenType::OP_PLUS, TokenType::OP_MINUS})) {
        Token op = previous();
        auto right = parse_factor();
        if (!right) {
            throw error(peek(), "Expect expression after '+' or '-'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_factor() {
    auto expr = parse_unary();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match({TokenType::OP_MULTIPLY, TokenType::OP_DIVIDE, TokenType::OP_MODULO})) {
        Token op = previous();
        auto right = parse_unary();
        if (!right) {
            throw error(peek(), "Expect expression after '*', '/' or '%'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (match({TokenType::OP_NOT, TokenType::OP_MINUS})) {
        Token op = previous();
        auto right = parse_unary();
        if (!right) {
            throw error(peek(), "Expect expression after unary operator.");
        }
        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    auto expr = parse_primary();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    try {
        if (match(TokenType::LITERAL_NUMBER)) {
            return std::make_unique<LiteralExpr>(previous());
        }
        if (match(TokenType::LITERAL_STRING)) {
            return std::make_unique<LiteralExpr>(previous());
        }
        if (match(TokenType::LITERAL_BOOL) || match(TokenType::KW_TRUE) || match(TokenType::KW_FALSE)) {
            return std::make_unique<LiteralExpr>(previous());
        }
        if (match(TokenType::IDENTIFIER)) {
            Token name = previous();
            // 检查是否是函数调用
            if (check(TokenType::DELIMITER_LPAREN)) {
                consume(TokenType::DELIMITER_LPAREN, "Expect '(' after function name.");  // 使用 consume 而不是 match
                std::vector<std::unique_ptr<Expr>> arguments;
                // 解析参数列表
                if (!check(TokenType::DELIMITER_RPAREN)) {
                    do {
                        if (arguments.size() >= 255) {
                            throw error(peek(), "Cannot have more than 255 arguments.");
                        }
                        auto arg = parse_expression();
                        if (!arg) {
                            throw error(peek(), "Expect expression in function arguments.");
                        }
                        arguments.push_back(std::move(arg));
                    } while (match(TokenType::DELIMITER_COMMA));
                }
                Token paren = consume(TokenType::DELIMITER_RPAREN, "Expect ')' after arguments.");
                return std::make_unique<CallExpr>(
                    std::make_unique<IdentifierExpr>(name),
                    paren,
                    std::move(arguments)
                );
            }
            return std::make_unique<IdentifierExpr>(name);
        }
        if (match(TokenType::DELIMITER_LPAREN)) {
            if (check(TokenType::DELIMITER_RPAREN) || check(TokenType::IDENTIFIER) ||
                is_literal_token(peek())) {
                return parse_tuple_expr();
            }
            auto expr = parse_expression();
            consume(TokenType::DELIMITER_RPAREN, "Expect ')' after expression.");
            return expr;
        }

        throw error(peek(), "Expect expression.");
    } catch (const ParseError& error) {
        report_error(error);
        synchronize();
        return nullptr;
    }
}

std::unique_ptr<Expr> Parser::finish_call(const Token& callee) {
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after function name.");
    std::vector<std::unique_ptr<Expr>> arguments;

    // 解析参数列表
    if (!check(TokenType::DELIMITER_RPAREN)) {
        do {
            if (arguments.size() >= 255) {
                throw error(peek(), "Cannot have more than 255 arguments.");
            }
            auto arg = parse_expression();
            if (!arg) {
                throw error(peek(), "Expect expression in function arguments.");
            }
            arguments.push_back(std::move(arg));
        } while (match(TokenType::DELIMITER_COMMA));
    }

    Token paren = consume(TokenType::DELIMITER_RPAREN, "Expect ')' after arguments.");
    return std::make_unique<CallExpr>(
        std::make_unique<IdentifierExpr>(callee),
        paren,
        std::move(arguments)
    );
}

// -----------------------------------------------------------------------------
// Token 处理方法
// -----------------------------------------------------------------------------

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    throw ParseError(message, peek().line(), peek().column());
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) {
        return false;
    }
    return peek().type() == type;
}

bool Parser::is_at_end() const {
    return peek().type() == TokenType::END_OF_FILE;
}

Token Parser::peek() const {
    if (is_at_end()) {
        return Token(TokenType::END_OF_FILE, "", 0, 0);
    }
    return tokens_[current_];
}

Token Parser::previous() const {
    if (current_ <= 0 || current_ > tokens_.size()) {
        return Token(TokenType::TOKEN_ERROR, "", 0, 0);
    }
    return tokens_[current_ - 1];
}

Token Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

// -----------------------------------------------------------------------------
// 错误处理方法
// -----------------------------------------------------------------------------

void Parser::report_error(const ParseError& error) {
    if (in_panic_mode_) return;
    in_panic_mode_ = true;

    errors_.push_back(error);
    std::cerr << "Parse error at line " << error.line()
              << ", column " << error.column()
              << ": " << error.what() << std::endl;
}

void Parser::synchronize() {
    in_panic_mode_ = false;

    while (!is_at_end()) {
        if (previous().type() == TokenType::DELIMITER_SEMICOLON) return;

        switch (peek().type()) {
            case TokenType::KW_CLASS:
            case TokenType::KW_FUNCTION:
            case TokenType::KW_VAR:
            case TokenType::KW_FOR:
            case TokenType::KW_IF:
            case TokenType::KW_WHILE:
            case TokenType::KW_RETURN:
                return;
            default:
                advance();
        }
    }
}

void Parser::synchronize_until(TokenType type) {
    while (!is_at_end() && peek().type() != type) {
        advance();
    }
    if (!is_at_end()) advance(); // 消费目标 token
}

// -----------------------------------------------------------------------------
// 错误处理辅助方法
// -----------------------------------------------------------------------------

ParseError Parser::error(const Token& token, const std::string& message) {
    return ParseError(message, token.line(), token.column());
}

void Parser::check_max_nesting_depth() {
    if (nesting_depth_ > MAX_NESTING_DEPTH) {
        throw error(peek(), "Maximum nesting depth exceeded.");
    }
}

bool Parser::is_literal_token(const Token& token) const {
    switch (token.type()) {
        case TokenType::LITERAL_NUMBER:
        case TokenType::LITERAL_STRING:
        case TokenType::LITERAL_CHAR:
        case TokenType::LITERAL_CHARACTER:
        case TokenType::LITERAL_BOOL:
        case TokenType::KW_TRUE:
        case TokenType::KW_FALSE:
        case TokenType::KW_NONE:
            return true;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// 语句解析方法（续）
// -----------------------------------------------------------------------------

/**
 * @brief 解析语句
 * @return 语句的AST节点
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
std::unique_ptr<Stmt> Parser::parse_statement() {
    try {
        if (match(TokenType::KW_IF)) {
            return parse_if_statement();
        }
        if (match(TokenType::KW_WHILE)) {
            return parse_while_statement();
        }
        if (match(TokenType::KW_FOR)) {
            return parse_for_statement();
        }
        if (match(TokenType::KW_RETURN)) {
            return parse_return_statement();
        }
        if (match(TokenType::KW_BREAK)) {
            return parse_break_statement();
        }
        if (match(TokenType::KW_CONTINUE)) {
            return parse_continue_statement();
        }
        if (match(TokenType::DELIMITER_LBRACE)) {
            return parse_block_statement();
        }

        return parse_expression_statement();
    } catch (const ParseError& error) {
        report_error(error);
        synchronize();
        return nullptr;
    }
}

std::unique_ptr<Stmt> Parser::parse_if_statement() {
    // 记录开始位置用于错误报告
    Token if_token = previous();

    // 解析条件
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'if'.");
    auto condition = parse_expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after if condition.");

    // 解析 then 分支
    auto then_branch = parse_statement();

    // 解析可选的 else 分支
    std::unique_ptr<Stmt> else_branch = nullptr;
    if (match(TokenType::KW_ELSE)) {
        else_branch = parse_statement();
    }

    return std::make_unique<IfStmt>(
        if_token,
        std::move(condition),
        std::move(then_branch),
        std::move(else_branch)
    );
}

std::unique_ptr<Stmt> Parser::parse_while_statement() {
    // 记录开始位置用于错误报告
    Token while_token = previous();

    // 解析条件
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'while'.");
    auto condition = parse_expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after while condition.");

    // 增加循环嵌套深度
    ++nesting_depth_;
    check_max_nesting_depth();

    // 解析循环体
    auto body = parse_statement();

    // 减少循环嵌套深度
    --nesting_depth_;

    return std::make_unique<WhileStmt>(
        while_token,
        std::move(condition),
        std::move(body)
    );
}

/**
 * @brief 解析块语句
 * @return 块语句的AST节点
 *
 * 块语句由一对花括号包围，内部可以包含任意数量的语句。
 * 每个块都会创建一个新的作用域。
 *
 * @throws ParseError 如果块语句语法不正确
 */
std::unique_ptr<Stmt> Parser::parse_block_statement() {
    std::vector<std::unique_ptr<Stmt>> statements;

    // 增加嵌套深度
    ++nesting_depth_;
    check_max_nesting_depth();

    while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
        auto stmt = parse_declaration();
        if (stmt) {
            statements.push_back(std::move(stmt));
        }
    }

    // 减少嵌套深度
    --nesting_depth_;

    consume(TokenType::DELIMITER_RBRACE, "Expect '}' after block.");
    return std::make_unique<BlockStmt>(std::move(statements));
}

/**
 * @brief 解析for循环语句
 * @return for循环语句的AST节点
 *
 * for循环语法：
 * for (initializer; condition; increment) {
 *     // loop body
 * }
 *
 * 其中：
 * - initializer: 可选的初始化语句
 * - condition: 可选的循环条件
 * - increment: 可选的递增表达式
 *
 * @throws ParseError 如果for循环语法不正确
 */
std::unique_ptr<Stmt> Parser::parse_for_statement() {
    // 记录开始位置用于错误报告
    Token for_token = previous();

    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'for'.");

    // 解析初始化语句
    std::unique_ptr<Stmt> initializer;
    if (match(TokenType::DELIMITER_SEMICOLON)) {
        initializer = nullptr;
    } else if (match(TokenType::KW_NUMBER) ||
               match(TokenType::KW_STRING) ||
               match(TokenType::KW_BOOL) ||
               match(TokenType::KW_CHARACTER) ||
               match(TokenType::IDENTIFIER)) {
        initializer = parse_type_declaration();
    } else {
        initializer = parse_expression_statement();
    }

    // 解析条件表达式
    std::unique_ptr<Expr> condition = nullptr;
    if (!check(TokenType::DELIMITER_SEMICOLON)) {
        condition = parse_expression();
    }
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after loop condition.");

    // 解析递增表达式
    std::unique_ptr<Expr> increment = nullptr;
    if (!check(TokenType::DELIMITER_RPAREN)) {
        increment = parse_expression();
    }
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after for clauses.");

    // 增加循环嵌套深度
    ++nesting_depth_;
    check_max_nesting_depth();

    // 解析循环体
    auto body = parse_statement();

    // 减少循环嵌套深度
    --nesting_depth_;

    return std::make_unique<ForStmt>(
        for_token,
        std::move(initializer),
        std::move(condition),
        std::move(increment),
        std::move(body)
    );
}

/**
 * @brief 解析return语句
 * @return return语句的AST节点
 *
 * return语句语法：
 * return [expression]?;
 *
 * @throws ParseError 如果return语句语法不正确
 */
std::unique_ptr<Stmt> Parser::parse_return_statement() {
    Token keyword = previous();
    std::unique_ptr<Expr> value = nullptr;

    if (!check(TokenType::DELIMITER_SEMICOLON)) {
        value = parse_expression();
    }

    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after return value.");
    return std::make_unique<ReturnStmt>(keyword, std::move(value));
}

/**
 * @brief 解析break语句
 * @return break语句的AST节点
 *
 * @throws ParseError 如果break语句语法不正确或在循环外使用
 */
std::unique_ptr<Stmt> Parser::parse_break_statement() {
    Token keyword = previous();
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after 'break'.");
    return std::make_unique<BreakStmt>(keyword);
}

/**
 * @brief 解析continue语句
 * @return continue语句的AST节点
 *
 * @throws ParseError 如果continue语句语法不正确或在循环外使用
 */
std::unique_ptr<Stmt> Parser::parse_continue_statement() {
    Token keyword = previous();
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after 'continue'.");
    return std::make_unique<ContinueStmt>(keyword);
}

/**
 * @brief 解析表达式语句
 * @return 表达式语句的AST节点
 *
 * 表达式语句是一个以分号结尾的表达式
 *
 * @throws ParseError 如果表达式语句语法不正确
 */
std::unique_ptr<Stmt> Parser::parse_expression_statement() {
    auto expr = parse_expression();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after expression.");
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

// -----------------------------------------------------------------------------
// 函数声明解析方法
// -----------------------------------------------------------------------------

/**
 * @brief 解析函数声明
 * @return 函数声明的AST节点
 *
 * 函数声明的语法：
 * function name(param1 type1, param2 type2, ...) returnType {
 *     // function body
 * }
 *
 * @throws ParseError 如果函数声明语法不正确
 */
std::unique_ptr<Stmt> Parser::parse_function_declaration() {
    // 记录开始位置用于错误报告
    Token func_token = previous();

    // 解析函数名
    Token name = consume(TokenType::IDENTIFIER, "Expect function name.");

    // 解析参数列表
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after function name.");
    std::vector<Parameter> parameters;

    if (!check(TokenType::DELIMITER_RPAREN)) {
        do {
            if (parameters.size() >= 255) {
                throw error(peek(), "Cannot have more than 255 parameters.");
            }

            Token param_name = consume(TokenType::IDENTIFIER, "Expect parameter name.");
            Token param_type = consume(TokenType::IDENTIFIER, "Expect parameter type.");
            parameters.emplace_back(Parameter{param_type, param_name});
        } while (match(TokenType::DELIMITER_COMMA));
    }
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after parameters.");

    // 解析返回类型
    Token return_type = consume(TokenType::IDENTIFIER, "Expect function return type.");

    // 解析函数体
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before function body.");
    auto body = std::unique_ptr<BlockStmt>(dynamic_cast<BlockStmt*>(parse_block_statement().release()));

    return std::make_unique<FunctionStmt>(return_type, name, std::move(parameters), std::move(body));
}

// -----------------------------------------------------------------------------
// 错误处理辅助方法
// -----------------------------------------------------------------------------

/**
 * @brief 处理解析错误
 * @param error 要处理的错误
 *
 * 错误处理流程：
 * 1. 记录错误信息
 * 2. 如果不在恐慌模式，进入恐慌模式
 * 3. 尝试恢复到下一个有效的语法位置
 * 4. 退出恐慌模式
 */
void Parser::handle_error(const ParseError& error) {
    // 记录错误
    report_error(error);

    // 如果不在恐慌模式，进入恐慌模式并开始恢复
    if (!in_panic_mode_) {
        in_panic_mode_ = true;
        panic_mode_error_recovery();
        in_panic_mode_ = false;
    }
}

void Parser::panic_mode_error_recovery() {
    // 记录当前错误位置
    size_t error_line = peek().line();
    size_t error_column = peek().column();

    // 尝试找到最近的同步点
    while (!is_at_end()) {
        // 如果找到了新的语句或声明开始，且已经跨过了错误位置，则退出恢复
        if (is_statement_boundary() || is_declaration_boundary()) {
            if (peek().line() > error_line ||
                (peek().line() == error_line && peek().column() > error_column)) {
                return;
            }
        }
        advance();
    }
}

std::string Parser::format_error_message(
    const Token& token,
    const std::string& message) const {
    std::stringstream ss;
    ss << "Line " << token.line() << ", Column " << token.column() << ": ";

    if (token.type() == TokenType::END_OF_FILE) {
        ss << "Error at end: " << message;
    } else {
        ss << "Error at '" << token.lexeme() << "': " << message;
    }

    // 添加上下文信息
    if (current_ > 0 && current_ < tokens_.size()) {
        ss << "\nContext: ... ";
        // 显示错误位置前后的 token
        size_t start = (current_ >= 2) ? current_ - 2 : 0;
        size_t end = (current_ + 3 < tokens_.size()) ? current_ + 3 : tokens_.size();

        for (size_t i = start; i < end; ++i) {
            if (i == current_) ss << ">>> ";
            ss << tokens_[i].lexeme() << " ";
            if (i == current_) ss << "<<< ";
        }
        ss << "...";
    }

    return ss.str();
}

bool Parser::is_statement_boundary() const {
    if (previous().type() == TokenType::DELIMITER_SEMICOLON) return true;
    if (previous().type() == TokenType::DELIMITER_RBRACE) return true;
    return false;
}

bool Parser::is_declaration_boundary() const {
    if (is_at_end()) return false;

    switch (peek().type()) {
        case TokenType::KW_CLASS:
        case TokenType::KW_FUNCTION:
        case TokenType::KW_VAR:
        case TokenType::KW_FOR:
        case TokenType::KW_IF:
        case TokenType::KW_WHILE:
        case TokenType::KW_RETURN:
            return true;
        default:
            return false;
    }
}

std::unique_ptr<Type> Parser::parse_type() {
    if (match(TokenType::DELIMITER_LPAREN)) {  // 元组类型
        return parse_tuple_type();
    }

    Token type_name = consume(TokenType::IDENTIFIER, "Expect type name.");
    return std::make_unique<BasicType>(type_name);
}

std::unique_ptr<Type> Parser::parse_tuple_type() {
    std::vector<std::unique_ptr<Type>> element_types;

    // 解析第一个类型
    if (!check(TokenType::DELIMITER_RPAREN)) {
        do {
            auto type = parse_type();
            if (type) {
                element_types.push_back(std::move(type));
            }
        } while (match(TokenType::DELIMITER_COMMA));
    }

    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after tuple type elements.");
    return std::make_unique<TupleType>(std::move(element_types));
}

std::unique_ptr<Expr> Parser::parse_tuple_expr() {
    Token left_paren = previous();
    std::vector<std::unique_ptr<Expr>> elements;

    // 解析第一个表达式
    if (!check(TokenType::DELIMITER_RPAREN)) {
        do {
            auto expr = parse_expression();
            if (expr) {
                elements.push_back(std::move(expr));
            }
        } while (match(TokenType::DELIMITER_COMMA));
    }

    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after tuple elements.");
    return std::make_unique<TupleExpr>(std::move(elements), left_paren);
}

std::unique_ptr<Expr> Parser::parse_postfix() {
    auto expr = parse_primary();

    while (true) {
        if (match(TokenType::DELIMITER_DOT)) {
            // 元组成员访问
            if (match(TokenType::LITERAL_NUMBER)) {
                Token dot = previous();
                std::string num_str(previous().lexeme());
                size_t index = std::stoul(num_str, nullptr, 10);
                expr = std::make_unique<TupleMemberExpr>(
                    std::move(expr), dot, index);
            } else {
                // ... 处理其他成员访问 ...
            }
        } else {
            break;
        }
    }

    return expr;
}

Token Parser::peek_next() const {
    if (current_ + 1 >= tokens_.size()) {
        return Token(TokenType::END_OF_FILE, "", 0, 0);
    }
    return tokens_[current_ + 1];
}
} // namespace collie