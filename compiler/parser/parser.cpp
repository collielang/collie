/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语法分析器实现，负责将词法分析器生成的 token 序列转换为抽象语法树(AST)。
 *              实现了递归下降解析算法，支持表达式、语句、函数、类等语法结构的解析。
 */
#include "parser.h"
#include <iostream>

namespace collie {

/**
 * 语法分析入口
 * 根据当前 token 类型选择解析策略,支持块语句和声明语句
 * @return 解析得到的语句节点
 * @throw ParseError 当遇到语法错误时
 */
std::unique_ptr<Stmt> Parser::parse() {
    try {
        parse_state_ = ParseState::NORMAL;

        // 如果是块语句，直接返回
        if (match(TokenType::DELIMITER_LBRACE)) {
            return block();
        }

        // 否则返回单个声明
        return declaration();
    } catch (const ParseError& error) {
        if (parse_state_ != ParseState::PANIC_MODE) {
            parse_state_ = ParseState::PANIC_MODE;
            synchronize();
        }
        return nullptr;
    }
}

/**
 * 表达式解析
 * 解析最基本的表达式
 * @return 表达式节点
 */
std::unique_ptr<Expr> Parser::expression() {
    return assignment();
}

/**
 * 赋值表达式解析
 * 解析形如: identifier = expression
 * @return 赋值表达式节点
 */
std::unique_ptr<Expr> Parser::assignment() {
    auto expr = logical_or();

    if (match(TokenType::OP_ASSIGN)) {
        Token equals = previous();
        auto value = assignment();

        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr.get())) {
            return std::make_unique<AssignExpr>(
                identifier->name(),
                std::move(value)
            );
        }

        error("Invalid assignment target.");
    }

    return expr;
}

/**
 * 逻辑或表达式解析
 * 解析形如: expr || expr
 * @return 逻辑或表达式节点
 */
std::unique_ptr<Expr> Parser::logical_or() {
    auto expr = logical_and();

    while (match(TokenType::OP_OR)) {
        Token op = previous();
        auto right = logical_and();
        expr = std::make_unique<BinaryExpr>(
            std::move(expr),
            op,
            std::move(right)
        );
    }

    return expr;
}

/**
 * 逻辑与表达式解析
 * 解析形如: expr && expr
 * @return 逻辑与表达式节点
 */
std::unique_ptr<Expr> Parser::logical_and() {
    auto expr = equality();

    while (match(TokenType::OP_AND)) {
        Token op = previous();
        auto right = equality();
        expr = std::make_unique<BinaryExpr>(
            std::move(expr),
            op,
            std::move(right)
        );
    }

    return expr;
}

/**
 * 相等性表达式解析
 * 解析形如: expr == expr 或 expr != expr
 * @return 相等性表达式节点
 */
std::unique_ptr<Expr> Parser::equality() {
    auto expr = comparison();

    std::vector<TokenType> operators = {
        TokenType::OP_EQUAL,
        TokenType::OP_NOT_EQUAL
    };

    while (match(operators)) {
        Token op = previous();
        auto right = comparison();
        expr = std::make_unique<BinaryExpr>(
            std::move(expr),
            op,
            std::move(right)
        );
    }

    return expr;
}

/**
 * 比较表达式解析
 * 解析形如: expr > expr, expr >= expr, expr < expr, expr <= expr
 * @return 比较表达式节点
 */
std::unique_ptr<Expr> Parser::comparison() {
    auto expr = term();

    std::vector<TokenType> operators = {
        TokenType::OP_GREATER,
        TokenType::OP_GREATER_EQ,
        TokenType::OP_LESS,
        TokenType::OP_LESS_EQ
    };

    while (match(operators)) {
        Token op = previous();
        auto right = term();
        expr = std::make_unique<BinaryExpr>(
            std::move(expr),
            op,
            std::move(right)
        );
    }

    return expr;
}

/**
 * 项表达式解析
 * 解析加减法表达式
 * @return 项表达式节点
 */
std::unique_ptr<Expr> Parser::term() {
    auto expr = factor();

    std::vector<TokenType> operators = {
        TokenType::OP_PLUS,
        TokenType::OP_MINUS
    };

    while (match(operators)) {
        Token op = previous();
        auto right = factor();
        expr = std::make_unique<BinaryExpr>(
            std::move(expr),
            op,
            std::move(right)
        );
    }

    return expr;
}

/**
 * 因子表达式解析
 * 解析乘除模运算表达式
 * @return 因子表达式节点
 */
std::unique_ptr<Expr> Parser::factor() {
    auto expr = unary();

    std::vector<TokenType> operators = {
        TokenType::OP_MULTIPLY,
        TokenType::OP_DIVIDE,
        TokenType::OP_MODULO
    };

    while (match(operators)) {
        Token op = previous();
        auto right = unary();
        expr = std::make_unique<BinaryExpr>(
            std::move(expr),
            op,
            std::move(right)
        );
    }

    return expr;
}

/**
 * 一元表达式解析
 * 解析一元运算符表达式
 * @return 一元表达式节点
 */
std::unique_ptr<Expr> Parser::unary() {
    std::vector<TokenType> operators = {
        TokenType::OP_NOT,
        TokenType::OP_MINUS,
        TokenType::OP_BIT_NOT
    };

    if (match(operators)) {
        Token op = previous();
        auto right = unary();
        return std::make_unique<UnaryExpr>(op, std::move(right));
    }

    return primary();
}

/**
 * 基本表达式解析
 * 解析字面量、标识符、括号表达式等基本表达式
 * @return 基本表达式节点
 * @throw ParseError 当表达式语法错误时
 */
std::unique_ptr<Expr> Parser::primary() {
    if (match(TokenType::LITERAL_NUMBER) ||
        match(TokenType::LITERAL_STRING) ||
        match(TokenType::LITERAL_CHAR) ||
        match(TokenType::LITERAL_CHARACTER) ||
        match(TokenType::LITERAL_BOOL)) {
        return std::make_unique<LiteralExpr>(previous());
    }

    if (match(TokenType::IDENTIFIER)) {
        Token name = previous();

        // 如果后面是左括号，说明是函数调用
        if (match(TokenType::DELIMITER_LPAREN)) {
            return finish_call(std::make_unique<IdentifierExpr>(name));
        }

        return std::make_unique<IdentifierExpr>(name);
    }

    if (match(TokenType::DELIMITER_LPAREN)) {
        auto expr = expression();
        consume(TokenType::DELIMITER_RPAREN, "Expect ')' after expression.");
        return expr;
    }

    throw ParseError("Expect expression.", current_token_.line(), current_token_.column());
}

/**
 * 辅助方法: 消费指定类型的 token
 * @param type 期望的 token 类型
 * @param message 错误信息
 * @return 消费的 token
 * @throw ParseError 当 token 类型不匹配时
 */
Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw ParseError(message, current_token_.line(), current_token_.column());
}

/**
 * 辅助方法: 匹配并消费指定类型的 token
 * @param type 期望的 token 类型
 * @return 是否匹配成功
 */
bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

/**
 * 辅助方法: 匹配并消费指定类型列表中的 token
 * @param types 期望的 token 类型列表
 * @return 是否匹配成功
 */
bool Parser::match(const std::vector<TokenType>& types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

/**
 * 辅助方法: 检查当前 token 是否是指定类型
 * @param type 期望的 token 类型
 * @return 是否匹配
 */
bool Parser::check(TokenType type) const {
    if (is_at_end()) return false;
    return current_token_.type() == type;
}

/**
 * 辅助方法: 前进到下一个 token
 * @return 前一个 token
 */
Token Parser::advance() {
    previous_token_ = current_token_;
    current_token_ = lexer_.next_token();
    return previous_token_;
}

/**
 * 辅助方法: 检查是否到达输入结束
 * @return 是否到达结束
 */
bool Parser::is_at_end() const {
    return current_token_.type() == TokenType::END_OF_FILE;
}

/**
 * 辅助方法: 获取当前 token
 * @return 当前 token
 */
Token Parser::peek() const {
    return current_token_;
}

/**
 * 辅助方法: 获取前一个 token
 * @return 前一个 token
 */
Token Parser::previous() const {
    return previous_token_;
}

/**
 * 错误处理
 * 创建并抛出语法错误异常
 * @param message 错误信息
 * @return 永远不会返回,总是抛出异常
 */
std::unique_ptr<Expr> Parser::error(const std::string& message) {
    throw ParseError(message, current_token_.line(), current_token_.column());
}

/**
 * 错误恢复
 * 在遇到语法错误时进行恢复,跳过错误的 token 直到遇到同步点
 */
void Parser::synchronize() {
    parse_state_ = ParseState::ERROR_RECOVERY;

    while (!is_at_end()) {
        // 如果前一个 token 是分号
        if (previous().type() == TokenType::DELIMITER_SEMICOLON) {
            parse_state_ = ParseState::NORMAL;
            return;
        }

        switch (peek().type()) {
            case TokenType::KW_CLASS:
            case TokenType::KW_FUNCTION:
            case TokenType::KW_IF:
            case TokenType::KW_WHILE:
            case TokenType::KW_FOR:
            case TokenType::KW_RETURN:
                parse_state_ = ParseState::NORMAL;
                return;
        }

        advance();
    }

    parse_state_ = ParseState::NORMAL;
}

/**
 * 声明语句解析
 * 解析类声明、函数声明、变量声明等顶层声明语句
 * @return 声明语句节点
 * @throw ParseError 当声明语法错误时
 */
std::unique_ptr<Stmt> Parser::declaration() {
    try {
        if (match(TokenType::KW_CLASS)) {
            return class_declaration();
        }
        if (match(TokenType::KW_FUNCTION)) {
            return function_declaration();
        }
        if (is_type_token(peek().type())) {
            Token type = advance();
            Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");
            return var_declaration(type, name);
        }

        return statement();
    } catch (const ParseError& error) {
        if (parse_state_ != ParseState::PANIC_MODE) {
            synchronize();
        }
        return nullptr;
    }
}

/**
 * 函数声明解析
 * 解析函数定义,包括返回类型、名称、参数列表和函数体
 * @param type 函数返回类型的 token
 * @param name 函数名称的 token
 * @return 函数声明节点
 * @throw ParseError 当函数声明语法错误时
 */
std::unique_ptr<Stmt> Parser::function_declaration(Token type, Token name) {
    // 解析参数列表
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after function name.");
    auto params = parameters();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after parameters.");

    // 解析函数体
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before function body.");
    auto body = block();

    return std::make_unique<FunctionStmt>(type, name, std::move(params), std::move(body));
}

/**
 * 变量声明语句解析
 * @param type 变量类型的 token
 * @param name 变量名称的 token
 * @return 变量声明语句节点
 */
std::unique_ptr<Stmt> Parser::var_declaration(Token type, Token name) {
    std::unique_ptr<Expr> initializer = nullptr;
    bool is_const = false;

    // 检查是否是常量声明
    if (match(TokenType::KW_CONST)) {
        is_const = true;
    }

    if (match(TokenType::OP_ASSIGN)) {
        initializer = expression();
    } else if (is_const) {
        throw ParseError("Const variable must be initialized",
            name.line(), name.column());
    }

    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after variable declaration.");
    return std::make_unique<VarDeclStmt>(type, name, std::move(initializer), is_const);
}

/**
 * 语句解析
 * 根据当前 token 类型解析不同类型的语句
 * @return 语句节点
 */
std::unique_ptr<Stmt> Parser::statement() {
    try {
        switch (peek().type()) {
            case TokenType::KW_IF:
                advance();
                return if_statement();
            case TokenType::KW_WHILE:
                advance();
                return while_statement();
            case TokenType::KW_FOR:
                advance();
                return for_statement();
            case TokenType::KW_RETURN:
                advance();
                return return_statement();
            case TokenType::KW_BREAK:
                if (!in_loop()) {
                    throw ParseError("Cannot use 'break' outside of a loop.",
                        current_token_.line(), current_token_.column());
                }
                advance();
                return break_statement();
            case TokenType::KW_CONTINUE:
                if (!in_loop()) {
                    throw ParseError("Cannot use 'continue' outside of a loop.",
                        current_token_.line(), current_token_.column());
                }
                advance();
                return continue_statement();
            case TokenType::DELIMITER_LBRACE:
                advance();
                return block();
            default:
                return expression_statement();
        }
    } catch (const ParseError& error) {
        if (parse_state_ != ParseState::PANIC_MODE) {
            parse_state_ = ParseState::ERROR_RECOVERY;
            panic_mode_recovery();
        }
        return nullptr;
    }
}

/**
 * 表达式语句解析
 * 解析单独的表达式语句
 * @return 表达式语句节点
 */
std::unique_ptr<Stmt> Parser::expression_statement() {
    auto expr = expression();
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after expression.");
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

/**
 * 块语句解析
 * 解析由大括号包围的语句序列
 * @return 块语句节点
 * @throw ParseError 当块语句语法错误时
 */
std::unique_ptr<Stmt> Parser::block() {
    auto statements = block_statements();
    consume(TokenType::DELIMITER_RBRACE, "Expect '}' after block.");
    return std::make_unique<BlockStmt>(std::move(statements));
}

/**
 * 块内语句序列解析
 * 解析块内的多个语句
 * @return 语句节点列表
 */
std::vector<std::unique_ptr<Stmt>> Parser::block_statements() {
    std::vector<std::unique_ptr<Stmt>> statements;

    while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
        try {
            auto stmt = declaration();
            if (stmt) {
                statements.push_back(std::move(stmt));
            }
        } catch (const ParseError& error) {
            if (parse_state_ != ParseState::PANIC_MODE) {
                parse_state_ = ParseState::ERROR_RECOVERY;
                panic_mode_recovery();
            }
            // 继续解析下一个语句
            continue;
        }
    }

    return statements;
}

/**
 * if 语句解析
 * 解析形如: if (condition) statement [else statement]
 * @return if 语句节点
 */
std::unique_ptr<Stmt> Parser::if_statement() {
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'if'.");
    auto condition = expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after if condition.");

    auto then_branch = statement();
    std::unique_ptr<Stmt> else_branch = nullptr;

    if (match(TokenType::KW_ELSE)) {
        else_branch = statement();
    }

    return std::make_unique<IfStmt>(
        std::move(condition),
        std::move(then_branch),
        std::move(else_branch)
    );
}

/**
 * while 循环语句解析
 * 解析形如: while (condition) statement
 * @return while 语句节点
 */
std::unique_ptr<Stmt> Parser::while_statement() {
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'while'.");
    auto condition = expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after while condition.");

    loop_depth_++;  // 进入循环
    auto body = statement();
    loop_depth_--;  // 退出循环

    return std::make_unique<WhileStmt>(
        std::move(condition),
        std::move(body)
    );
}

/**
 * for 循环语句解析
 * 解析形如: for (init; condition; increment) statement
 * @return for 语句节点
 */
std::unique_ptr<Stmt> Parser::for_statement() {
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'for'.");

    // 初始化部分
    std::unique_ptr<Stmt> initializer;
    if (!check(TokenType::DELIMITER_SEMICOLON)) {
        if (is_type_token(peek().type())) {
            Token type = advance();
            Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");
            initializer = var_declaration(type, name);
        } else {
            initializer = expression_statement();
        }
    } else {
        advance(); // 跳过分号
    }

    // 条件部分
    std::unique_ptr<Expr> condition;
    if (!check(TokenType::DELIMITER_SEMICOLON)) {
        condition = expression();
    }
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after loop condition.");

    // 增量部分
    std::unique_ptr<Expr> increment;
    if (!check(TokenType::DELIMITER_RPAREN)) {
        increment = expression();
    }
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after for clauses.");

    loop_depth_++;  // 进入循环
    auto body = statement();
    loop_depth_--;  // 退出循环

    return std::make_unique<ForStmt>(
        std::move(initializer),
        std::move(condition),
        std::move(increment),
        std::move(body)
    );
}

/**
 * return 语句解析
 * 解析形如: return [expression];
 * @return return 语句节点
 */
std::unique_ptr<Stmt> Parser::return_statement() {
    Token keyword = previous();
    std::unique_ptr<Expr> value = nullptr;

    if (!check(TokenType::DELIMITER_SEMICOLON)) {
        value = expression();
    }

    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after return value.");
    return std::make_unique<ReturnStmt>(keyword, std::move(value));
}

/**
 * 参数列表解析
 * 解析函数参数列表
 * @return 参数列表
 */
std::vector<Parameter> Parser::parameters() {
    std::vector<Parameter> params;

    if (!check(TokenType::DELIMITER_RPAREN)) {
        do {
            if (params.size() >= 255) {
                error("Cannot have more than 255 parameters.");
            }

            // 解析参数类型
            Token type = consume(TokenType::IDENTIFIER, "Expect parameter type.");
            // 解析参数名
            Token name = consume(TokenType::IDENTIFIER, "Expect parameter name.");

            params.push_back(Parameter{type, name});

        } while (match(TokenType::DELIMITER_COMMA));
    }

    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after parameters.");
    return params;
}

/**
 * 函数调用解析
 * 解析函数调用表达式,包括参数列表
 * @param callee 被调用的函数表达式
 * @return 函数调用表达式节点
 */
std::unique_ptr<Expr> Parser::finish_call(std::unique_ptr<Expr> callee) {
    std::vector<std::unique_ptr<Expr>> arguments;

    if (!check(TokenType::DELIMITER_RPAREN)) {
        do {
            if (arguments.size() >= 255) {
                error("Cannot have more than 255 arguments.");
            }
            arguments.push_back(expression());
        } while (match(TokenType::DELIMITER_COMMA));
    }

    Token paren = consume(TokenType::DELIMITER_RPAREN,
                         "Expect ')' after arguments.");

    return std::make_unique<CallExpr>(
        std::move(callee),
        paren,
        std::move(arguments)
    );
}

/**
 * 恐慌模式恢复
 * 在遇到严重错误时进行恢复,跳过所有 token 直到遇到语句开始标记
 */
void Parser::panic_mode_recovery() {
    while (!is_at_end()) {
        // 跳过所有 token 直到遇到语句开始标记
        if (is_statement_start(peek().type())) {
            parse_state_ = ParseState::NORMAL;
            return;
        }

        // 跳过当前 token
        advance();

        // 如果遇到分号或右大括号,也可以认为是一个恢复点
        if (previous().type() == TokenType::DELIMITER_SEMICOLON ||
            previous().type() == TokenType::DELIMITER_RBRACE) {
            parse_state_ = ParseState::NORMAL;
            return;
        }
    }

    // 如果到达文件末尾,恢复正常状态
    parse_state_ = ParseState::NORMAL;
}

/**
 * 类成员声明解析
 * 解析类成员变量或成员函数声明
 * @param is_public 是否是公有成员
 * @return 成员声明节点
 * @throw ParseError 当成员声明语法错误时
 */
std::unique_ptr<Stmt> Parser::member_declaration(bool is_public) {
    if (!is_type_token(peek().type())) {
        throw ParseError("Expect member type.",
            current_token_.line(), current_token_.column());
    }

    Token type = advance();
    Token name = consume(TokenType::IDENTIFIER, "Expect member name.");

    // 如果后面是左括号,说明是成员函数
    if (match(TokenType::DELIMITER_LPAREN)) {
        auto method = function_declaration(type, name);
        method->set_access(is_public);
        return method;
    }

    // 否则是成员变量
    auto field = var_declaration(type, name);
    field->set_access(is_public);
    return field;
}

/**
 * 类声明解析
 * 解析类定义,包括类名、成员变量和成员函数
 * @return 类声明节点
 * @throw ParseError 当类声明语法错误时
 */
std::unique_ptr<Stmt> Parser::class_declaration() {
    // 获取类名
    Token name = consume(TokenType::IDENTIFIER, "Expect class name.");

    // 解析类体
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before class body.");

    // 解析类成员
    std::vector<std::unique_ptr<Stmt>> members;
    while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
        try {
            // 解析访问修饰符
            bool is_public = true;
            if (match(TokenType::KW_PRIVATE)) {
                is_public = false;
            } else {
                match(TokenType::KW_PUBLIC); // 可选的 public 关键字
            }

            // 解析成员声明
            members.push_back(member_declaration(is_public));
        } catch (const ParseError& error) {
            if (parse_state_ != ParseState::PANIC_MODE) {
                parse_state_ = ParseState::ERROR_RECOVERY;
                panic_mode_recovery();
            }
            // 继续解析下一个成员
            continue;
        }
    }

    consume(TokenType::DELIMITER_RBRACE, "Expect '}' after class body.");

    return std::make_unique<ClassStmt>(name, std::move(members));
}

/**
 * @brief 检查是否是类型 token
 * @param type token 类型
 * @return 是否是类型 token
 */
bool Parser::is_type_token(TokenType type) const {
    return type == TokenType::KW_NUMBER ||
           type == TokenType::KW_STRING ||
           type == TokenType::KW_BOOL ||
           type == TokenType::KW_CHAR ||
           type == TokenType::KW_CHARACTER ||
           type == TokenType::KW_VOID ||
           type == TokenType::KW_OBJECT;
}

/**
 * @brief 检查是否是语句开始 token
 * @param type token 类型
 * @return 是否是语句开始 token
 */
bool Parser::is_statement_start(TokenType type) const {
    switch (type) {
        case TokenType::KW_IF:
        case TokenType::KW_WHILE:
        case TokenType::KW_FOR:
        case TokenType::KW_RETURN:
        case TokenType::KW_BREAK:
        case TokenType::KW_CONTINUE:
        case TokenType::DELIMITER_LBRACE:
        case TokenType::KW_CLASS:
        case TokenType::KW_FUNCTION:
            return true;
        default:
            return false;
    }
}

/**
 * @brief 检查是否是访问修饰符
 * @param type token 类型
 * @return 是否是访问修饰符
 */
bool Parser::is_access_modifier(TokenType type) const {
    return type == TokenType::KW_PUBLIC ||
           type == TokenType::KW_PRIVATE ||
           type == TokenType::KW_PROTECTED;
}

/**
 * break 语句解析
 * 解析形如: break;
 * @return break 语句节点
 * @throw ParseError 当不在循环内使用 break 时
 */
std::unique_ptr<Stmt> Parser::break_statement() {
    Token keyword = previous();

    if (!in_loop()) {
        throw ParseError("Cannot use 'break' outside of a loop.",
            keyword.line(), keyword.column());
    }

    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after 'break'.");
    return std::make_unique<BreakStmt>(keyword);
}

/**
 * continue 语句解析
 * 解析形如: continue;
 * @return continue 语句节点
 * @throw ParseError 当不在循环内使用 continue 时
 */
std::unique_ptr<Stmt> Parser::continue_statement() {
    Token keyword = previous();

    if (!in_loop()) {
        throw ParseError("Cannot use 'continue' outside of a loop.",
            keyword.line(), keyword.column());
    }

    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after 'continue'.");
    return std::make_unique<ContinueStmt>(keyword);
}

} // namespace collie