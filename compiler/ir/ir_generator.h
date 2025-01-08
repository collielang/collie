/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 生成器，负责将 AST 转换为 IR
 */

#ifndef COLLIE_IR_GENERATOR_H
#define COLLIE_IR_GENERATOR_H

#include <memory>
#include "ir_node.h"
#include "../parser/ast.h"

namespace collie {
namespace ir {

// IR 生成器基类
class IRGenerator {
public:
    virtual ~IRGenerator() = default;

    // 生成 IR
    virtual std::shared_ptr<IRNode> generate(const ast::ASTNode* ast) = 0;
};

// 表达式 IR 生成器
class ExpressionIRGenerator : public IRGenerator {
public:
    std::shared_ptr<IRNode> generate(const ast::ASTNode* ast) override;

private:
    // 生成二元表达式的 IR
    std::shared_ptr<IRNode> generateBinaryExpr(const ast::BinaryExpr* expr);

    // 生成一元表达式的 IR
    std::shared_ptr<IRNode> generateUnaryExpr(const ast::UnaryExpr* expr);

    // 生成字面量的 IR
    std::shared_ptr<IRNode> generateLiteral(const ast::Literal* literal);

    // 生成标识符的 IR
    std::shared_ptr<IRNode> generateIdentifier(const ast::Identifier* id);
};

// 语句 IR 生成器
class StatementIRGenerator : public IRGenerator {
public:
    std::shared_ptr<IRNode> generate(const ast::ASTNode* ast) override;

private:
    // 生成赋值语句的 IR
    std::shared_ptr<IRNode> generateAssignment(const ast::AssignmentStmt* stmt);

    // 生成条件语句的 IR
    std::shared_ptr<IRNode> generateIfStmt(const ast::IfStmt* stmt);

    // 生成循环语句的 IR
    std::shared_ptr<IRNode> generateLoopStmt(const ast::LoopStmt* stmt);

    // 生成返回语句的 IR
    std::shared_ptr<IRNode> generateReturnStmt(const ast::ReturnStmt* stmt);
};

// 函数 IR 生成器
class FunctionIRGenerator : public IRGenerator {
public:
    std::shared_ptr<IRNode> generate(const ast::ASTNode* ast) override;

private:
    // 生成函数声明的 IR
    std::shared_ptr<IRNode> generateFunctionDecl(const ast::FunctionDecl* decl);

    // 生成函数调用的 IR
    std::shared_ptr<IRNode> generateFunctionCall(const ast::FunctionCall* call);

    // 生成函数参数的 IR
    std::shared_ptr<IRNode> generateParameter(const ast::Parameter* param);
};

// 主 IR 生成器
class MainIRGenerator : public IRGenerator {
public:
    std::shared_ptr<IRNode> generate(const ast::ASTNode* ast) override;

private:
    ExpressionIRGenerator exprGenerator_;
    StatementIRGenerator stmtGenerator_;
    FunctionIRGenerator funcGenerator_;
};

} // namespace ir
} // namespace collie

#endif // COLLIE_IR_GENERATOR_H