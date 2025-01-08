/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: 死代码消除优化器测试
 */

#include "ir_optimizer_test_base.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 死代码消除优化器测试
 */
class DeadCodeEliminationTest : public IROptimizerTest {
protected:
    void SetUp() override {
        IROptimizerTest::SetUp();
        manager_->addOptimizer(std::make_shared<DeadCodeEliminationOptimizer>());
    }
};

/**
 * 未使用的计算指令消除测试
 */
TEST_F(DeadCodeEliminationTest, UnusedComputation) {
    // 创建基本块
    auto block = std::make_shared<IRBasicBlock>();

    // 创建一个未使用的加法指令
    auto inst1 = std::make_shared<IRInstruction>(IROpType::ADD);
    inst1->addOperand(std::make_shared<IRConstant>(10));
    inst1->addOperand(std::make_shared<IRConstant>(20));
    block->addInstruction(inst1);

    // 创建一个有副作用的存储指令
    auto inst2 = std::make_shared<IRInstruction>(IROpType::STORE);
    inst2->addOperand(std::make_shared<IRConstant>(30));
    block->addInstruction(inst2);

    // 运行优化
    bool modified = manager_->runOptimizations(block);
    EXPECT_TRUE(modified);

    // 验证结果：未使用的加法指令应被删除，存储指令应保留
    auto& instructions = block->getInstructions();
    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0]->getOpType(), IROpType::STORE);
}

/**
 * 有依赖关系的指令保留测试
 */
TEST_F(DeadCodeEliminationTest, UsedComputation) {
    auto block = std::make_shared<IRBasicBlock>();

    // 创建一个加法指令
    auto inst1 = std::make_shared<IRInstruction>(IROpType::ADD);
    inst1->addOperand(std::make_shared<IRConstant>(10));
    inst1->addOperand(std::make_shared<IRConstant>(20));
    block->addInstruction(inst1);

    // 创建一个使用加法结果的存储指令
    auto inst2 = std::make_shared<IRInstruction>(IROpType::STORE);
    inst2->addOperand(inst1);  // 使用加法指令的结果
    block->addInstruction(inst2);

    // 运行优化
    bool modified = manager_->runOptimizations(block);
    EXPECT_FALSE(modified);

    // 验证结果：两条指令都应保留
    auto& instructions = block->getInstructions();
    ASSERT_EQ(instructions.size(), 2);
    EXPECT_EQ(instructions[0]->getOpType(), IROpType::ADD);
    EXPECT_EQ(instructions[1]->getOpType(), IROpType::STORE);
}

/**
 * 函数调用指令保留测试
 */
TEST_F(DeadCodeEliminationTest, FunctionCall) {
    auto block = std::make_shared<IRBasicBlock>();

    // 创建一个函数调用指令
    auto call = std::make_shared<IRInstruction>(IROpType::CALL);
    call->addOperand(std::make_shared<IRFunction>("test_func"));
    block->addInstruction(call);

    // 运行优化
    bool modified = manager_->runOptimizations(block);
    EXPECT_FALSE(modified);

    // 验证结果：函数调用指令应保留
    auto& instructions = block->getInstructions();
    ASSERT_EQ(instructions.size(), 1);
    EXPECT_EQ(instructions[0]->getOpType(), IROpType::CALL);
}

/**
 * 跳转指令保留测试
 */
TEST_F(DeadCodeEliminationTest, BranchAndJump) {
    auto block = std::make_shared<IRBasicBlock>();

    // 创建一个条件分支指令
    auto br = std::make_shared<IRInstruction>(IROpType::BR);
    br->addOperand(std::make_shared<IRConstant>(1));
    br->addOperand(std::make_shared<IRBasicBlock>());
    br->addOperand(std::make_shared<IRBasicBlock>());
    block->addInstruction(br);

    // 创建一个无条件跳转指令
    auto jmp = std::make_shared<IRInstruction>(IROpType::JMP);
    jmp->addOperand(std::make_shared<IRBasicBlock>());
    block->addInstruction(jmp);

    // 运行优化
    bool modified = manager_->runOptimizations(block);
    EXPECT_FALSE(modified);

    // 验证结果：跳转指令应保留
    auto& instructions = block->getInstructions();
    ASSERT_EQ(instructions.size(), 2);
    EXPECT_EQ(instructions[0]->getOpType(), IROpType::BR);
    EXPECT_EQ(instructions[1]->getOpType(), IROpType::JMP);
}

} // namespace test
} // namespace ir
} // namespace collie