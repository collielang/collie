/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 公共子表达式消除优化器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 公共子表达式消除优化器测试
 */
class IRCSEOptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        optimizer_ = std::make_unique<CommonSubexpressionEliminationOptimizer>();
    }

    std::unique_ptr<CommonSubexpressionEliminationOptimizer> optimizer_;
};

/**
 * 基本的公共子表达式消除测试
 */
TEST_F(IRCSEOptimizerTest, BasicCSE) {
    // 创建一个基本块，包含重复的表达式：
    // t1 = a + b
    // t2 = c * d
    // t3 = a + b  // 这是重复的表达式
    // t4 = t2 + t3
    auto block = std::make_shared<IRBasicBlock>();

    auto varA = std::make_shared<IRVariable>("a", IRType::INT);
    auto varB = std::make_shared<IRVariable>("b", IRType::INT);
    auto varC = std::make_shared<IRVariable>("c", IRType::INT);
    auto varD = std::make_shared<IRVariable>("d", IRType::INT);

    // t1 = a + b
    auto add1 = std::make_shared<IRInstruction>(IROpType::ADD);
    add1->addOperand(varA);
    add1->addOperand(varB);
    block->addInstruction(add1);

    // t2 = c * d
    auto mul = std::make_shared<IRInstruction>(IROpType::MUL);
    mul->addOperand(varC);
    mul->addOperand(varD);
    block->addInstruction(mul);

    // t3 = a + b (重复的表达式)
    auto add2 = std::make_shared<IRInstruction>(IROpType::ADD);
    add2->addOperand(varA);
    add2->addOperand(varB);
    block->addInstruction(add2);

    // t4 = t2 + t3
    auto add3 = std::make_shared<IRInstruction>(IROpType::ADD);
    add3->addOperand(mul);
    add3->addOperand(add2);
    block->addInstruction(add3);

    // 创建函数并添加基本块
    auto func = std::make_shared<IRFunction>("test");
    func->addBasicBlock(block);

    // 运行 CSE 优化
    auto result = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_NE(result, nullptr);
    auto optimizedFunc = std::dynamic_pointer_cast<IRFunction>(result);
    ASSERT_NE(optimizedFunc, nullptr);

    // 获取优化后的指令
    const auto& optimizedBlock = optimizedFunc->getBasicBlocks()[0];
    const auto& instructions = optimizedBlock->getInstructions();

    // 验证指令数量减少了（删除了重复的 a + b）
    EXPECT_EQ(instructions.size(), 3);

    // 验证最后一条指令使用了第一个 a + b 的结果
    auto lastInst = instructions.back();
    ASSERT_EQ(lastInst->getOpType(), IROpType::ADD);
    const auto& operands = lastInst->getOperands();
    EXPECT_EQ(operands[0], mul);
    EXPECT_EQ(operands[1], add1);  // 应该使用第一个 a + b 的结果
}

/**
 * 跨基本块的公共子表达式消除测试
 */
TEST_F(IRCSEOptimizerTest, CrossBlockCSE) {
    // TODO: 添加跨基本块的测试用例
}

/**
 * 带有副作用的公共子表达式消除测试
 */
TEST_F(IRCSEOptimizerTest, CSEWithSideEffects) {
    // TODO: 添加带有副作用的测试用例
}

} // namespace test
} // namespace ir
} // namespace collie
