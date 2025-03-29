/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 生成器实现
 */

#include "ir_generator.h"
#include <stdexcept>

namespace collie {
namespace ir {

// ExpressionIRGenerator 实现
std::shared_ptr<IRNode> ExpressionIRGenerator::generate(const ast::ASTNode* ast) {
    if (auto* expr = dynamic_cast<const ast::BinaryExpr*>(ast)) {
        return generateBinaryExpr(expr);
    } else if (auto* expr = dynamic_cast<const ast::UnaryExpr*>(ast)) {
        return generateUnaryExpr(expr);
    } else if (auto* literal = dynamic_cast<const ast::Literal*>(ast)) {
        return generateLiteral(literal);
    } else if (auto* id = dynamic_cast<const ast::Identifier*>(ast)) {
        return generateIdentifier(id);
    }
    throw std::runtime_error("Unsupported expression type");
}

std::shared_ptr<IRNode> ExpressionIRGenerator::generateBinaryExpr(const ast::BinaryExpr* expr) {
    // 创建二元运算指令
    auto inst = std::make_shared<IRInstruction>(IROpType::ADD); // 默认为加法，后面会根据实际运算符修改

    // 生成左操作数
    auto left = generate(expr->getLeft());
    if (!left) {
        throw std::runtime_error("Failed to generate left operand");
    }
    inst->addOperand(std::dynamic_pointer_cast<IROperand>(left));

    // 生成右操作数
    auto right = generate(expr->getRight());
    if (!right) {
        throw std::runtime_error("Failed to generate right operand");
    }
    inst->addOperand(std::dynamic_pointer_cast<IROperand>(right));

    // 根据运算符类型设置操作类型
    switch (expr->getOperator()) {
        case ast::BinaryOperator::ADD:
            inst->setOpType(IROpType::ADD);
            break;
        case ast::BinaryOperator::SUB:
            inst->setOpType(IROpType::SUB);
            break;
        case ast::BinaryOperator::MUL:
            inst->setOpType(IROpType::MUL);
            break;
        case ast::BinaryOperator::DIV:
            inst->setOpType(IROpType::DIV);
            break;
        case ast::BinaryOperator::MOD:
            inst->setOpType(IROpType::MOD);
            break;
        case ast::BinaryOperator::AND:
            inst->setOpType(IROpType::AND);
            break;
        case ast::BinaryOperator::OR:
            inst->setOpType(IROpType::OR);
            break;
        case ast::BinaryOperator::XOR:
            inst->setOpType(IROpType::XOR);
            break;
        case ast::BinaryOperator::SHL:
            inst->setOpType(IROpType::SHL);
            break;
        case ast::BinaryOperator::SHR:
            inst->setOpType(IROpType::SHR);
            break;
        case ast::BinaryOperator::EQ:
            inst->setOpType(IROpType::EQ);
            break;
        case ast::BinaryOperator::NE:
            inst->setOpType(IROpType::NE);
            break;
        case ast::BinaryOperator::LT:
            inst->setOpType(IROpType::LT);
            break;
        case ast::BinaryOperator::LE:
            inst->setOpType(IROpType::LE);
            break;
        case ast::BinaryOperator::GT:
            inst->setOpType(IROpType::GT);
            break;
        case ast::BinaryOperator::GE:
            inst->setOpType(IROpType::GE);
            break;
        default:
            throw std::runtime_error("Unsupported binary operator");
    }

    return inst;
}

std::shared_ptr<IRNode> ExpressionIRGenerator::generateUnaryExpr(const ast::UnaryExpr* expr) {
    // 创建一元运算指令
    auto inst = std::make_shared<IRInstruction>(IROpType::NOT); // 默认为取反，后面会根据实际运算符修改

    // 生成操作数
    auto operand = generate(expr->getOperand());
    if (!operand) {
        throw std::runtime_error("Failed to generate unary operand");
    }
    inst->addOperand(std::dynamic_pointer_cast<IROperand>(operand));

    // 根据运算符类型设置操作类型
    switch (expr->getOperator()) {
        case ast::UnaryOperator::NOT:
            inst->setOpType(IROpType::NOT);
            break;
        case ast::UnaryOperator::NEG:
            // 对于负号，我们使用 0 减去操作数
            inst->setOpType(IROpType::SUB);
            auto zero = std::make_shared<IRConstant>(0);
            inst->addOperand(zero);
            break;
        default:
            throw std::runtime_error("Unsupported unary operator");
    }

    return inst;
}

std::shared_ptr<IRNode> ExpressionIRGenerator::generateLiteral(const ast::Literal* literal) {
    // 根据字面量类型创建对应的常量操作数
    switch (literal->getType()) {
        case ast::LiteralType::INTEGER:
            return std::make_shared<IRConstant>(literal->getIntValue());
        case ast::LiteralType::FLOAT:
            return std::make_shared<IRConstant>(literal->getFloatValue());
        case ast::LiteralType::BOOLEAN:
            return std::make_shared<IRConstant>(literal->getBoolValue());
        case ast::LiteralType::STRING:
            return std::make_shared<IRConstant>(literal->getStringValue());
        default:
            throw std::runtime_error("Unsupported literal type");
    }
}

std::shared_ptr<IRNode> ExpressionIRGenerator::generateIdentifier(const ast::Identifier* id) {
    // 创建变量操作数
    return std::make_shared<IRVariable>(id->getName());
}

// StatementIRGenerator 实现
std::shared_ptr<IRNode> StatementIRGenerator::generate(const ast::ASTNode* ast) {
    if (auto* stmt = dynamic_cast<const ast::AssignmentStmt*>(ast)) {
        return generateAssignment(stmt);
    } else if (auto* stmt = dynamic_cast<const ast::IfStmt*>(ast)) {
        return generateIfStmt(stmt);
    } else if (auto* stmt = dynamic_cast<const ast::LoopStmt*>(ast)) {
        return generateLoopStmt(stmt);
    } else if (auto* stmt = dynamic_cast<const ast::ReturnStmt*>(ast)) {
        return generateReturnStmt(stmt);
    }
    throw std::runtime_error("Unsupported statement type");
}

std::shared_ptr<IRNode> StatementIRGenerator::generateAssignment(const ast::AssignmentStmt* stmt) {
    // 创建赋值指令（使用 store 指令）
    auto inst = std::make_shared<IRInstruction>(IROpType::STORE);

    // 生成左值（目标变量）
    auto target = std::make_shared<IRVariable>(stmt->getTarget()->getName());
    inst->addOperand(target);

    // 生成右值（表达式）
    ExpressionIRGenerator exprGen;
    auto value = exprGen.generate(stmt->getValue());
    if (!value) {
        throw std::runtime_error("Failed to generate assignment value");
    }
    inst->addOperand(std::dynamic_pointer_cast<IROperand>(value));

    return inst;
}

std::shared_ptr<IRNode> StatementIRGenerator::generateIfStmt(const ast::IfStmt* stmt) {
    // 创建基本块
    auto function = std::make_shared<IRFunction>("if_stmt");

    // 条件基本块
    auto condBlock = std::make_shared<IRBasicBlock>("cond");

    // 生成条件表达式
    ExpressionIRGenerator exprGen;
    auto condition = exprGen.generate(stmt->getCondition());
    if (!condition) {
        throw std::runtime_error("Failed to generate if condition");
    }

    // 创建条件跳转指令
    auto cjmp = std::make_shared<IRInstruction>(IROpType::CJMP);
    cjmp->addOperand(std::dynamic_pointer_cast<IROperand>(condition));

    // 创建 then 和 else 基本块
    auto thenBlock = std::make_shared<IRBasicBlock>("then");
    auto elseBlock = std::make_shared<IRBasicBlock>("else");
    auto endBlock = std::make_shared<IRBasicBlock>("end");

    // 添加条件跳转的目标标签
    cjmp->addOperand(std::make_shared<IRLabel>(thenBlock->getLabel()));
    cjmp->addOperand(std::make_shared<IRLabel>(elseBlock->getLabel()));

    // 添加条件跳转指令到条件基本块
    condBlock->addInstruction(cjmp);

    // 生成 then 语句
    if (stmt->getThenStmt()) {
        auto thenStmt = generate(stmt->getThenStmt());
        if (auto* thenInst = dynamic_cast<IRInstruction*>(thenStmt.get())) {
            thenBlock->addInstruction(std::shared_ptr<IRInstruction>(thenInst));
        }
    }

    // 添加跳转到结束块的指令
    auto thenJmp = std::make_shared<IRInstruction>(IROpType::JMP);
    thenJmp->addOperand(std::make_shared<IRLabel>(endBlock->getLabel()));
    thenBlock->addInstruction(thenJmp);

    // 生成 else 语句（如果有）
    if (stmt->getElseStmt()) {
        auto elseStmt = generate(stmt->getElseStmt());
        if (auto* elseInst = dynamic_cast<IRInstruction*>(elseStmt.get())) {
            elseBlock->addInstruction(std::shared_ptr<IRInstruction>(elseInst));
        }
    }

    // 添加跳转到结束块的指令
    auto elseJmp = std::make_shared<IRInstruction>(IROpType::JMP);
    elseJmp->addOperand(std::make_shared<IRLabel>(endBlock->getLabel()));
    elseBlock->addInstruction(elseJmp);

    // 将所有基本块添加到函数中
    function->addBasicBlock(condBlock);
    function->addBasicBlock(thenBlock);
    function->addBasicBlock(elseBlock);
    function->addBasicBlock(endBlock);

    return function;
}

std::shared_ptr<IRNode> StatementIRGenerator::generateLoopStmt(const ast::LoopStmt* stmt) {
    // 创建基本块
    auto function = std::make_shared<IRFunction>("loop_stmt");

    // 创建循环的基本块
    auto headerBlock = std::make_shared<IRBasicBlock>("loop_header");
    auto bodyBlock = std::make_shared<IRBasicBlock>("loop_body");
    auto endBlock = std::make_shared<IRBasicBlock>("loop_end");

    // 生成循环条件（如果有）
    if (stmt->getCondition()) {
        ExpressionIRGenerator exprGen;
        auto condition = exprGen.generate(stmt->getCondition());
        if (!condition) {
            throw std::runtime_error("Failed to generate loop condition");
        }

        // 创建条件跳转指令
        auto cjmp = std::make_shared<IRInstruction>(IROpType::CJMP);
        cjmp->addOperand(std::dynamic_pointer_cast<IROperand>(condition));
        cjmp->addOperand(std::make_shared<IRLabel>(bodyBlock->getLabel()));
        cjmp->addOperand(std::make_shared<IRLabel>(endBlock->getLabel()));

        // 添加条件跳转指令到头部基本块
        headerBlock->addInstruction(cjmp);
    } else {
        // 如果没有条件（无限循环），直接跳转到循环体
        auto jmp = std::make_shared<IRInstruction>(IROpType::JMP);
        jmp->addOperand(std::make_shared<IRLabel>(bodyBlock->getLabel()));
        headerBlock->addInstruction(jmp);
    }

    // 生成循环体
    if (stmt->getBody()) {
        auto bodyStmt = generate(stmt->getBody());
        if (auto* bodyInst = dynamic_cast<IRInstruction*>(bodyStmt.get())) {
            bodyBlock->addInstruction(std::shared_ptr<IRInstruction>(bodyInst));
        }
    }

    // 添加跳转回循环头部的指令
    auto loopJmp = std::make_shared<IRInstruction>(IROpType::JMP);
    loopJmp->addOperand(std::make_shared<IRLabel>(headerBlock->getLabel()));
    bodyBlock->addInstruction(loopJmp);

    // 将所有基本块添加到函数中
    function->addBasicBlock(headerBlock);
    function->addBasicBlock(bodyBlock);
    function->addBasicBlock(endBlock);

    return function;
}

std::shared_ptr<IRNode> StatementIRGenerator::generateReturnStmt(const ast::ReturnStmt* stmt) {
    // 创建返回指令
    auto inst = std::make_shared<IRInstruction>(IROpType::RET);

    // 如果有返回值，生成返回值表达式
    if (stmt->getValue()) {
        ExpressionIRGenerator exprGen;
        auto value = exprGen.generate(stmt->getValue());
        if (!value) {
            throw std::runtime_error("Failed to generate return value");
        }
        inst->addOperand(std::dynamic_pointer_cast<IROperand>(value));
    }

    return inst;
}

// FunctionIRGenerator 实现
std::shared_ptr<IRNode> FunctionIRGenerator::generate(const ast::ASTNode* ast) {
    if (auto* decl = dynamic_cast<const ast::FunctionDecl*>(ast)) {
        return generateFunctionDecl(decl);
    } else if (auto* call = dynamic_cast<const ast::FunctionCall*>(ast)) {
        return generateFunctionCall(call);
    }
    throw std::runtime_error("Unsupported function node type");
}

std::shared_ptr<IRNode> FunctionIRGenerator::generateFunctionDecl(const ast::FunctionDecl* decl) {
    // TODO: 实现函数声明的 IR 生成
    return nullptr;
}

std::shared_ptr<IRNode> FunctionIRGenerator::generateFunctionCall(const ast::FunctionCall* call) {
    // TODO: 实现函数调用的 IR 生成
    return nullptr;
}

std::shared_ptr<IRNode> FunctionIRGenerator::generateParameter(const ast::Parameter* param) {
    // TODO: 实现函数参数的 IR 生成
    return nullptr;
}

// MainIRGenerator 实现
std::shared_ptr<IRNode> MainIRGenerator::generate(const ast::ASTNode* ast) {
    if (dynamic_cast<const ast::Expression*>(ast)) {
        return exprGenerator_.generate(ast);
    } else if (dynamic_cast<const ast::Statement*>(ast)) {
        return stmtGenerator_.generate(ast);
    } else if (dynamic_cast<const ast::Function*>(ast)) {
        return funcGenerator_.generate(ast);
    }
    throw std::runtime_error("Unsupported AST node type");
}

} // namespace ir
} // namespace collie
