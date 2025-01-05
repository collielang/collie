/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "parser.h"
#include <iostream>

namespace collie {

std::unique_ptr<Stmt> Parser::parse() {
    try {
        // 如果是块语句，直接返回
        if (match(TokenType::DELIMITER_LBRACE)) {
            return block();
        }
        // 否则返回单个声明
        return declaration();
    } catch (const ParseError& error) {
        std::cout << "Parse error: " << error.what() << std::endl;
        had_error_ = true;
        synchronize();
        return nullptr;
    }
}

// 表达式解析方法实现
std::unique_ptr<Expr> Parser::expression() {
    return assignment();
}

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

// 辅助方法实现
Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw ParseError(message, current_token_.line(), current_token_.column());
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(const std::vector<TokenType>& types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) return false;
    return current_token_.type() == type;
}

Token Parser::advance() {
    previous_token_ = current_token_;
    current_token_ = lexer_.next_token();
    return previous_token_;
}

bool Parser::is_at_end() const {
    return current_token_.type() == TokenType::END_OF_FILE;
}

Token Parser::peek() const {
    return current_token_;
}

Token Parser::previous() const {
    return previous_token_;
}

// 错误处理方法实现
std::unique_ptr<Expr> Parser::error(const std::string& message) {
    throw ParseError(message, current_token_.line(), current_token_.column());
}

void Parser::synchronize() {
    while (!is_at_end()) {
        // 如果当前 token 是分号，跳过它并返回
        if (current_token_.type() == TokenType::DELIMITER_SEMICOLON) {
            advance();
            return;
        }

        // 如果前一个 token 是分号，直接返回
        if (previous().type() == TokenType::DELIMITER_SEMICOLON) {
            return;
        }

        switch (current_token_.type()) {
            case TokenType::KW_CLASS:
            case TokenType::KW_IF:
            case TokenType::KW_WHILE:
            case TokenType::KW_FOR:
            case TokenType::KW_RETURN:
            case TokenType::KW_NUMBER:
            case TokenType::KW_STRING:
            case TokenType::KW_BOOL:
            case TokenType::KW_CHAR:
            case TokenType::KW_CHARACTER:
                return;
            default:
                advance();
                break;
        }
    }
}

// 添加语句解析方法的实现
std::unique_ptr<Stmt> Parser::declaration() {
    try {
        Token current = peek();
        std::cout << "Current token: type=" << static_cast<int>(current.type())
                  << ", lexeme='" << current.lexeme() << "'" << std::endl;

        // 检查是否是类型关键字（变量声明或函数声明）
        if (match(TokenType::KW_NUMBER) ||
            match(TokenType::KW_STRING) ||
            match(TokenType::KW_BOOL) ||
            match(TokenType::KW_CHAR)) {

            Token type = previous();  // 保存类型
            Token name = consume(TokenType::IDENTIFIER, "Expect name.");

            // 如果后面是左括号，说明是函数声明
            if (match(TokenType::DELIMITER_LPAREN)) {
                return function_declaration(type, name);  // 传入已经读取的类型和名称
            }

            // 否则是变量声明
            return var_declaration(type, name);  // 传入已经读取的类型和名称
        }

        return statement();
    } catch (const ParseError& error) {
        std::cout << "Parse error in declaration: " << error.what() << std::endl;
        synchronize();
        return nullptr;
    }
}

// 函数声明方法
std::unique_ptr<Stmt> Parser::function_declaration(Token type, Token name) {
    // 解析参数列表
    auto params = parameters();

    // 解析函数体
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before function body.");
    auto body = std::unique_ptr<BlockStmt>(
        dynamic_cast<BlockStmt*>(block().release())
    );
    if (!body) {
        throw ParseError("Function body must be a block.",
            current_token_.line(), current_token_.column());
    }

    return std::make_unique<FunctionStmt>(
        type,
        name,
        std::move(params),
        std::move(body)
    );
}

// 变量声明方法
std::unique_ptr<Stmt> Parser::var_declaration(Token type, Token name) {
    std::unique_ptr<Expr> initializer = nullptr;
    if (match(TokenType::OP_ASSIGN)) {
        initializer = expression();
    }

    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after variable declaration.");
    return std::make_unique<VarDeclStmt>(type, name, std::move(initializer));
}

std::unique_ptr<Stmt> Parser::statement() {
    if (match(TokenType::DELIMITER_LBRACE)) {
        return block();
    }
    if (match(TokenType::KW_IF)) {
        return if_statement();
    }
    if (match(TokenType::KW_WHILE)) {
        return while_statement();
    }
    if (match(TokenType::KW_FOR)) {
        return for_statement();
    }
    return expression_statement();
}

std::unique_ptr<Stmt> Parser::expression_statement() {
    auto expr = expression();
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after expression.");
    return std::make_unique<ExpressionStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::block() {
    auto statements = block_statements();
    consume(TokenType::DELIMITER_RBRACE, "Expect '}' after block.");
    return std::make_unique<BlockStmt>(std::move(statements));
}

std::vector<std::unique_ptr<Stmt>> Parser::block_statements() {
    std::vector<std::unique_ptr<Stmt>> statements;

    while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
        auto decl = declaration();
        if (decl) {
            statements.push_back(std::move(decl));
        }
    }

    return statements;
}

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

std::unique_ptr<Stmt> Parser::while_statement() {
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'while'.");
    auto condition = expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after while condition.");

    auto body = statement();

    return std::make_unique<WhileStmt>(
        std::move(condition),
        std::move(body)
    );
}

std::unique_ptr<Stmt> Parser::for_statement() {
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'for'.");

    // 初始化部分
    std::unique_ptr<Stmt> initializer;
    if (!check(TokenType::DELIMITER_SEMICOLON)) {
        if (check(TokenType::KW_NUMBER) ||
            check(TokenType::KW_STRING) ||
            check(TokenType::KW_BOOL) ||
            check(TokenType::KW_CHAR) ||
            check(TokenType::KW_CHARACTER)) {
            // 获取类型和名称
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

    // 循环体
    auto body = statement();

    return std::make_unique<ForStmt>(
        std::move(initializer),
        std::move(condition),
        std::move(increment),
        std::move(body)
    );
}

// 解析参数列表
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

// 解析函数调用
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

} // namespace collie