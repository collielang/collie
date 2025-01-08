/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 生成器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_generator.h"
#include "../ir/ir_node.h"
#include "../parser/ast.h"

namespace collie {
namespace ir {
namespace test {

/**
 * IR 生成器测试
 */
class IRGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建各种类型的生成器
        generator_ = std::make_unique<IRGenerator>();
        exprGenerator_ = std::make_unique<ExpressionIRGenerator>();
        stmtGenerator_ = std::make_unique<StatementIRGenerator>();
        funcGenerator_ = std::make_unique<FunctionIRGenerator>();
    }

    std::unique_ptr<IRGenerator> generator_;
    std::unique_ptr<ExpressionIRGenerator> exprGenerator_;
    std::unique_ptr<StatementIRGenerator> stmtGenerator_;
    std::unique_ptr<FunctionIRGenerator> funcGenerator_;
};

/**
 * 表达式生成测试
 */
TEST_F(IRGeneratorTest, ExpressionGeneration) {
    // 创建一个简单的二元表达式：1 + 2
    auto left = std::make_shared<parser::NumberLiteral>(1);
    auto right = std::make_shared<parser::NumberLiteral>(2);
    auto expr = std::make_shared<parser::BinaryExpression>(
        parser::BinaryOperator::ADD,
        left,
        right
    );

    // 使用专门的表达式生成器
    auto ir = exprGenerator_->generate(expr.get());
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

    // 使用通用生成器
    auto ir2 = generator_->generateExpression(expr);
    ASSERT_NE(ir2, nullptr);

    // 验证两种方式生成的 IR 是等价的
    auto inst2 = std::dynamic_pointer_cast<IRInstruction>(ir2);
    ASSERT_NE(inst2, nullptr);
    EXPECT_EQ(inst2->getOpType(), inst->getOpType());
}

/**
 * 常量表达式生成测试
 */
TEST_F(IRGeneratorTest, ConstantExpression) {
    // 创建一个常量表达式的 AST 节点
    auto ast = std::make_shared<parser::NumberLiteral>(42);

    // 生成 IR
    auto ir = generator_->generateExpression(ast);
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR 是一个常量
    auto constant = std::dynamic_pointer_cast<IRConstant>(ir);
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(std::get<int64_t>(constant->getValue()), 42);
}

/**
 * 二元表达式生成测试
 */
TEST_F(IRGeneratorTest, BinaryExpression) {
    // 创建一个二元表达式的 AST 节点：1 + 2
    auto left = std::make_shared<parser::NumberLiteral>(1);
    auto right = std::make_shared<parser::NumberLiteral>(2);
    auto ast = std::make_shared<parser::BinaryExpression>(
        parser::BinaryOperator::ADD,
        left,
        right
    );

    // 生成 IR
    auto ir = generator_->generateExpression(ast);
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR 是一个指令
    auto inst = std::dynamic_pointer_cast<IRInstruction>(ir);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->getOpType(), IROpType::ADD);

    // 验证操作数
    const auto& operands = inst->getOperands();
    ASSERT_EQ(operands.size(), 2);

    auto leftConst = std::dynamic_pointer_cast<IRConstant>(operands[0]);
    auto rightConst = std::dynamic_pointer_cast<IRConstant>(operands[1]);

    ASSERT_NE(leftConst, nullptr);
    ASSERT_NE(rightConst, nullptr);
    EXPECT_EQ(std::get<int64_t>(leftConst->getValue()), 1);
    EXPECT_EQ(std::get<int64_t>(rightConst->getValue()), 2);
}

/**
 * 赋值语句生成测试
 */
TEST_F(IRGeneratorTest, AssignmentGeneration) {
    // 创建一个简单的赋值语句：x = 42
    auto target = std::make_shared<parser::Identifier>("x");
    auto value = std::make_shared<parser::NumberLiteral>(42);
    auto stmt = std::make_shared<parser::AssignmentStatement>(target, value);

    // 使用专门的语句生成器
    auto ir = stmtGenerator_->generate(stmt.get());
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR
    auto inst = std::dynamic_pointer_cast<IRInstruction>(ir);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->getOpType(), IROpType::STORE);

    // 验证操作数
    const auto& operands = inst->getOperands();
    ASSERT_EQ(operands.size(), 2);

    auto targetOp = std::dynamic_pointer_cast<IRVariable>(operands[1]);
    auto valueOp = std::dynamic_pointer_cast<IRConstant>(operands[0]);
    ASSERT_NE(targetOp, nullptr);
    ASSERT_NE(valueOp, nullptr);

    EXPECT_EQ(targetOp->getName(), "x");
    EXPECT_EQ(std::get<int64_t>(valueOp->getValue()), 42);
}

/**
 * 变量声明生成测试
 */
TEST_F(IRGeneratorTest, VariableDeclaration) {
    // 创建一个变量声明的 AST 节点：int x = 10
    auto init = std::make_shared<parser::NumberLiteral>(10);
    auto ast = std::make_shared<parser::VariableDeclaration>(
        "x",
        parser::Type::INT,
        init
    );

    // 生成 IR
    auto ir = generator_->generateStatement(ast);
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR 是一个存储指令
    auto inst = std::dynamic_pointer_cast<IRInstruction>(ir);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->getOpType(), IROpType::STORE);

    // 验证操作数
    const auto& operands = inst->getOperands();
    ASSERT_EQ(operands.size(), 2);

    auto value = std::dynamic_pointer_cast<IRConstant>(operands[0]);
    auto var = std::dynamic_pointer_cast<IRVariable>(operands[1]);

    ASSERT_NE(value, nullptr);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(std::get<int64_t>(value->getValue()), 10);
    EXPECT_EQ(var->getName(), "x");
}

/**
 * 条件语句生成测试
 */
TEST_F(IRGeneratorTest, IfStatement) {
    // 创建一个条件语句的 AST 节点：if (x > 0) y = 1; else y = 2;
    auto condition = std::make_shared<parser::BinaryExpression>(
        std::make_shared<parser::Identifier>("x"),
        parser::BinaryOperator::GT,
        std::make_shared<parser::NumberLiteral>(0)
    );

    auto thenStmt = std::make_shared<parser::AssignmentStatement>(
        std::make_shared<parser::Identifier>("y"),
        std::make_shared<parser::NumberLiteral>(1)
    );

    auto elseStmt = std::make_shared<parser::AssignmentStatement>(
        std::make_shared<parser::Identifier>("y"),
        std::make_shared<parser::NumberLiteral>(2)
    );

    auto ast = std::make_shared<parser::IfStatement>(condition, thenStmt, elseStmt);

    // 生成 IR
    auto ir = generator_->generateStatement(ast);
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR 结构
    auto func = std::dynamic_pointer_cast<IRFunction>(ir);
    ASSERT_NE(func, nullptr);

    // 验证基本块数量（条件块、then块、else块、合并块）
    const auto& blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 4);

    // 验证条件块的终止指令是条件跳转
    const auto& condBlock = blocks[0];
    ASSERT_FALSE(condBlock->getInstructions().empty());
    auto brInst = condBlock->getInstructions().back();
    EXPECT_EQ(brInst->getOpType(), IROpType::BR);
}

/**
 * 函数声明生成测试
 */
TEST_F(IRGeneratorTest, FunctionDeclaration) {
    // 创建一个函数声明的 AST 节点：int add(int a, int b) { return a + b; }
    std::vector<std::shared_ptr<parser::Parameter>> params = {
        std::make_shared<parser::Parameter>("a", parser::Type::INT),
        std::make_shared<parser::Parameter>("b", parser::Type::INT)
    };

    auto left = std::make_shared<parser::Identifier>("a");
    auto right = std::make_shared<parser::Identifier>("b");
    auto body = std::make_shared<parser::BinaryExpression>(
        parser::BinaryOperator::ADD,
        left,
        right
    );
    auto returnStmt = std::make_shared<parser::ReturnStatement>(body);

    auto ast = std::make_shared<parser::FunctionDeclaration>(
        "add",
        parser::Type::INT,
        params,
        std::vector<std::shared_ptr<parser::Statement>>{returnStmt}
    );

    // 使用专门的函数生成器
    auto ir = funcGenerator_->generate(ast.get());
    ASSERT_NE(ir, nullptr);

    // 验证生成的 IR 是一个函数
    auto func = std::dynamic_pointer_cast<IRFunction>(ir);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "add");

    // 验证参数
    const auto& funcParams = func->getParameters();
    ASSERT_EQ(funcParams.size(), 2);
    EXPECT_EQ(funcParams[0]->getName(), "a");
    EXPECT_EQ(funcParams[1]->getName(), "b");

    // 验证基本块
    const auto& blocks = func->getBasicBlocks();
    ASSERT_GE(blocks.size(), 1);

    // 验证返回指令
    const auto& instructions = blocks[0]->getInstructions();
    ASSERT_GE(instructions.size(), 2);  // 至少有一条加法指令和一条返回指令

    auto lastInst = instructions.back();
    EXPECT_EQ(lastInst->getOpType(), IROpType::RET);

    // 使用通用生成器
    auto ir2 = generator_->generateFunction(ast);
    ASSERT_NE(ir2, nullptr);

    // 验证两种方式生成的 IR 是等价的
    auto func2 = std::dynamic_pointer_cast<IRFunction>(ir2);
    ASSERT_NE(func2, nullptr);
    EXPECT_EQ(func2->getName(), func->getName());
}

} // namespace test
} // namespace ir
} // namespace collie
