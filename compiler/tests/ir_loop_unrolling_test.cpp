/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 循环展开优化器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 循环展开优化器测试
 */
class IRLoopUnrollingTest : public ::testing::Test {
protected:
    void SetUp() override {
        optimizer_ = std::make_unique<LoopUnrollingOptimizer>();
    }

    // 创建一个简单的循环函数用于测试
    std::shared_ptr<IRFunction> createTestLoop(int64_t tripCount) {
        // 创建一个包含循环的函数：
        // int sum = 0;
        // for (int i = 0; i < n; i++) {
        //     sum = sum + i;
        // }
        auto func = std::make_shared<IRFunction>("test_loop");

        // 创建基本块
        auto entryBlock = std::make_shared<IRBasicBlock>();
        auto headerBlock = std::make_shared<IRBasicBlock>();
        auto bodyBlock = std::make_shared<IRBasicBlock>();
        auto exitBlock = std::make_shared<IRBasicBlock>();

        // 创建变量
        auto varSum = std::make_shared<IRVariable>("sum", IRType::INT);
        auto varI = std::make_shared<IRVariable>("i", IRType::INT);
        auto varN = std::make_shared<IRConstant>(tripCount);

        // 入口块：初始化变量
        auto initSum = std::make_shared<IRInstruction>(IROpType::STORE);
        initSum->addOperand(std::make_shared<IRConstant>(0));
        initSum->addOperand(varSum);
        entryBlock->addInstruction(initSum);

        auto initI = std::make_shared<IRInstruction>(IROpType::STORE);
        initI->addOperand(std::make_shared<IRConstant>(0));
        initI->addOperand(varI);
        entryBlock->addInstruction(initI);

        // 循环头：检查条件 i < n
        auto loadI = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadI->addOperand(varI);
        headerBlock->addInstruction(loadI);

        auto cmp = std::make_shared<IRInstruction>(IROpType::LT);
        cmp->addOperand(loadI);
        cmp->addOperand(varN);
        headerBlock->addInstruction(cmp);

        auto br = std::make_shared<IRInstruction>(IROpType::BR);
        br->addOperand(cmp);
        br->addOperand(bodyBlock);
        br->addOperand(exitBlock);
        headerBlock->addInstruction(br);

        // 循环体：sum = sum + i
        auto loadSum = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadSum->addOperand(varSum);
        bodyBlock->addInstruction(loadSum);

        auto loadI2 = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadI2->addOperand(varI);
        bodyBlock->addInstruction(loadI2);

        auto add = std::make_shared<IRInstruction>(IROpType::ADD);
        add->addOperand(loadSum);
        add->addOperand(loadI2);
        bodyBlock->addInstruction(add);

        auto storeSum = std::make_shared<IRInstruction>(IROpType::STORE);
        storeSum->addOperand(add);
        storeSum->addOperand(varSum);
        bodyBlock->addInstruction(storeSum);

        // 更新循环变量 i++
        auto loadI3 = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadI3->addOperand(varI);
        bodyBlock->addInstruction(loadI3);

        auto inc = std::make_shared<IRInstruction>(IROpType::ADD);
        inc->addOperand(loadI3);
        inc->addOperand(std::make_shared<IRConstant>(1));
        bodyBlock->addInstruction(inc);

        auto storeI = std::make_shared<IRInstruction>(IROpType::STORE);
        storeI->addOperand(inc);
        storeI->addOperand(varI);
        bodyBlock->addInstruction(storeI);

        // 跳回循环头
        auto jmp = std::make_shared<IRInstruction>(IROpType::JMP);
        jmp->addOperand(headerBlock);
        bodyBlock->addInstruction(jmp);

        // 添加基本块到函数
        func->addBasicBlock(entryBlock);
        func->addBasicBlock(headerBlock);
        func->addBasicBlock(bodyBlock);
        func->addBasicBlock(exitBlock);

        return func;
    }

    std::unique_ptr<LoopUnrollingOptimizer> optimizer_;
};

/**
 * 基本的循环展开测试
 */
TEST_F(IRLoopUnrollingTest, BasicUnrolling) {
    // 创建一个迭代 10 次的循环
    auto func = createTestLoop(10);

    // 运行循环展开优化
    auto result = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_NE(result, nullptr);
    auto optimizedFunc = std::dynamic_pointer_cast<IRFunction>(result);
    ASSERT_NE(optimizedFunc, nullptr);

    // 验证基本块数量增加了（因为循环体被复制了）
    const auto& blocks = optimizedFunc->getBasicBlocks();
    EXPECT_GT(blocks.size(), 4);  // 原来有4个基本块

    // 验证循环变量的递增值变大了
    bool foundLargerIncrement = false;
    for (const auto& block : blocks) {
        for (const auto& inst : block->getInstructions()) {
            if (inst->getOpType() == IROpType::ADD) {
                auto operand = inst->getOperand(1);
                if (auto constant = std::dynamic_pointer_cast<IRConstant>(operand)) {
                    if (std::get<int64_t>(constant->getValue()) > 1) {
                        foundLargerIncrement = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundLargerIncrement);
}

/**
 * 小循环展开测试（迭代次数小于展开因子）
 */
TEST_F(IRLoopUnrollingTest, SmallLoopUnrolling) {
    // 创建一个迭代 2 次的循环
    auto func = createTestLoop(2);

    // 运行循环展开优化
    auto result = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_NE(result, nullptr);
    auto optimizedFunc = std::dynamic_pointer_cast<IRFunction>(result);
    ASSERT_NE(optimizedFunc, nullptr);

    // 验证基本块数量没有变化（循环太小，不应该展开）
    const auto& blocks = optimizedFunc->getBasicBlocks();
    EXPECT_EQ(blocks.size(), 4);
}

/**
 * 大循环展开测试（循环体太大）
 */
TEST_F(IRLoopUnrollingTest, LargeLoopUnrolling) {
    // TODO: 添加循环体很大的测试用例
}

} // namespace test
} // namespace ir
} // namespace collie