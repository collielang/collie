/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 抽象语法树节点定义
 */
#ifndef COLLIE_AST_H
#define COLLIE_AST_H

#include <memory>
#include <vector>
#include <string>
#include "../lexer/token.h"

namespace collie {

// 前向声明
class ExprVisitor;
class StmtVisitor;

/**
 * @brief 表达式基类
 * 所有具体的表达式类型都继承自这个基类
 */
class Expr {
public:
    virtual ~Expr() = default;
    virtual void accept(ExprVisitor& visitor) const = 0;
};

/**
 * @brief 语句基类
 * 所有具体的语句类型都继承自这个基类
 */
class Stmt {
public:
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor& visitor) const = 0;
};

/**
 * @brief 字面量表达式
 * 用于表示数字、字符串、布尔值等直接量
 */
class LiteralExpr : public Expr {
public:
    explicit LiteralExpr(Token token) : token_(token) {}
    void accept(ExprVisitor& visitor) const override;
    const Token& token() const { return token_; }

private:
    Token token_;
};

/**
 * @brief 标识符表达式
 * 用于表示变量名等标识符
 */
class IdentifierExpr : public Expr {
public:
    explicit IdentifierExpr(Token name) : name_(name) {}
    void accept(ExprVisitor& visitor) const override;
    const Token& name() const { return name_; }

private:
    Token name_;
};

/**
 * @brief 二元表达式
 * 用于表示加减乘除等需要两个操作数的表达式
 */
class BinaryExpr : public Expr {
public:
    BinaryExpr(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
        : left_(std::move(left)), operator_(op), right_(std::move(right)) {}

    void accept(ExprVisitor& visitor) const override;

    const Expr* left() const { return left_.get(); }
    const Token& op() const { return operator_; }
    const Expr* right() const { return right_.get(); }

private:
    std::unique_ptr<Expr> left_;
    Token operator_;
    std::unique_ptr<Expr> right_;
};

/**
 * @brief 一元表达式
 * 用于表示负号、逻辑非等只需要一个操作数的表达式
 */
class UnaryExpr : public Expr {
public:
    UnaryExpr(Token op, std::unique_ptr<Expr> operand)
        : operator_(op), operand_(std::move(operand)) {}

    void accept(ExprVisitor& visitor) const override;

    const Token& op() const { return operator_; }
    const Expr* operand() const { return operand_.get(); }

private:
    Token operator_;
    std::unique_ptr<Expr> operand_;
};

/**
 * @brief 表达式语句
 * 用于表示单个表达式作为语句使用的情况，如函数调用等
 */
class ExpressionStmt : public Stmt {
public:
    /**
     * @brief 构造表达式语句
     * @param expression 要执行的表达式
     */
    explicit ExpressionStmt(std::unique_ptr<Expr> expression)
        : expression_(std::move(expression)) {}

    void accept(StmtVisitor& visitor) const override;
    const Expr* expression() const { return expression_.get(); }

private:
    std::unique_ptr<Expr> expression_;
};

/**
 * @brief 变量声明语句
 * 用于声明和可选地初始化变量
 */
class VarDeclStmt : public Stmt {
public:
    /**
     * @brief 构造变量声明语句
     * @param type 变量类型
     * @param name 变量名
     * @param initializer 初始化表达式（可选）
     */
    VarDeclStmt(Token type, Token name, std::unique_ptr<Expr> initializer)
        : type_(type), name_(name), initializer_(std::move(initializer)) {}

    void accept(StmtVisitor& visitor) const override;
    const Token& type() const { return type_; }
    const Token& name() const { return name_; }
    const Expr* initializer() const { return initializer_.get(); }

private:
    Token type_;
    Token name_;
    std::unique_ptr<Expr> initializer_;
};

/**
 * @brief 块语句
 * 用于表示一组语句组成的代码块
 */
class BlockStmt : public Stmt {
public:
    /**
     * @brief 构造块语句
     * @param statements 块中的语句列表
     */
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> statements)
        : statements_(std::move(statements)) {}

    void accept(StmtVisitor& visitor) const override;
    const std::vector<std::unique_ptr<Stmt>>& statements() const { return statements_; }

private:
    std::vector<std::unique_ptr<Stmt>> statements_;
};

/**
 * @brief 赋值表达式
 * 用于表示变量赋值操作
 */
class AssignExpr : public Expr {
public:
    /**
     * @brief 构造赋值表达式
     * @param name 被赋值的变量名
     * @param value 要赋的值
     */
    AssignExpr(Token name, std::unique_ptr<Expr> value)
        : name_(name), value_(std::move(value)) {}

    void accept(ExprVisitor& visitor) const override;

    const Token& name() const { return name_; }
    const Expr* value() const { return value_.get(); }

private:
    Token name_;
    std::unique_ptr<Expr> value_;
};

/**
 * @brief If 语句
 * 用于表示条件分支语句，包括条件、then分支和可选的else分支
 */
class IfStmt : public Stmt {
public:
    /**
     * @brief 构造 if 语句
     * @param condition 条件表达式
     * @param then_branch then 分支语句
     * @param else_branch else 分支语句（可选）
     */
    IfStmt(std::unique_ptr<Expr> condition,
           std::unique_ptr<Stmt> then_branch,
           std::unique_ptr<Stmt> else_branch = nullptr);

    void accept(StmtVisitor& visitor) const override;

    const Expr* condition() const { return condition_.get(); }
    const Stmt* then_branch() const { return then_branch_.get(); }
    const Stmt* else_branch() const { return else_branch_.get(); }

private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Stmt> then_branch_;
    std::unique_ptr<Stmt> else_branch_;
};

/**
 * @brief While 语句
 * 用于表示 while 循环语句，包括循环条件和循环体
 */
class WhileStmt : public Stmt {
public:
    /**
     * @brief 构造 while 语句
     * @param condition 循环条件
     * @param body 循环体
     */
    WhileStmt(std::unique_ptr<Expr> condition,
              std::unique_ptr<Stmt> body);

    void accept(StmtVisitor& visitor) const override;

    const Expr* condition() const { return condition_.get(); }
    const Stmt* body() const { return body_.get(); }

private:
    std::unique_ptr<Expr> condition_;
    std::unique_ptr<Stmt> body_;
};

/**
 * @brief For 语句
 * 用于表示 for 循环语句，包括初始化、条件、增量和循环体
 */
class ForStmt : public Stmt {
public:
    /**
     * @brief 构造 for 语句
     * @param initializer 初始化语句（可以是变量声明或表达式语句）
     * @param condition 循环条件（可选）
     * @param increment 增量表达式（可选）
     * @param body 循环体
     */
    ForStmt(std::unique_ptr<Stmt> initializer,
            std::unique_ptr<Expr> condition,
            std::unique_ptr<Expr> increment,
            std::unique_ptr<Stmt> body);

    void accept(StmtVisitor& visitor) const override;

    const Stmt* initializer() const { return initializer_.get(); }
    const Expr* condition() const { return condition_.get(); }
    const Expr* increment() const { return increment_.get(); }
    const Stmt* body() const { return body_.get(); }

private:
    std::unique_ptr<Stmt> initializer_;  // 可以是变量声明或表达式语句
    std::unique_ptr<Expr> condition_;    // 可以为空
    std::unique_ptr<Expr> increment_;    // 可以为空
    std::unique_ptr<Stmt> body_;
};

/**
 * @brief 函数参数结构
 */
struct Parameter {
    Token type;  ///< 参数类型
    Token name;  ///< 参数名
};

/**
 * @brief 函数声明语句
 */
class FunctionStmt : public Stmt {
public:
    /**
     * @brief 构造函数声明语句
     * @param type 返回类型
     * @param name 函数名
     * @param parameters 参数列表
     * @param body 函数体
     */
    FunctionStmt(Token type, Token name,
                std::vector<Parameter> parameters,
                std::unique_ptr<BlockStmt> body);

    void accept(StmtVisitor& visitor) const override;

    const Token& return_type() const { return return_type_; }
    const Token& name() const { return name_; }
    const std::vector<Parameter>& parameters() const { return parameters_; }
    const BlockStmt* body() const { return body_.get(); }

private:
    Token return_type_;
    Token name_;
    std::vector<Parameter> parameters_;
    std::unique_ptr<BlockStmt> body_;
};

/**
 * @brief 函数调用表达式
 * 用于表示函数调用，包括函数名和参数列表
 */
class CallExpr : public Expr {
public:
    /**
     * @brief 构造函数调用表达式
     * @param callee 被调用的函数（通常是标识符表达式）
     * @param paren 右括号的位置（用于错误报告）
     * @param arguments 参数列表
     */
    CallExpr(std::unique_ptr<Expr> callee,
            Token paren,
            std::vector<std::unique_ptr<Expr>> arguments);

    void accept(ExprVisitor& visitor) const override;

    const Expr* callee() const { return callee_.get(); }
    const Token& paren() const { return paren_; }
    const std::vector<std::unique_ptr<Expr>>& arguments() const { return arguments_; }

private:
    std::unique_ptr<Expr> callee_;
    Token paren_;
    std::vector<std::unique_ptr<Expr>> arguments_;
};

/**
 * @brief 表达式访问者接口
 * 实现访问者模式，用于遍历和处理表达式节点
 */
class ExprVisitor {
public:
    virtual ~ExprVisitor() = default;

    /// @brief 访问字面量表达式
    virtual void visitLiteral(const LiteralExpr& expr) = 0;

    /// @brief 访问标识符表达式
    virtual void visitIdentifier(const IdentifierExpr& expr) = 0;

    /// @brief 访问二元表达式
    virtual void visitBinary(const BinaryExpr& expr) = 0;

    /// @brief 访问一元表达式
    virtual void visitUnary(const UnaryExpr& expr) = 0;

    /// @brief 访问赋值表达式
    virtual void visitAssign(const AssignExpr& expr) = 0;

    /// @brief 访问函数调用表达式
    virtual void visitCall(const CallExpr& expr) = 0;
};

/**
 * @brief 语句访问者接口
 * 实现访问者模式，用于遍历和处理语句节点
 */
class StmtVisitor {
public:
    virtual ~StmtVisitor() = default;

    /// @brief 访问表达式语句
    virtual void visitExpression(const ExpressionStmt& stmt) = 0;

    /// @brief 访问变量声明语句
    virtual void visitVarDecl(const VarDeclStmt& stmt) = 0;

    /// @brief 访问块语句
    virtual void visitBlock(const BlockStmt& stmt) = 0;

    /// @brief 访问 if 语句
    virtual void visitIf(const IfStmt& stmt) = 0;

    /// @brief 访问 while 语句
    virtual void visitWhile(const WhileStmt& stmt) = 0;

    /// @brief 访问 for 语句
    virtual void visitFor(const ForStmt& stmt) = 0;

    /// @brief 访问函数声明语句
    virtual void visitFunction(const FunctionStmt& stmt) = 0;
};

} // namespace collie

#endif // COLLIE_AST_H