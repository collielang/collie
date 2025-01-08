/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 节点测试
 */

#include "ir/ir_node.h"
#include <gtest/gtest.h>
#include <memory>

namespace collie {
namespace ir {
namespace test {

class IRNodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建一个简单的 IR 函数
        func_ = std::make_shared<IRFunction>("test_func");

        // 创建基本块
        block1_ = std::make_shared<IRBasicBlock>();
        block2_ = std::make_shared<IRBasicBlock>();

        // 创建变量和常量
        var1_ = std::make_shared<IRVariable>("x", IRType::INT);
        var2_ = std::make_shared<IRVariable>("y", IRType::INT);
        const1_ = std::make_shared<IRConstant>(10);
        const2_ = std::make_shared<IRConstant>(20);

        // 创建指令
        add_ = std::make_shared<IRInstruction>(IROpType::ADD);
        add_->addOperand(var1_);
        add_->addOperand(const1_);

        mul_ = std::make_shared<IRInstruction>(IROpType::MUL);
        mul_->addOperand(var2_);
        mul_->addOperand(const2_);

        // 添加指令到基本块
        block1_->addInstruction(add_);
        block1_->addInstruction(mul_);

        // 添加基本块到函数
        func_->addBasicBlock(block1_);
        func_->addBasicBlock(block2_);
    }

    std::shared_ptr<IRFunction> func_;
    std::shared_ptr<IRBasicBlock> block1_;
    std::shared_ptr<IRBasicBlock> block2_;
    std::shared_ptr<IRVariable> var1_;
    std::shared_ptr<IRVariable> var2_;
    std::shared_ptr<IRConstant> const1_;
    std::shared_ptr<IRConstant> const2_;
    std::shared_ptr<IRInstruction> add_;
    std::shared_ptr<IRInstruction> mul_;
};

TEST_F(IRNodeTest, ConstantTest) {
    // 测试整数常量
    auto intConst = std::make_shared<IRConstant>(42);
    EXPECT_EQ(intConst->getType(), IRType::INT);
    EXPECT_EQ(intConst->toString(), "42");

    // 测试浮点常量
    auto floatConst = std::make_shared<IRConstant>(3.14);
    EXPECT_EQ(floatConst->getType(), IRType::FLOAT);
    EXPECT_EQ(floatConst->toString(), "3.14");

    // 测试布尔常量
    auto boolConst = std::make_shared<IRConstant>(true);
    EXPECT_EQ(boolConst->getType(), IRType::BOOL);
    EXPECT_EQ(boolConst->toString(), "true");

    // 测试字符串常量
    auto strConst = std::make_shared<IRConstant>(std::string("hello"));
    EXPECT_EQ(strConst->getType(), IRType::STRING);
    EXPECT_EQ(strConst->toString(), "\"hello\"");
}

TEST_F(IRNodeTest, VariableTest) {
    EXPECT_EQ(var1_->getName(), "x");
    EXPECT_EQ(var1_->getType(), IRType::INT);
    EXPECT_EQ(var1_->toString(), "%x");
}

TEST_F(IRNodeTest, InstructionTest) {
    // 测试指令类型
    EXPECT_EQ(add_->getOpType(), IROpType::ADD);
    EXPECT_EQ(mul_->getOpType(), IROpType::MUL);

    // 测试操作数
    EXPECT_EQ(add_->getOperands().size(), 2);
    EXPECT_EQ(add_->getOperands()[0], var1_);
    EXPECT_EQ(add_->getOperands()[1], const1_);

    // 测试指令字符串表示
    std::string addStr = add_->toString();
    EXPECT_TRUE(addStr.find("add") != std::string::npos);
    EXPECT_TRUE(addStr.find("%x") != std::string::npos);
    EXPECT_TRUE(addStr.find("10") != std::string::npos);
}

TEST_F(IRNodeTest, BasicBlockTest) {
    // 测试基本块的指令
    EXPECT_EQ(block1_->getInstructions().size(), 2);
    EXPECT_EQ(block1_->getInstructions()[0], add_);
    EXPECT_EQ(block1_->getInstructions()[1], mul_);

    // 测试基本块的父函数
    EXPECT_EQ(block1_->getParent(), func_);
    EXPECT_EQ(block2_->getParent(), func_);
}

TEST_F(IRNodeTest, FunctionTest) {
    // 测试函数名
    EXPECT_EQ(func_->getName(), "test_func");

    // 测试基本块
    EXPECT_EQ(func_->getBasicBlocks().size(), 2);
    EXPECT_EQ(func_->getBasicBlocks()[0], block1_);
    EXPECT_EQ(func_->getBasicBlocks()[1], block2_);
}

TEST_F(IRNodeTest, CFGTest) {
    // 创建一个简单的控制流图
    auto br = std::make_shared<IRInstruction>(IROpType::BR);
    br->addOperand(std::make_shared<IRConstant>(true));  // 条件
    br->addOperand(block2_);  // true 分支
    br->addOperand(block1_);  // false 分支
    block1_->addInstruction(br);

    // 构建 CFG
    auto cfg = buildCFG(func_);

    // 测试后继
    auto successors = cfg.getSuccessors(block1_);
    EXPECT_EQ(successors.size(), 2);
    EXPECT_TRUE(successors.find(block2_) != successors.end());
    EXPECT_TRUE(successors.find(block1_) != successors.end());

    // 测试前驱
    auto predecessors = cfg.getPredecessors(block2_);
    EXPECT_EQ(predecessors.size(), 1);
    EXPECT_TRUE(predecessors.find(block1_) != predecessors.end());
}

} // namespace test
} // namespace ir
} // namespace collie