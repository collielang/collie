/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: 基本块合并优化器测试
 */

#include "ir_optimizer_test_base.h"

namespace collie {
namespace ir {
namespace test {

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

/**
 * 多个基本块链式合并测试
 */
TEST_F(BlockMergingTest, ChainMerge) {
    // 创建一个函数
    auto func = std::make_shared<IRFunction>("test_func");

    // 创建三个基本块
    auto block1 = std::make_shared<IRBasicBlock>();
    auto block2 = std::make_shared<IRBasicBlock>();
    auto block3 = std::make_shared<IRBasicBlock>();

    // 在第一个基本块中添加指令
    auto add1 = std::make_shared<IRInstruction>(IROpType::ADD);
    add1->addOperand(std::make_shared<IRConstant>(10));
    add1->addOperand(std::make_shared<IRConstant>(20));
    block1->addInstruction(add1);

    auto jmp1 = std::make_shared<IRInstruction>(IROpType::JMP);
    jmp1->addOperand(block2);
    block1->addInstruction(jmp1);

    // 在第二个基本块中添加指令
    auto add2 = std::make_shared<IRInstruction>(IROpType::ADD);
    add2->addOperand(add1);
    add2->addOperand(std::make_shared<IRConstant>(30));
    block2->addInstruction(add2);

    auto jmp2 = std::make_shared<IRInstruction>(IROpType::JMP);
    jmp2->addOperand(block3);
    block2->addInstruction(jmp2);

    // 在第三个基本块中添加指令
    auto add3 = std::make_shared<IRInstruction>(IROpType::ADD);
    add3->addOperand(add2);
    add3->addOperand(std::make_shared<IRConstant>(40));
    block3->addInstruction(add3);

    // 设置基本块的前驱后继关系
    block1->addSuccessor(block2);
    block2->addPredecessor(block1);
    block2->addSuccessor(block3);
    block3->addPredecessor(block2);

    // 将基本块添加到函数中
    func->addBasicBlock(block1);
    func->addBasicBlock(block2);
    func->addBasicBlock(block3);

    // 运行优化
    bool modified = manager_->runOptimizations(func);
    EXPECT_TRUE(modified);

    // 验证结果：三个基本块应该被合并为一个
    auto& blocks = func->getBasicBlocks();
    ASSERT_EQ(blocks.size(), 1);

    // 验证合并后的基本块包含正确的指令
    auto& instructions = blocks[0]->getInstructions();
    ASSERT_EQ(instructions.size(), 3);  // 三条加法指令
    EXPECT_EQ(instructions[0]->getOpType(), IROpType::ADD);
    EXPECT_EQ(instructions[1]->getOpType(), IROpType::ADD);
    EXPECT_EQ(instructions[2]->getOpType(), IROpType::ADD);
}

} // namespace test
} // namespace ir
} // namespace collie