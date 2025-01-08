/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 优化器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

class IROptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<OptimizationManager>();
        manager_->addOptimizer(std::make_shared<ConstantFoldingOptimizer>());
    }

    std::unique_ptr<OptimizationManager> manager_;
};

// 测试常量折叠优化 - 加法
TEST_F(IROptimizerTest, ConstantFoldingAdd) {
    // 创建常量操作数
    auto left = std::make_shared<IRConstant>(10);
    auto right = std::make_shared<IRConstant>(20);

    // 创建加法指令
    auto inst = std::make_shared<IRInstruction>(IROpType::ADD);
    inst->addOperand(left);
    inst->addOperand(right);

    // 运行优化
    bool modified = manager_->runOptimizations(inst);
    EXPECT_TRUE(modified);

    // 验证结果
    ASSERT_EQ(inst->getOperands().size(), 1);
    auto result = std::dynamic_pointer_cast<IRConstant>(inst->getOperands()[0]);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(std::get<int64_t>(result->getValue()), 30);
}

// 测试常量折叠优化 - 乘法
TEST_F(IROptimizerTest, ConstantFoldingMul) {
    auto left = std::make_shared<IRConstant>(5);
    auto right = std::make_shared<IRConstant>(6);

    auto inst = std::make_shared<IRInstruction>(IROpType::MUL);
    inst->addOperand(left);
    inst->addOperand(right);

    bool modified = manager_->runOptimizations(inst);
    EXPECT_TRUE(modified);

    ASSERT_EQ(inst->getOperands().size(), 1);
    auto result = std::dynamic_pointer_cast<IRConstant>(inst->getOperands()[0]);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(std::get<int64_t>(result->getValue()), 30);
}

// 测试常量折叠优化 - 除法
TEST_F(IROptimizerTest, ConstantFoldingDiv) {
    auto left = std::make_shared<IRConstant>(100);
    auto right = std::make_shared<IRConstant>(20);

    auto inst = std::make_shared<IRInstruction>(IROpType::DIV);
    inst->addOperand(left);
    inst->addOperand(right);

    bool modified = manager_->runOptimizations(inst);
    EXPECT_TRUE(modified);

    ASSERT_EQ(inst->getOperands().size(), 1);
    auto result = std::dynamic_pointer_cast<IRConstant>(inst->getOperands()[0]);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(std::get<int64_t>(result->getValue()), 5);
}

// 测试常量折叠优化 - 除零错误
TEST_F(IROptimizerTest, ConstantFoldingDivByZero) {
    auto left = std::make_shared<IRConstant>(100);
    auto right = std::make_shared<IRConstant>(0);

    auto inst = std::make_shared<IRInstruction>(IROpType::DIV);
    inst->addOperand(left);
    inst->addOperand(right);

    bool modified = manager_->runOptimizations(inst);
    EXPECT_FALSE(modified);

    ASSERT_EQ(inst->getOperands().size(), 2);
}

// 测试常量折叠优化 - 非常量操作数
TEST_F(IROptimizerTest, ConstantFoldingNonConstant) {
    auto left = std::make_shared<IRConstant>(10);
    auto right = std::make_shared<IRVariable>("x", IRType::INT64);

    auto inst = std::make_shared<IRInstruction>(IROpType::ADD);
    inst->addOperand(left);
    inst->addOperand(right);

    bool modified = manager_->runOptimizations(inst);
    EXPECT_FALSE(modified);

    ASSERT_EQ(inst->getOperands().size(), 2);
}

// 测试优化管理器的优化级别设置
TEST_F(IROptimizerTest, OptimizationLevel) {
    manager_->setOptimizationLevel(OptimizationLevel::O0);

    auto left = std::make_shared<IRConstant>(10);
    auto right = std::make_shared<IRConstant>(20);

    auto inst = std::make_shared<IRInstruction>(IROpType::ADD);
    inst->addOperand(left);
    inst->addOperand(right);

    bool modified = manager_->runOptimizations(inst);
    EXPECT_FALSE(modified);

    ASSERT_EQ(inst->getOperands().size(), 2);
}

} // namespace test
} // namespace ir
} // namespace collie