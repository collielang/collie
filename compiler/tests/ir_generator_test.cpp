/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 生成器单元测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_generator.h"
#include "../parser/ast.h"

using namespace collie;
using namespace collie::ir;

// 测试表达式 IR 生成
TEST(IRGeneratorTest, ExpressionGeneration) {
    // 创建一个简单的二元表达式：1 + 2
    auto left = std::make_shared<ast::Literal>(1);
    auto right = std::make_shared<ast::Literal>(2);
    auto expr = std::make_shared<ast::BinaryExpr>(left, ast::BinaryOperator::ADD, right);

    // 生成 IR
    ExpressionIRGenerator generator;
    auto ir = generator.generate(expr.get());
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR
    auto inst = std::dynamic_pointer_cast<IRInstruction>(ir);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->getOpType(), IROpType::ADD);

    // 验证操作数
    const auto& operands = inst->getOperands();
    ASSERT_EQ(operands.size(), 2);

    auto leftOp = std::dynamic_pointer_cast<IRConstant>(operands[0]);
    auto rightOp = std::dynamic_pointer_cast<IRConstant>(operands[1]);
    ASSERT_NE(leftOp, nullptr);
    ASSERT_NE(rightOp, nullptr);

    EXPECT_EQ(std::get<int64_t>(leftOp->getValue()), 1);
    EXPECT_EQ(std::get<int64_t>(rightOp->getValue()), 2);
}

// 测试赋值语句 IR 生成
TEST(IRGeneratorTest, AssignmentGeneration) {
    // 创建一个简单的赋值语句：x = 42
    auto target = std::make_shared<ast::Identifier>("x");
    auto value = std::make_shared<ast::Literal>(42);
    auto stmt = std::make_shared<ast::AssignmentStmt>(target, value);

    // 生成 IR
    StatementIRGenerator generator;
    auto ir = generator.generate(stmt.get());
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR
    auto inst = std::dynamic_pointer_cast<IRInstruction>(ir);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->getOpType(), IROpType::STORE);

    // 验证操作数
    const auto& operands = inst->getOperands();
    ASSERT_EQ(operands.size(), 2);

    auto targetOp = std::dynamic_pointer_cast<IRVariable>(operands[0]);
    auto valueOp = std::dynamic_pointer_cast<IRConstant>(operands[1]);
    ASSERT_NE(targetOp, nullptr);
    ASSERT_NE(valueOp, nullptr);

    EXPECT_EQ(targetOp->getName(), "x");
    EXPECT_EQ(std::get<int64_t>(valueOp->getValue()), 42);
}

// 测试条件语句 IR 生成
TEST(IRGeneratorTest, IfStatementGeneration) {
    // 创建一个简单的条件语句：if (x > 0) y = 1; else y = 2;
    auto condition = std::make_shared<ast::BinaryExpr>(
        std::make_shared<ast::Identifier>("x"),
        ast::BinaryOperator::GT,
        std::make_shared<ast::Literal>(0)
    );

    auto thenStmt = std::make_shared<ast::AssignmentStmt>(
        std::make_shared<ast::Identifier>("y"),
        std::make_shared<ast::Literal>(1)
    );

    auto elseStmt = std::make_shared<ast::AssignmentStmt>(
        std::make_shared<ast::Identifier>("y"),
        std::make_shared<ast::Literal>(2)
    );

    auto stmt = std::make_shared<ast::IfStmt>(condition, thenStmt, elseStmt);

    // 生成 IR
    StatementIRGenerator generator;
    auto ir = generator.generate(stmt.get());
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR
    auto func = std::dynamic_pointer_cast<IRFunction>(ir);
    ASSERT_NE(func, nullptr);

    // 验证基本块
    const auto& blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4); // cond, then, else, end
}

// 测试函数声明 IR 生成
TEST(IRGeneratorTest, FunctionDeclarationGeneration) {
    // 创建一个简单的函数声明：function add(a, b) { return a + b; }
    std::vector<std::shared_ptr<ast::Parameter>> params = {
        std::make_shared<ast::Parameter>("a"),
        std::make_shared<ast::Parameter>("b")
    };

    auto body = std::make_shared<ast::ReturnStmt>(
        std::make_shared<ast::BinaryExpr>(
            std::make_shared<ast::Identifier>("a"),
            ast::BinaryOperator::ADD,
            std::make_shared<ast::Identifier>("b")
        )
    );

    auto decl = std::make_shared<ast::FunctionDecl>("add", params, body);

    // 生成 IR
    FunctionIRGenerator generator;
    auto ir = generator.generate(decl.get());
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR
    auto func = std::dynamic_pointer_cast<IRFunction>(ir);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "add");

    // 验证基本块
    const auto& blocks = func->getBasicBlocks();
    ASSERT_GE(blocks.size(), 1);

    // 验证入口基本块
    const auto& entryBlock = blocks[0];
    const auto& instructions = entryBlock->getInstructions();
    ASSERT_GE(instructions.size(), 4); // 至少包含两个参数的 alloca 和 store 指令
}