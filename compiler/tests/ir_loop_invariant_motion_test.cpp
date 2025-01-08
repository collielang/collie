/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 循环不变量外提优化器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 循环不变量外提优化器测试
 */
class IRLoopInvariantMotionTest : public ::testing::Test {
protected:
    void SetUp() override {
        optimizer_ = std::make_unique<LoopInvariantMotionOptimizer>();
    }

    // 创建一个包含循环不变量的测试函数
    std::shared_ptr<IRFunction> createTestFunction() {
        // 创建一个包含循环不变量的函数：
        // int result = 0;
        // int x = a + b;  // 循环不变量
        // for (int i = 0; i < n; i++) {
        //     result = result + x;  // x 是循环不变量
        // }
        auto func = std::make_shared<IRFunction>("test_loop");

        // 创建基本块
        auto entryBlock = std::make_shared<IRBasicBlock>();
        auto headerBlock = std::make_shared<IRBasicBlock>();
        auto bodyBlock = std::make_shared<IRBasicBlock>();
        auto exitBlock = std::make_shared<IRBasicBlock>();

        // 创建变量
        auto varResult = std::make_shared<IRVariable>("result", IRType::INT);
        auto varX = std::make_shared<IRVariable>("x", IRType::INT);
        auto varI = std::make_shared<IRVariable>("i", IRType::INT);
        auto varA = std::make_shared<IRVariable>("a", IRType::INT);
        auto varB = std::make_shared<IRVariable>("b", IRType::INT);
        auto varN = std::make_shared<IRConstant>(10);

        // 入口块：初始化变量
        auto initResult = std::make_shared<IRInstruction>(IROpType::STORE);
        initResult->addOperand(std::make_shared<IRConstant>(0));
        initResult->addOperand(varResult);
        entryBlock->addInstruction(initResult);

        // 计算循环不变量 x = a + b
        auto loadA = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadA->addOperand(varA);
        entryBlock->addInstruction(loadA);

        auto loadB = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadB->addOperand(varB);
        entryBlock->addInstruction(loadB);

        auto addAB = std::make_shared<IRInstruction>(IROpType::ADD);
        addAB->addOperand(loadA);
        addAB->addOperand(loadB);
        entryBlock->addInstruction(addAB);

        auto storeX = std::make_shared<IRInstruction>(IROpType::STORE);
        storeX->addOperand(addAB);
        storeX->addOperand(varX);
        entryBlock->addInstruction(storeX);

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

        // 循环体：result = result + x
        auto loadResult = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadResult->addOperand(varResult);
        bodyBlock->addInstruction(loadResult);

        auto loadX = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadX->addOperand(varX);
        bodyBlock->addInstruction(loadX);

        auto add = std::make_shared<IRInstruction>(IROpType::ADD);
        add->addOperand(loadResult);
        add->addOperand(loadX);
        bodyBlock->addInstruction(add);

        auto storeResult = std::make_shared<IRInstruction>(IROpType::STORE);
        storeResult->addOperand(add);
        storeResult->addOperand(varResult);
        bodyBlock->addInstruction(storeResult);

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

    std::unique_ptr<LoopInvariantMotionOptimizer> optimizer_;
};

/**
 * 基本的循环不变量外提测试
 */
TEST_F(IRLoopInvariantMotionTest, BasicInvariantMotion) {
    // 创建测试函数
    auto func = createTestFunction();

    // 运行优化
    auto result = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_NE(result, nullptr);
    auto optimizedFunc = std::dynamic_pointer_cast<IRFunction>(result);
    ASSERT_NE(optimizedFunc, nullptr);

    // 验证基本块数量增加了（因为添加了前导块）
    const auto& blocks = optimizedFunc->getBasicBlocks();
    EXPECT_GT(blocks.size(), 4);  // 原来有4个基本块

    // 验证循环不变量被移到了循环外
    bool foundInvariantOutsideLoop = false;
    for (const auto& block : blocks) {
        // 跳过循环体和循环头
        if (block == blocks[2] || block == blocks[1]) continue;

        // 在循环外的基本块中查找加载 x 的指令
        for (const auto& inst : block->getInstructions()) {
            if (inst->getOpType() == IROpType::LOAD) {
                auto operand = inst->getOperand(0);
                if (auto var = std::dynamic_pointer_cast<IRVariable>(operand)) {
                    if (var->getName() == "x") {
                        foundInvariantOutsideLoop = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundInvariantOutsideLoop);
}

/**
 * 多个循环不变量测试
 */
TEST_F(IRLoopInvariantMotionTest, MultipleInvariants) {
    // TODO: 添加包含多个循环不变量的测试用例
}

/**
 * 嵌套循环不变量测试
 */
TEST_F(IRLoopInvariantMotionTest, NestedLoopInvariants) {
    // TODO: 添加嵌套循环中的不变量测试用例
}

} // namespace test
} // namespace ir
} // namespace collie