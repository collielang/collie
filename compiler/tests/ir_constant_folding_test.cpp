/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: 常量折叠优化器测试
 */

#include "ir_optimizer_test_base.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 常量折叠优化器测试
 */
class ConstantFoldingTest : public IROptimizerTest {
protected:
    void SetUp() override {
        IROptimizerTest::SetUp();
        manager_->addOptimizer(std::make_shared<ConstantFoldingOptimizer>());
    }
};

/**
 * 常量加法折叠测试
 */
TEST_F(ConstantFoldingTest, FoldAdd) {
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

/**
 * 常量乘法折叠测试
 */
TEST_F(ConstantFoldingTest, FoldMul) {
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

/**
 * 常量除法折叠测试
 */
TEST_F(ConstantFoldingTest, FoldDiv) {
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

/**
 * 除零错误测试
 */
TEST_F(ConstantFoldingTest, DivByZero) {
    auto left = std::make_shared<IRConstant>(100);
    auto right = std::make_shared<IRConstant>(0);

    auto inst = std::make_shared<IRInstruction>(IROpType::DIV);
    inst->addOperand(left);
    inst->addOperand(right);

    bool modified = manager_->runOptimizations(inst);
    EXPECT_FALSE(modified);

    ASSERT_EQ(inst->getOperands().size(), 2);
}

/**
 * 非常量操作数测试
 */
TEST_F(ConstantFoldingTest, NonConstant) {
    auto left = std::make_shared<IRConstant>(10);
    auto right = std::make_shared<IRVariable>("x", IRType::INT64);

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