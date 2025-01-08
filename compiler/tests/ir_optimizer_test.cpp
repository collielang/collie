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

/**
 * IR 优化器测试基类
 */
class IROptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_unique<OptimizationManager>();
    }

    std::unique_ptr<OptimizationManager> manager_;
};

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
 * 基本块合并优化器测试
 */
class BlockMergingTest : public IROptimizerTest {
protected:
    void SetUp() override {
        IROptimizerTest::SetUp();
        manager_->addOptimizer(std::make_shared<BlockMergingOptimizer>());
    }
};

/**
 * 简单基本块合并测试
 */
TEST_F(BlockMergingTest, SimpleMerge) {
    // 创建一个函数
    auto func = std::make_shared<IRFunction>("test_func");

    // 创建两个基本块
    auto block1 = std::make_shared<IRBasicBlock>();
    auto block2 = std::make_shared<IRBasicBlock>();

    // 在第一个基本块中添加一条加法指令和一条跳转指令
    auto add = std::make_shared<IRInstruction>(IROpType::ADD);
    add->addOperand(std::make_shared<IRConstant>(10));
    add->addOperand(std::make_shared<IRConstant>(20));
    block1->addInstruction(add);

    auto jmp = std::make_shared<IRInstruction>(IROpType::JMP);
    jmp->addOperand(block2);
    block1->addInstruction(jmp);

    // 在第二个基本块中添加一条乘法指令
    auto mul = std::make_shared<IRInstruction>(IROpType::MUL);
    mul->addOperand(add);  // 使用第一个基本块的加法结果
    mul->addOperand(std::make_shared<IRConstant>(2));
    block2->addInstruction(mul);

    // 设置基本块的前驱后继关系
    block1->addSuccessor(block2);
    block2->addPredecessor(block1);

    // 将基本块添加到函数中
    func->addBasicBlock(block1);
    func->addBasicBlock(block2);

    // 运行优化
    bool modified = manager_->runOptimizations(func);
    EXPECT_TRUE(modified);

    // 验证结果：两个基本块应该被合并为一个
    auto& blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 1);

    // 验证合并后的基本块包含正确的指令
    auto& instructions = blocks[0]->getInstructions();
    ASSERT_EQ(instructions.size(), 2);  // 加法指令和乘法指令
    EXPECT_EQ(instructions[0]->getOpType(), IROpType::ADD);
    EXPECT_EQ(instructions[1]->getOpType(), IROpType::MUL);
}

/**
 * 条件分支阻止合并测试
 */
TEST_F(BlockMergingTest, NoMergeWithBranch) {
    // 创建一个函数
    auto func = std::make_shared<IRFunction>("test_func");

    // 创建三个基本块
    auto block1 = std::make_shared<IRBasicBlock>();
    auto block2 = std::make_shared<IRBasicBlock>();
    auto block3 = std::make_shared<IRBasicBlock>();

    // 在第一个基本块中添加一条条件分支指令
    auto cond = std::make_shared<IRInstruction>(IROpType::BR);
    cond->addOperand(std::make_shared<IRConstant>(1));  // 条件
    cond->addOperand(block2);  // true 分支
    cond->addOperand(block3);  // false 分支
    block1->addInstruction(cond);

    // 在第二个基本块中添加一条加法指令
    auto add = std::make_shared<IRInstruction>(IROpType::ADD);
    add->addOperand(std::make_shared<IRConstant>(10));
    add->addOperand(std::make_shared<IRConstant>(20));
    block2->addInstruction(add);

    // 在第三个基本块中添加一条乘法指令
    auto mul = std::make_shared<IRInstruction>(IROpType::MUL);
    mul->addOperand(std::make_shared<IRConstant>(30));
    mul->addOperand(std::make_shared<IRConstant>(40));
    block3->addInstruction(mul);

    // 设置基本块的前驱后继关系
    block1->addSuccessor(block2);
    block1->addSuccessor(block3);
    block2->addPredecessor(block1);
    block3->addPredecessor(block1);

    // 将基本块添加到函数中
    func->addBasicBlock(block1);
    func->addBasicBlock(block2);
    func->addBasicBlock(block3);

    // 运行优化
    bool modified = manager_->runOptimizations(func);
    EXPECT_FALSE(modified);

    // 验证结果：由于有条件分支，基本块不应该被合并
    auto& blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 3);
}

} // namespace test
} // namespace ir
} // namespace collie