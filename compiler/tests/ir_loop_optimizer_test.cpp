/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 循环优化器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 循环优化器测试基类
 */
class IRLoopOptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建循环优化器
        invariantMotionOptimizer_ = std::make_unique<LoopInvariantMotionOptimizer>();
    }

    // 创建一个简单的循环函数用于测试
    std::shared_ptr<IRFunction> createTestLoop() {
        // 创建一个包含循环的函数：
        // int sum = 0;
        // int x = a * b;  // 循环不变量
        // for (int i = 0; i < n; i++) {
        //     sum = sum + x;  // x 是循环不变量，可以外提
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
        auto varN = std::make_shared<IRVariable>("n", IRType::INT);
        auto varA = std::make_shared<IRVariable>("a", IRType::INT);
        auto varB = std::make_shared<IRVariable>("b", IRType::INT);
        auto varX = std::make_shared<IRVariable>("x", IRType::INT);

        // 入口块：初始化变量
        auto initSum = std::make_shared<IRInstruction>(IROpType::STORE);
        initSum->addOperand(std::make_shared<IRConstant>(0));
        initSum->addOperand(varSum);
        entryBlock->addInstruction(initSum);

        auto initI = std::make_shared<IRInstruction>(IROpType::STORE);
        initI->addOperand(std::make_shared<IRConstant>(0));
        initI->addOperand(varI);
        entryBlock->addInstruction(initI);

        // 计算 x = a * b（循环不变量）
        auto mulInst = std::make_shared<IRInstruction>(IROpType::MUL);
        mulInst->addOperand(varA);
        mulInst->addOperand(varB);
        auto storeX = std::make_shared<IRInstruction>(IROpType::STORE);
        storeX->addOperand(mulInst);
        storeX->addOperand(varX);
        bodyBlock->addInstruction(storeX);

        // 循环头：检查条件 i < n
        auto loadI = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadI->addOperand(varI);
        headerBlock->addInstruction(loadI);

        auto loadN = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadN->addOperand(varN);
        headerBlock->addInstruction(loadN);

        auto cmp = std::make_shared<IRInstruction>(IROpType::LT);
        cmp->addOperand(loadI);
        cmp->addOperand(loadN);
        headerBlock->addInstruction(cmp);

        auto br = std::make_shared<IRInstruction>(IROpType::BR);
        br->addOperand(cmp);
        br->addOperand(bodyBlock);
        br->addOperand(exitBlock);
        headerBlock->addInstruction(br);

        // 循环体：sum = sum + x
        auto loadSum = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadSum->addOperand(varSum);
        bodyBlock->addInstruction(loadSum);

        auto loadX = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadX->addOperand(varX);
        bodyBlock->addInstruction(loadX);

        auto add = std::make_shared<IRInstruction>(IROpType::ADD);
        add->addOperand(loadSum);
        add->addOperand(loadX);
        bodyBlock->addInstruction(add);

        auto storeSum = std::make_shared<IRInstruction>(IROpType::STORE);
        storeSum->addOperand(add);
        storeSum->addOperand(varSum);
        bodyBlock->addInstruction(storeSum);

        // 更新循环变量 i++
        auto loadI2 = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadI2->addOperand(varI);
        bodyBlock->addInstruction(loadI2);

        auto inc = std::make_shared<IRInstruction>(IROpType::ADD);
        inc->addOperand(loadI2);
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

    std::unique_ptr<LoopInvariantMotionOptimizer> invariantMotionOptimizer_;
};

/**
 * 循环不变量外提优化器测试
 */
TEST_F(IRLoopOptimizerTest, InvariantMotion) {
    // 创建测试用的循环函数
    auto func = createTestLoop();

    // 运行循环不变量外提优化
    auto result = invariantMotionOptimizer_->optimize(func);

    // 验证优化结果
    ASSERT_NE(result, nullptr);
    auto optimizedFunc = std::dynamic_pointer_cast<IRFunction>(result);
    ASSERT_NE(optimizedFunc, nullptr);

    // 验证 x = a * b 被移到了循环外
    const auto& blocks = optimizedFunc->getBasicBlocks();
    bool foundMulOutsideLoop = false;
    for (const auto& block : blocks) {
        if (block != blocks[2]) {  // blocks[2] 是循环体
            for (const auto& inst : block->getInstructions()) {
                if (inst->getOpType() == IROpType::MUL) {
                    foundMulOutsideLoop = true;
                    break;
                }
            }
        }
    }
    EXPECT_TRUE(foundMulOutsideLoop);
}

/**
 * 嵌套循环不变量外提测试
 */
TEST_F(IRLoopOptimizerTest, NestedLoopInvariantMotion) {
    // TODO: 添加嵌套循环的测试用例
}

/**
 * 循环不变量外提的副作用测试
 */
TEST_F(IRLoopOptimizerTest, InvariantMotionSideEffects) {
    // TODO: 添加包含副作用的测试用例
}

} // namespace test
} // namespace ir
} // namespace collie
