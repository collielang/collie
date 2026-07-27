/*
 * @Author: Zhang Bokai <zbrook@126.com>
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
#include "../lexer/lexer.h"
#include "../utils/token_utils.h"

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
        // 记录本轮开始前的游标位置，用于死循环防护
        size_t before = current_;
        try {
            auto stmt = parse_declaration();
            if (stmt) {
                statements.push_back(std::move(stmt));
            } else {
                synchronize();
            }
        } catch (const ParseError& error) {
            report_error(error);
            synchronize();
        }
        // 进度守卫：若本轮未消费任何 token（例如 synchronize 停在无法处理的
        // 边界关键字上），强制前进一个 token，避免驱动循环死循环。
        if (current_ == before && !is_at_end()) {
            advance();
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
        // const 前缀：`const number x = 42;`
        bool is_const = match(TokenType::KW_CONST);

        // 检查是否是类型名开头的变量声明
        if (match({TokenType::KW_NUMBER,
                  TokenType::KW_STRING,
                  TokenType::KW_BOOL,
                  TokenType::KW_CHARACTER,
                  TokenType::KW_CHAR,
                  TokenType::KW_BYTE,
                  TokenType::KW_WORD,
                  TokenType::KW_DWORD,
                  TokenType::KW_NONE,
                  TokenType::KW_OBJECT,
                  TokenType::KW_INTEGER,
                  TokenType::KW_DECIMAL,
                  TokenType::KW_TRIBOOL,
                  TokenType::KW_BIT,
                  TokenType::KW_ARRAY,
                  TokenType::KW_TUPLE})) {
            return parse_type_declaration(is_const);
        }

        // const 后面必须跟类型名，否则报错
        if (is_const) {
            throw error(peek(), "Expect type name after 'const'.");
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

        if (match(TokenType::KW_CLASS)) {
            return parse_class_declaration();
        }

        auto stmt = parse_statement();
        if (!stmt) {
            throw error(peek(), "Expected declaration or statement.");
        }

        return stmt;

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
std::unique_ptr<Stmt> Parser::parse_type_declaration(bool is_const) {
    try {
        // 记录类型 token
        Token type = previous();

        // 解析变量名
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

        // 解析可选的初始化表达式
        std::unique_ptr<Expr> initializer = nullptr;
        if (match(TokenType::OP_ASSIGN)) {
            initializer = parse_expression();
            if (!initializer) {
                // 初始化表达式解析失败：parse_expression 内部已上报错误并
                // 同步到当前语句边界。此处直接放弃当前声明，交由驱动循环
                // （parse_program）继续解析后续语句，避免二次同步吞掉正确代码。
                return nullptr;
            }
        }

        // 确保语句以分号结束
        consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after variable declaration.");

        return std::make_unique<VarDeclStmt>(type, name, std::move(initializer), is_const);

    } catch (const ParseError& error) {
        report_error(error);
        synchronize();
        return nullptr;
    }
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
    auto expr = parse_ternary();
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

        // 索引赋值：arr[i] = value
        if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
            // 从原 IndexExpr 中取回子表达式的所有权，重组为 IndexAssignExpr。
            Token bracket = index->bracket();
            auto object = index->take_object();
            auto index_expr = index->take_index();
            return std::make_unique<IndexAssignExpr>(
                std::move(object), bracket, std::move(index_expr),
                std::move(value));
        }

        // 属性赋值：obj.name = value（如 this.name = n）
        if (auto* property = dynamic_cast<PropertyExpr*>(expr.get())) {
            // 从原 PropertyExpr 中取回对象子表达式，重组为 PropertyAssignExpr。
            Token name = property->name();
            auto object = property->take_object();
            return std::make_unique<PropertyAssignExpr>(
                std::move(object), name, std::move(value));
        }

        throw error(equals, "Invalid assignment target.");
    }

    // 复合赋值运算符：+=, -=, *=, /=, %=
    // 脱糖为 x = x op expr
    if (match({TokenType::OP_PLUS_ASSIGN, TokenType::OP_MINUS_ASSIGN,
               TokenType::OP_MULTIPLY_ASSIGN, TokenType::OP_DIVIDE_ASSIGN,
               TokenType::OP_MODULO_ASSIGN})) {
        Token op_token = previous();
        auto value = parse_assignment();
        if (!value) {
            throw error(peek(), "Expect expression after compound assignment operator.");
        }

        if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr.get())) {
            Token name = identifier->name();

            // 确定对应的二元运算符
            TokenType binary_op;
            std::string_view op_lexeme;
            switch (op_token.type()) {
                case TokenType::OP_PLUS_ASSIGN:     binary_op = TokenType::OP_PLUS; op_lexeme = "+"; break;
                case TokenType::OP_MINUS_ASSIGN:    binary_op = TokenType::OP_MINUS; op_lexeme = "-"; break;
                case TokenType::OP_MULTIPLY_ASSIGN: binary_op = TokenType::OP_MULTIPLY; op_lexeme = "*"; break;
                case TokenType::OP_DIVIDE_ASSIGN:   binary_op = TokenType::OP_DIVIDE; op_lexeme = "/"; break;
                case TokenType::OP_MODULO_ASSIGN:   binary_op = TokenType::OP_MODULO; op_lexeme = "%"; break;
                default: throw error(op_token, "Unknown compound assignment operator.");
            }

            // 构造 BinaryExpr: identifier op value
            Token bin_op(binary_op, op_lexeme, op_token.line(), op_token.column());
            auto lhs = std::make_unique<IdentifierExpr>(name);
            auto binary = std::make_unique<BinaryExpr>(std::move(lhs), bin_op, std::move(value));

            return std::make_unique<AssignExpr>(name, std::move(binary));
        }

        throw error(op_token, "Invalid compound assignment target.");
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_ternary() {
    auto expr = parse_logical_or();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    // `==?` 多路匹配（t44）：target ==? v1, v2: r1, v3: r2, default_r
    // 末尾裸表达式 = 默认分支；其余裸值与后面最近的「值: 结果」归组
    if (match(TokenType::OP_EQ_QUESTION)) {
        Token op = previous();
        std::vector<MultiMatchExpr::Branch> branches;
        std::unique_ptr<Expr> default_expr;
        std::vector<std::unique_ptr<Expr>> pending;  // 待归组的裸值
        while (true) {
            auto item = parse_ternary();
            if (!item) {
                throw error(peek(), "Expect expression in '==?' expression.");
            }
            if (match(TokenType::OP_COLON)) {
                // item 与之前的裸值归为一组候选值
                pending.push_back(std::move(item));
                auto result = parse_ternary();
                if (!result) {
                    throw error(peek(), "Expect result expression after ':' in '==?'.");
                }
                MultiMatchExpr::Branch branch;
                branch.values = std::move(pending);
                pending.clear();
                branch.result = std::move(result);
                branches.push_back(std::move(branch));
                if (match(TokenType::DELIMITER_COMMA)) {
                    continue;
                }
                break;
            }
            if (match(TokenType::DELIMITER_COMMA)) {
                pending.push_back(std::move(item));
                continue;
            }
            // 末尾裸表达式 = 默认分支；若前面还有未归组的裸值则为语法错误
            if (!pending.empty()) {
                throw error(op,
                    "Bare values in '==?' must join a 'value: result' branch; "
                    "only the trailing expression may be the default branch.");
            }
            default_expr = std::move(item);
            break;
        }
        if (branches.empty()) {
            throw error(op, "Expect at least one 'value: result' branch in '==?'.");
        }
        return std::make_unique<MultiMatchExpr>(std::move(expr), op,
                                                std::move(branches),
                                                std::move(default_expr));
    }

    if (match(TokenType::OP_QUESTION)) {
        Token question = previous();
        auto then_expr = parse_ternary();  // 右结合
        if (!then_expr) {
            throw error(peek(), "Expect expression after '?'.");
        }
        consume(TokenType::OP_COLON, "Expect ':' in ternary expression.");
        auto else_expr = parse_ternary();  // 右结合
        if (!else_expr) {
            throw error(peek(), "Expect expression after ':'.");
        }
        // tribool 三分支形式（t43）：cond ? then : else : unset；
        // 第二个 ':' 贪心归属最内层三元
        std::unique_ptr<Expr> unset_expr;
        if (match(TokenType::OP_COLON)) {
            unset_expr = parse_ternary();
            if (!unset_expr) {
                throw error(peek(), "Expect expression after second ':'.");
            }
        }
        return std::make_unique<TernaryExpr>(std::move(expr), question,
                                             std::move(then_expr), std::move(else_expr),
                                             std::move(unset_expr));
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
    auto expr = parse_bit_or();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match(TokenType::OP_AND)) {
        Token op = previous();
        auto right = parse_bit_or();
        if (!right) {
            throw error(peek(), "Expect expression after '&&'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

// 位运算优先级（t47，与 C 家族一致）：`|` < `^` < `&` < 相等比较；
// 移位 `<< >>` 另居关系比较与加减之间（见 parse_shift）
std::unique_ptr<Expr> Parser::parse_bit_or() {
    auto expr = parse_bit_xor();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match(TokenType::OP_BIT_OR)) {
        Token op = previous();
        auto right = parse_bit_xor();
        if (!right) {
            throw error(peek(), "Expect expression after '|'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_bit_xor() {
    auto expr = parse_bit_and();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match(TokenType::OP_BIT_XOR)) {
        Token op = previous();
        auto right = parse_bit_and();
        if (!right) {
            throw error(peek(), "Expect expression after '^'.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_bit_and() {
    auto expr = parse_equality();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match(TokenType::OP_BIT_AND)) {
        Token op = previous();
        auto right = parse_equality();
        if (!right) {
            throw error(peek(), "Expect expression after '&'.");
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
    auto expr = parse_shift();
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
        auto right = parse_shift();
        if (!right) {
            throw error(peek(), "Expect expression after comparison operator.");
        }
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_shift() {
    auto expr = parse_term();
    if (!expr) {
        throw error(peek(), "Expect expression.");
    }

    while (match({TokenType::OP_BIT_LSHIFT, TokenType::OP_BIT_RSHIFT})) {
        Token op = previous();
        auto right = parse_term();
        if (!right) {
            throw error(peek(), "Expect expression after shift operator.");
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
    if (match({TokenType::OP_NOT, TokenType::OP_MINUS, TokenType::OP_BIT_NOT})) {
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

    // 后缀链：索引 expr[index] 与方法调用 expr.method(args)，可混合链式
    // （如 arr[0].toString()、m[0][1]、n.toString()[0]）
    while (true) {
        if (check(TokenType::DELIMITER_LBRACKET)) {
            Token bracket = advance();
            auto index = parse_expression();
            if (!index) {
                throw error(peek(), "Expect index expression inside '[]'.");
            }
            consume(TokenType::DELIMITER_RBRACKET, "Expect ']' after index expression.");
            expr = std::make_unique<IndexExpr>(std::move(expr), bracket,
                                               std::move(index));
            continue;
        }
        if (check(TokenType::DELIMITER_DOT)) {
            advance(); // 消费 '.'
            Token name = consume(TokenType::IDENTIFIER, "Expect member name after '.'.");
            // 带括号为方法调用，无括号为属性访问（如 str.length）
            if (!check(TokenType::DELIMITER_LPAREN)) {
                expr = std::make_unique<PropertyExpr>(std::move(expr), name);
                continue;
            }
            advance(); // 消费 '('
            std::vector<std::unique_ptr<Expr>> arguments;
            if (!check(TokenType::DELIMITER_RPAREN)) {
                do {
                    auto argument = parse_expression();
                    if (!argument) {
                        throw error(peek(), "Expect expression in method arguments.");
                    }
                    arguments.push_back(std::move(argument));
                } while (match(TokenType::DELIMITER_COMMA));
            }
            consume(TokenType::DELIMITER_RPAREN, "Expect ')' after method arguments.");
            expr = std::make_unique<MethodCallExpr>(std::move(expr), name,
                                                    std::move(arguments));
            continue;
        }
        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    if (match(TokenType::LITERAL_NUMBER)) {
        return std::make_unique<LiteralExpr>(previous());
    }

    // 数组字面量：[a, b, c]，允许尾逗号，空数组为 []
    if (check(TokenType::DELIMITER_LBRACKET)) {
        Token bracket = advance();
        std::vector<std::unique_ptr<Expr>> elements;
        if (!check(TokenType::DELIMITER_RBRACKET)) {
            do {
                // 尾逗号：逗号后直接遇到 ']' 则结束
                if (check(TokenType::DELIMITER_RBRACKET)) {
                    break;
                }
                auto element = parse_expression();
                if (!element) {
                    throw error(peek(), "Expect expression in array literal.");
                }
                elements.push_back(std::move(element));
            } while (match(TokenType::DELIMITER_COMMA));
        }
        consume(TokenType::DELIMITER_RBRACKET, "Expect ']' after array elements.");
        return std::make_unique<ArrayLiteralExpr>(std::move(elements), bracket);
    }

    if (match(TokenType::LITERAL_STRING)) {
        return std::make_unique<LiteralExpr>(previous());
    }

    // 字符字面量 'a'（含 UTF-16 多字节字符，t47）
    if (match(TokenType::LITERAL_CHAR) || match(TokenType::LITERAL_CHARACTER)) {
        return std::make_unique<LiteralExpr>(previous());
    }

    // 插值字符串 @"...{expr}..."：脱糖为文本段与 toString(expr) 的 '+' 拼接
    if (match(TokenType::LITERAL_INTERPOLATED_STRING)) {
        return desugar_interpolated_string(previous());
    }

    if (match(TokenType::LITERAL_BOOL) || match(TokenType::KW_TRUE) || match(TokenType::KW_FALSE) ||
        match(TokenType::KW_UNSET)) {
        return std::make_unique<LiteralExpr>(previous());
    }

    // 对象实例化：new ClassName(arguments)
    if (match(TokenType::KW_NEW)) {
        Token class_name = consume(TokenType::IDENTIFIER, "Expect class name after 'new'.");
        consume(TokenType::DELIMITER_LPAREN, "Expect '(' after class name.");
        std::vector<std::unique_ptr<Expr>> arguments;
        if (!check(TokenType::DELIMITER_RPAREN)) {
            do {
                auto argument = parse_expression();
                if (!argument) {
                    throw error(peek(), "Expect expression in constructor arguments.");
                }
                arguments.push_back(std::move(argument));
            } while (match(TokenType::DELIMITER_COMMA));
        }
        consume(TokenType::DELIMITER_RPAREN, "Expect ')' after constructor arguments.");
        return std::make_unique<NewExpr>(class_name, std::move(arguments));
    }

    // this：类方法/构造器体内引用当前实例
    if (match(TokenType::KW_THIS)) {
        return std::make_unique<ThisExpr>(previous());
    }

    // base.method(args)：显式父类方法调用（C# 语义）；base 不是一等值，
    // 必须紧跟 `.方法名(实参)`，在 primary 层一次性解析完整调用
    if (match(TokenType::KW_BASE)) {
        Token keyword = previous();
        consume(TokenType::DELIMITER_DOT, "Expect '.' after 'base'.");
        Token method = consume(TokenType::IDENTIFIER,
                               "Expect method name after 'base.'.");
        consume(TokenType::DELIMITER_LPAREN, "Expect '(' after base method name.");
        std::vector<std::unique_ptr<Expr>> arguments;
        if (!check(TokenType::DELIMITER_RPAREN)) {
            do {
                if (arguments.size() >= 255) {
                    throw error(peek(), "Cannot have more than 255 arguments.");
                }
                auto argument = parse_expression();
                if (!argument) {
                    throw error(peek(), "Expect expression in base method arguments.");
                }
                arguments.push_back(std::move(argument));
            } while (match(TokenType::DELIMITER_COMMA));
        }
        consume(TokenType::DELIMITER_RPAREN, "Expect ')' after base method arguments.");
        return std::make_unique<BaseMethodCallExpr>(keyword, method,
                                                    std::move(arguments));
    }

    if (match(TokenType::IDENTIFIER)) {
        Token name = previous();
        // 检查是否是函数调用
        if (check(TokenType::DELIMITER_LPAREN)) {
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
                std::make_unique<IdentifierExpr>(name),
                paren,
                std::move(arguments)
            );
        }

        return std::make_unique<IdentifierExpr>(name);
    }

    if (match(TokenType::DELIMITER_LPAREN)) {
        Token left_paren = previous();

        // 空括号 () 解析为空元组
        if (check(TokenType::DELIMITER_RPAREN)) {
            advance();  // 消费 ')'
            return std::make_unique<TupleExpr>(
                std::vector<std::unique_ptr<Expr>>{},
                std::vector<std::string>{}, left_paren);
        }

        // 命名元素前瞻：IDENTIFIER ':' 开头即为命名元组（t45，经作者确认）
        // 读取一个「可选名字 + 表达式」元素，无名元素名字为空串
        auto parse_tuple_element = [&](std::string& name) -> std::unique_ptr<Expr> {
            name.clear();
            if (check(TokenType::IDENTIFIER) &&
                peek_next().type() == TokenType::OP_COLON) {
                name = std::string(advance().lexeme());  // 名字
                advance();                               // ':'
            }
            return parse_expression();
        };

        // 先解析第一个元素，再区分：
        //   (expr)                  -> 分组表达式，透明返回内部表达式
        //   (expr, expr, ...)       -> 无名元组
        //   (name: expr, ...)       -> 命名元组（首元素带名时无逗号也是元组）
        std::string first_name;
        auto first = parse_tuple_element(first_name);
        if (!first_name.empty() || check(TokenType::DELIMITER_COMMA)) {
            std::vector<std::unique_ptr<Expr>> elements;
            std::vector<std::string> names;
            elements.push_back(std::move(first));
            names.push_back(std::move(first_name));
            while (match(TokenType::DELIMITER_COMMA)) {
                std::string name;
                auto element = parse_tuple_element(name);
                if (element) {
                    elements.push_back(std::move(element));
                    names.push_back(std::move(name));
                }
            }
            consume(TokenType::DELIMITER_RPAREN, "Expect ')' after tuple elements.");
            return std::make_unique<TupleExpr>(
                std::move(elements), std::move(names), left_paren);
        }

        consume(TokenType::DELIMITER_RPAREN, "Expect ')' after expression.");
        return first;
    }

    throw error(peek(), "Expect expression.");
}

std::unique_ptr<Expr> Parser::desugar_interpolated_string(const Token& token) {
    // 脱糖规则（见设计文档 03-character.md）：@"a{x}b" → "a" + toString(x) + "b"。
    // lexeme 为引号内原文（转义未解码）：文本段在此解码，额外支持 \{ \} 输出
    // 字面花括号；插值段用子 Lexer/Parser 解析，段内 \" 解码为引号后再喂给子
    // 解析器（允许 @"{ \"x\" }" 在插值内写字符串字面量）。
    const std::string raw(token.lexeme());
    size_t line = token.line();
    size_t column = token.column();

    // 文本段转义解码（与单行字符串一致，额外支持 \{ \}）
    auto decode_text = [&](const std::string& text) {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\\') {
                out += text[i];
                continue;
            }
            if (i + 1 >= text.size()) {
                throw error(token, "Invalid escape sequence in interpolated string");
            }
            switch (text[++i]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '{': out += '{'; break;
                case '}': out += '}'; break;
                default:
                    throw error(token, "Invalid escape sequence in interpolated string");
            }
        }
        return out;
    };

    std::vector<std::unique_ptr<Expr>> parts;
    auto flush_text = [&](std::string& text) {
        if (text.empty()) return;
        parts.push_back(std::make_unique<LiteralExpr>(
            Token(TokenType::LITERAL_STRING, decode_text(text), line, column)));
        text.clear();
    };

    std::string text;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            // 转义序列原样留在文本段，由 decode_text 统一解码
            text += c;
            text += raw[++i];
            continue;
        }
        if (c == '}') {
            throw error(token, "Unmatched '}' in interpolated string");
        }
        if (c != '{') {
            text += c;
            continue;
        }

        // 插值段：收集到匹配的 '}'（按深度计数，段内 \" 解码为引号）
        std::string expr_src;
        size_t depth = 1;
        ++i;
        for (; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '"') {
                expr_src += '"';
                ++i;
                continue;
            }
            if (raw[i] == '{') {
                ++depth;
            } else if (raw[i] == '}' && --depth == 0) {
                break;
            }
            expr_src += raw[i];
        }
        if (depth != 0) {
            throw error(token, "Unmatched '{' in interpolated string");
        }

        flush_text(text);

        // 子解析插值表达式；子解析器的错误统一映射到外层 token 位置
        std::unique_ptr<Expr> inner;
        try {
            Lexer sub_lexer(expr_src);
            std::vector<Token> sub_tokens = sub_lexer.tokenize();
            for (const Token& t : sub_tokens) {
                if (t.type() == TokenType::TOKEN_ERROR) {
                    throw ParseError("lex error", t.line(), t.column());
                }
            }
            Parser sub_parser(sub_tokens);
            inner = sub_parser.parse_expression();
            if (!inner || !sub_parser.is_at_end()) {
                throw ParseError("incomplete expression", line, column);
            }
        } catch (const std::exception&) {
            throw error(token, "Invalid expression in string interpolation");
        }

        // 包一层 toString(...)，保证段类型为 string（与内建函数行为一致）
        std::vector<std::unique_ptr<Expr>> args;
        args.push_back(std::move(inner));
        parts.push_back(std::make_unique<CallExpr>(
            std::make_unique<IdentifierExpr>(
                Token(TokenType::IDENTIFIER, "toString", line, column)),
            Token(TokenType::DELIMITER_RPAREN, ")", line, column),
            std::move(args)));
    }
    flush_text(text);

    // 空串或纯文本退化为普通字符串字面量；多段用 '+' 左结合串接
    if (parts.empty()) {
        return std::make_unique<LiteralExpr>(
            Token(TokenType::LITERAL_STRING, "", line, column));
    }
    std::unique_ptr<Expr> result = std::move(parts[0]);
    Token plus(TokenType::OP_PLUS, "+", line, column);
    for (size_t k = 1; k < parts.size(); ++k) {
        result = std::make_unique<BinaryExpr>(std::move(result), plus,
                                              std::move(parts[k]));
    }
    return result;
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

Token Parser::consume_type_token(const std::string& message) {
    // 接受 IDENTIFIER 或任何类型关键字作为合法的类型标识（与 parse_declaration 对齐）
    TokenType t = peek().type();
    if (t == TokenType::IDENTIFIER ||
        t == TokenType::KW_NUMBER  || t == TokenType::KW_STRING ||
        t == TokenType::KW_BOOL    || t == TokenType::KW_NONE   ||
        t == TokenType::KW_VOID    || t == TokenType::KW_CHAR   ||
        t == TokenType::KW_CHARACTER || t == TokenType::KW_BYTE ||
        t == TokenType::KW_WORD    || t == TokenType::KW_OBJECT ||
        t == TokenType::KW_ARRAY   || t == TokenType::KW_TUPLE  ||
        t == TokenType::KW_INTEGER || t == TokenType::KW_DECIMAL ||
        t == TokenType::KW_TRIBOOL || t == TokenType::KW_DWORD  ||
        t == TokenType::KW_BIT) {
        return advance();
    }
    throw ParseError(message, peek().line(), peek().column());
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) {
        return false;
    }
    bool result = peek().type() == type;
    return result;
}

bool Parser::is_at_end() const {
    if (current_ >= tokens_.size()) {
        return true;
    }
    return peek().type() == TokenType::END_OF_FILE;
}

Token Parser::peek() const {
    if (current_ >= tokens_.size()) {
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
        if (match(TokenType::KW_DO)) {
            return parse_do_while_statement();
        }
        if (match(TokenType::KW_SWITCH)) {
            return parse_switch_statement();
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

std::unique_ptr<Stmt> Parser::parse_do_while_statement() {
    Token do_token = previous(); // KW_DO 已被 match 消费

    // 增加循环嵌套深度
    ++nesting_depth_;
    check_max_nesting_depth();

    // 解析循环体（必须是块语句）
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' after 'do'.");
    auto body = parse_block_statement();

    // 减少循环嵌套深度
    --nesting_depth_;

    // while (condition);
    consume(TokenType::KW_WHILE, "Expect 'while' after do-while body.");
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'while'.");
    auto condition = parse_expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after do-while condition.");
    consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after do-while statement.");

    return std::make_unique<DoWhileStmt>(
        do_token,
        std::move(body),
        std::move(condition)
    );
}

std::unique_ptr<Stmt> Parser::parse_switch_statement() {
    Token switch_token = previous(); // KW_SWITCH 已被 match 消费

    // switch (expr)
    consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'switch'.");
    auto condition = parse_expression();
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after switch expression.");

    // switch body: { case1 { } case2 { } default { } }
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before switch cases.");

    std::vector<SwitchCase> cases;
    bool has_default = false;

    while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
        SwitchCase sc;

        if (match(TokenType::KW_DEFAULT)) {
            // default 分支
            if (has_default) {
                throw error(previous(), "Duplicate 'default' in switch.");
            }
            has_default = true;
            sc.is_default = true;
        } else {
            // 值分支：解析逗号分隔的表达式列表
            sc.values.push_back(parse_expression());
            while (match(TokenType::DELIMITER_COMMA)) {
                sc.values.push_back(parse_expression());
            }
        }

        // 每个分支的执行体必须是块语句
        consume(TokenType::DELIMITER_LBRACE, "Expect '{' after switch case value.");
        sc.body = parse_block_statement();

        cases.push_back(std::move(sc));
    }

    consume(TokenType::DELIMITER_RBRACE, "Expect '}' after switch body.");

    return std::make_unique<SwitchStmt>(
        switch_token,
        std::move(condition),
        std::move(cases)
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
        // 记录本轮开始前的游标位置，用于死循环防护
        size_t before = current_;
        auto stmt = parse_declaration();
        if (stmt) {
            statements.push_back(std::move(stmt));
        } else {
            synchronize();
        }
        // 进度守卫：若本轮未消费任何 token，强制前进一个 token，
        // 避免块语句驱动循环死循环。
        if (current_ == before && !check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
            advance();
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

    // 解析初始化语句（类型关键字列表与 parse_declaration 对齐，t51 补齐 integer/decimal 等）
    std::unique_ptr<Stmt> initializer;
    if (match(TokenType::DELIMITER_SEMICOLON)) {
        initializer = nullptr;
    } else if (match({TokenType::KW_NUMBER,
                      TokenType::KW_STRING,
                      TokenType::KW_BOOL,
                      TokenType::KW_CHARACTER,
                      TokenType::KW_CHAR,
                      TokenType::KW_BYTE,
                      TokenType::KW_WORD,
                      TokenType::KW_DWORD,
                      TokenType::KW_OBJECT,
                      TokenType::KW_INTEGER,
                      TokenType::KW_DECIMAL,
                      TokenType::KW_TRIBOOL,
                      TokenType::KW_BIT,
                      TokenType::KW_ARRAY,
                      TokenType::KW_TUPLE}) ||
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
std::unique_ptr<Stmt> Parser::parse_function_declaration(bool is_override) {
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
            Token param_type = consume_type_token("Expect parameter type.");
            parameters.emplace_back(Parameter{param_type, param_name});
        } while (match(TokenType::DELIMITER_COMMA));
    }
    consume(TokenType::DELIMITER_RPAREN, "Expect ')' after parameters.");

    // 解析返回类型
    Token return_type = consume_type_token("Expect function return type.");

    // 解析函数体
    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before function body.");
    auto body = std::unique_ptr<BlockStmt>(dynamic_cast<BlockStmt*>(parse_block_statement().release()));

    return std::make_unique<FunctionStmt>(return_type, name, std::move(parameters), std::move(body), is_override);
}

/**
 * @brief 解析类声明（`class` 已消费）
 *
 * 文法（Java/C# 风格最小子集，见 uncategorized.md 附录，经作者确认）：
 *   classDecl -> "class" IDENTIFIER ("extends" IDENTIFIER)? "{" classMember* "}"
 */
std::unique_ptr<Stmt> Parser::parse_class_declaration() {
    Token name = consume(TokenType::IDENTIFIER, "Expect class name.");

    // 可选单继承：`extends 父类名`（无父类时用默认构造的 INVALID token 表示）
    Token superclass;
    if (match(TokenType::KW_EXTENDS)) {
        superclass = consume(TokenType::IDENTIFIER, "Expect superclass name after 'extends'.");
    }

    consume(TokenType::DELIMITER_LBRACE, "Expect '{' before class body.");

    std::vector<std::unique_ptr<Stmt>> members;
    while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
        auto member = parse_class_member(name);
        if (member) {
            members.push_back(std::move(member));
        }
    }

    consume(TokenType::DELIMITER_RBRACE, "Expect '}' after class body.");
    return std::make_unique<ClassStmt>(name, superclass, std::move(members));
}

/**
 * @brief 解析单个类成员
 *
 * 文法：
 *   classMember -> annotation* ("public" | "private")? (field | method | constructor)
 *   annotation  -> "@override" | "@deprecated"（仅类方法；@deprecated 暂仅接受不生效）
 *   field       -> typeToken IDENTIFIER ("=" expression)? ";"
 *   method      -> "function" IDENTIFIER "(" parameters? ")" typeToken block
 *   constructor -> 类名 IDENTIFIER "(" parameters? ")" (":" "base" "(" arguments? ")")? block
 *                  （与类名同名，无返回类型；base 委托脱糖为构造器体首条语句）
 */
std::unique_ptr<Stmt> Parser::parse_class_member(const Token& class_name) {
    // 注解（可多个，在访问修饰符之前，见 uncategorized.md）：
    // @override 标记覆写（语义层校验父类链确有同名方法）；
    // @deprecated 接受但暂不生效（TODO：调用处告警）；其余报错
    bool is_override = false;
    while (check(TokenType::ANNOTATION)) {
        Token annotation = advance();
        if (annotation.lexeme() == "override") {
            is_override = true;
        } else if (annotation.lexeme() != "deprecated") {
            throw error(annotation, "Unknown annotation '@" +
                                        std::string(annotation.lexeme()) + "'.");
        }
    }

    // 可选访问修饰符（缺省为 public，与 Stmt 基类默认值一致）
    bool is_public = true;
    if (match(TokenType::KW_PUBLIC)) {
        is_public = true;
    } else if (match(TokenType::KW_PRIVATE)) {
        is_public = false;
    }

    std::unique_ptr<Stmt> member;

    if (match(TokenType::KW_FUNCTION)) {
        // 方法：复用函数声明文法
        member = parse_function_declaration(is_override);
    } else if (is_override) {
        // @override 只能标注方法（字段/构造器不可覆写）
        throw error(peek(), "'@override' can only be applied to methods.");
    } else if (check(TokenType::IDENTIFIER) &&
               peek().lexeme() == class_name.lexeme() &&
               peek_next().type() == TokenType::DELIMITER_LPAREN) {
        // 构造器：与类名同名，无 function 前缀与返回类型
        Token ctor_name = advance();
        consume(TokenType::DELIMITER_LPAREN, "Expect '(' after constructor name.");
        std::vector<Parameter> parameters;
        if (!check(TokenType::DELIMITER_RPAREN)) {
            do {
                if (parameters.size() >= 255) {
                    throw error(peek(), "Cannot have more than 255 parameters.");
                }
                Token param_name = consume(TokenType::IDENTIFIER, "Expect parameter name.");
                Token param_type = consume_type_token("Expect parameter type.");
                parameters.emplace_back(Parameter{param_type, param_name});
            } while (match(TokenType::DELIMITER_COMMA));
        }
        consume(TokenType::DELIMITER_RPAREN, "Expect ')' after parameters.");

        // 可选构造器委托：`: base(arguments)`（见 uncategorized.md 附录），
        // 脱糖为构造器体首条 ExpressionStmt(BaseCallExpr)
        std::unique_ptr<Stmt> base_call_stmt;
        if (match(TokenType::OP_COLON)) {
            Token base_keyword = consume(TokenType::KW_BASE,
                                         "Expect 'base' after ':' in constructor.");
            consume(TokenType::DELIMITER_LPAREN, "Expect '(' after 'base'.");
            std::vector<std::unique_ptr<Expr>> base_args;
            if (!check(TokenType::DELIMITER_RPAREN)) {
                do {
                    if (base_args.size() >= 255) {
                        throw error(peek(), "Cannot have more than 255 arguments.");
                    }
                    auto arg = parse_expression();
                    if (!arg) {
                        throw error(peek(), "Expect expression in base arguments.");
                    }
                    base_args.push_back(std::move(arg));
                } while (match(TokenType::DELIMITER_COMMA));
            }
            consume(TokenType::DELIMITER_RPAREN, "Expect ')' after base arguments.");
            base_call_stmt = std::make_unique<ExpressionStmt>(
                std::make_unique<BaseCallExpr>(base_keyword, std::move(base_args)));
        }

        consume(TokenType::DELIMITER_LBRACE, "Expect '{' before constructor body.");
        // BlockStmt 构造后不可变，需先把 base 委托语句放入首位再收集体内语句，
        // 故不复用 parse_block_statement，手动展开同模式的驱动循环
        std::vector<std::unique_ptr<Stmt>> body_statements;
        if (base_call_stmt) {
            body_statements.push_back(std::move(base_call_stmt));
        }
        while (!check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
            size_t before = current_;
            auto stmt = parse_declaration();
            if (stmt) {
                body_statements.push_back(std::move(stmt));
            } else {
                synchronize();
            }
            // 进度守卫：若本轮未消费任何 token，强制前进一个 token
            if (current_ == before && !check(TokenType::DELIMITER_RBRACE) && !is_at_end()) {
                advance();
            }
        }
        consume(TokenType::DELIMITER_RBRACE, "Expect '}' after constructor body.");
        auto body = std::make_unique<BlockStmt>(std::move(body_statements));
        // 构造器不写返回类型，合成 none 类型 token 复用 FunctionStmt 节点
        Token return_type(TokenType::KW_NONE, "none", ctor_name.line(), ctor_name.column());
        member = std::make_unique<FunctionStmt>(return_type, ctor_name,
                                                std::move(parameters), std::move(body));
    } else {
        // 字段：`type name;` 或 `type name = expr;`
        Token type = consume_type_token("Expect field type in class body.");
        Token name = consume(TokenType::IDENTIFIER, "Expect field name.");
        std::unique_ptr<Expr> initializer;
        if (match(TokenType::OP_ASSIGN)) {
            initializer = parse_expression();
            if (!initializer) {
                throw error(peek(), "Expect expression after '=' in field declaration.");
            }
        }
        consume(TokenType::DELIMITER_SEMICOLON, "Expect ';' after field declaration.");
        member = std::make_unique<VarDeclStmt>(type, name, std::move(initializer), false);
    }

    member->set_access(is_public);
    return member;
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

Token Parser::peek_next() const {
    if (current_ + 1 >= tokens_.size()) {
        return Token(TokenType::END_OF_FILE, "", 0, 0);
    }
    return tokens_[current_ + 1];
}
} // namespace collie
