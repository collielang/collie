/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 循环强度削弱优化器测试
 */

#include <gtest/gtest.h>
#include "../ir/ir_optimizer.h"
#include "../ir/ir_node.h"

namespace collie {
namespace ir {
namespace test {

/**
 * 循环强度削弱优化器测试
 */
class IRStrengthReductionTest : public ::testing::Test {
protected:
    void SetUp() override {
        optimizer_ = std::make_unique<LoopStrengthReductionOptimizer>();
    }

    // 创建一个包含乘法运算的测试函数
    std::shared_ptr<IRFunction> createTestFunction() {
        // 创建一个包含乘法运算的函数：
        // int result = 0;
        // for (int i = 0; i < n; i++) {
        //     result = result + (i * 5);  // i * 5 可以被优化为累加
        // }
        auto func = std::make_shared<IRFunction>("test_mul");

        // 创建基本块
        auto entryBlock = std::make_shared<IRBasicBlock>();
        auto headerBlock = std::make_shared<IRBasicBlock>();
        auto bodyBlock = std::make_shared<IRBasicBlock>();
        auto exitBlock = std::make_shared<IRBasicBlock>();

        // 创建变量
        auto varResult = std::make_shared<IRVariable>("result", IRType::INT);
        auto varI = std::make_shared<IRVariable>("i", IRType::INT);
        auto varN = std::make_shared<IRConstant>(10);
        auto constFive = std::make_shared<IRConstant>(5);

        // 入口块：初始化变量
        auto initResult = std::make_shared<IRInstruction>(IROpType::STORE);
        initResult->addOperand(std::make_shared<IRConstant>(0));
        initResult->addOperand(varResult);
        entryBlock->addInstruction(initResult);

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

        // 循环体：result = result + (i * 5)
        auto loadResult = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadResult->addOperand(varResult);
        bodyBlock->addInstruction(loadResult);

        auto loadI2 = std::make_shared<IRInstruction>(IROpType::LOAD);
        loadI2->addOperand(varI);
        bodyBlock->addInstruction(loadI2);

        auto mul = std::make_shared<IRInstruction>(IROpType::MUL);
        mul->addOperand(loadI2);
        mul->addOperand(constFive);
        bodyBlock->addInstruction(mul);

        auto add = std::make_shared<IRInstruction>(IROpType::ADD);
        add->addOperand(loadResult);
        add->addOperand(mul);
        bodyBlock->addInstruction(add);

        auto storeResult = std::make_shared<IRInstruction>(IROpType::STORE);
        storeResult->addOperand(add);
        storeResult->addOperand(varResult);
        bodyBlock->addInstruction(storeResult);

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

    std::unique_ptr<LoopStrengthReductionOptimizer> optimizer_;
};

/**
 * 基本的强度削弱测试
 */
TEST_F(IRStrengthReductionTest, BasicStrengthReduction) {
    // 创建测试函数
    auto func = createTestFunction();

    // 运行优化
    bool changed = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_TRUE(changed);

    // 验证乘法运算被替换为加法
    bool foundMultiplication = false;
    bool foundStrengthReduction = false;

    for (const auto& block : func->getBasicBlocks()) {
        for (const auto& inst : block->getInstructions()) {
            // 检查是否还存在原始的乘法运算
            if (inst->getOpType() == IROpType::MUL) {
                auto operands = inst->getOperands();
                if (operands.size() == 2) {
                    if (auto constant = std::dynamic_pointer_cast<IRConstant>(operands[1])) {
                        if (std::get<int64_t>(constant->getValue()) == 5) {
                            foundMultiplication = true;
                        }
                    }
                }
            }

            // 检查是否添加了强度削弱后的加法运算
            if (inst->getOpType() == IROpType::ADD) {
                auto operands = inst->getOperands();
                if (operands.size() == 2) {
                    if (auto var = std::dynamic_pointer_cast<IRVariable>(operands[0])) {
                        if (var->getName().find("str_") == 0) {
                            foundStrengthReduction = true;
                        }
                    }
                }
            }
        }
    }

    // 原始的乘法运算应该被移除
    EXPECT_FALSE(foundMultiplication);
    // 应该找到强度削弱后的加法运算
    EXPECT_TRUE(foundStrengthReduction);
}

/**
 * 多个乘法运算的强度削弱测试
 */
TEST_F(IRStrengthReductionTest, MultipleMultiplications) {
    // 创建一个包含多个乘法运算的函数：
    // int result = 0;
    // for (int i = 0; i < n; i++) {
    //     result = result + (i * 5) + (i * 3);  // 两个乘法都可以被优化为累加
    // }
    auto func = std::make_shared<IRFunction>("test_multiple_mul");

    // 创建基本块
    auto entryBlock = std::make_shared<IRBasicBlock>();
    auto headerBlock = std::make_shared<IRBasicBlock>();
    auto bodyBlock = std::make_shared<IRBasicBlock>();
    auto exitBlock = std::make_shared<IRBasicBlock>();

    // 创建变量
    auto varResult = std::make_shared<IRVariable>("result", IRType::INT);
    auto varI = std::make_shared<IRVariable>("i", IRType::INT);
    auto varN = std::make_shared<IRConstant>(10);
    auto constFive = std::make_shared<IRConstant>(5);
    auto constThree = std::make_shared<IRConstant>(3);

    // 入口块：初始化变量
    auto initResult = std::make_shared<IRInstruction>(IROpType::STORE);
    initResult->addOperand(std::make_shared<IRConstant>(0));
    initResult->addOperand(varResult);
    entryBlock->addInstruction(initResult);

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

    // 循环体：result = result + (i * 5) + (i * 3)
    auto loadResult = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadResult->addOperand(varResult);
    bodyBlock->addInstruction(loadResult);

    auto loadI2 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadI2->addOperand(varI);
    bodyBlock->addInstruction(loadI2);

    auto mul1 = std::make_shared<IRInstruction>(IROpType::MUL);
    mul1->addOperand(loadI2);
    mul1->addOperand(constFive);
    bodyBlock->addInstruction(mul1);

    auto loadI3 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadI3->addOperand(varI);
    bodyBlock->addInstruction(loadI3);

    auto mul2 = std::make_shared<IRInstruction>(IROpType::MUL);
    mul2->addOperand(loadI3);
    mul2->addOperand(constThree);
    bodyBlock->addInstruction(mul2);

    auto add1 = std::make_shared<IRInstruction>(IROpType::ADD);
    add1->addOperand(loadResult);
    add1->addOperand(mul1);
    bodyBlock->addInstruction(add1);

    auto add2 = std::make_shared<IRInstruction>(IROpType::ADD);
    add2->addOperand(add1);
    add2->addOperand(mul2);
    bodyBlock->addInstruction(add2);

    auto storeResult = std::make_shared<IRInstruction>(IROpType::STORE);
    storeResult->addOperand(add2);
    storeResult->addOperand(varResult);
    bodyBlock->addInstruction(storeResult);

    // 更新循环变量 i++
    auto loadI4 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadI4->addOperand(varI);
    bodyBlock->addInstruction(loadI4);

    auto inc = std::make_shared<IRInstruction>(IROpType::ADD);
    inc->addOperand(loadI4);
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

    // 运行优化
    bool changed = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_TRUE(changed);

    // 验证乘法运算被替换为加法
    int foundMultiplications = 0;
    int foundStrengthReductions = 0;

    for (const auto& block : func->getBasicBlocks()) {
        for (const auto& inst : block->getInstructions()) {
            // 检查是否还存在原始的乘法运算
            if (inst->getOpType() == IROpType::MUL) {
                auto operands = inst->getOperands();
                if (operands.size() == 2) {
                    if (auto constant = std::dynamic_pointer_cast<IRConstant>(operands[1])) {
                        auto value = std::get<int64_t>(constant->getValue());
                        if (value == 5 || value == 3) {
                            foundMultiplications++;
                        }
                    }
                }
            }

            // 检查是否添加了强度削弱后的加法运算
            if (inst->getOpType() == IROpType::ADD) {
                auto operands = inst->getOperands();
                if (operands.size() == 2) {
                    if (auto var = std::dynamic_pointer_cast<IRVariable>(operands[0])) {
                        if (var->getName().find("str_") == 0) {
                            foundStrengthReductions++;
                        }
                    }
                }
            }
        }
    }

    // 原始的乘法运算应该被移除
    EXPECT_EQ(foundMultiplications, 0);
    // 应该找到两个强度削弱后的加法运算
    EXPECT_EQ(foundStrengthReductions, 2);
}

/**
 * 嵌套循环中的强度削弱测试
 */
TEST_F(IRStrengthReductionTest, NestedLoopStrengthReduction) {
    // 创建一个包含嵌套循环的函数：
    // int result = 0;
    // for (int i = 0; i < n; i++) {
    //     for (int j = 0; j < m; j++) {
    //         result = result + (i * 5) + (j * 3);
    //     }
    // }
    auto func = std::make_shared<IRFunction>("test_nested_loop");

    // 创建基本块
    auto entryBlock = std::make_shared<IRBasicBlock>();
    auto outerHeaderBlock = std::make_shared<IRBasicBlock>();
    auto innerHeaderBlock = std::make_shared<IRBasicBlock>();
    auto innerBodyBlock = std::make_shared<IRBasicBlock>();
    auto innerExitBlock = std::make_shared<IRBasicBlock>();
    auto outerExitBlock = std::make_shared<IRBasicBlock>();

    // 创建变量
    auto varResult = std::make_shared<IRVariable>("result", IRType::INT);
    auto varI = std::make_shared<IRVariable>("i", IRType::INT);
    auto varJ = std::make_shared<IRVariable>("j", IRType::INT);
    auto varN = std::make_shared<IRConstant>(10);
    auto varM = std::make_shared<IRConstant>(5);
    auto constFive = std::make_shared<IRConstant>(5);
    auto constThree = std::make_shared<IRConstant>(3);

    // 入口块：初始化变量
    auto initResult = std::make_shared<IRInstruction>(IROpType::STORE);
    initResult->addOperand(std::make_shared<IRConstant>(0));
    initResult->addOperand(varResult);
    entryBlock->addInstruction(initResult);

    auto initI = std::make_shared<IRInstruction>(IROpType::STORE);
    initI->addOperand(std::make_shared<IRConstant>(0));
    initI->addOperand(varI);
    entryBlock->addInstruction(initI);

    // 外层循环头：检查条件 i < n
    auto loadI = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadI->addOperand(varI);
    outerHeaderBlock->addInstruction(loadI);

    auto cmpI = std::make_shared<IRInstruction>(IROpType::LT);
    cmpI->addOperand(loadI);
    cmpI->addOperand(varN);
    outerHeaderBlock->addInstruction(cmpI);

    auto brI = std::make_shared<IRInstruction>(IROpType::BR);
    brI->addOperand(cmpI);
    brI->addOperand(innerHeaderBlock);
    brI->addOperand(outerExitBlock);
    outerHeaderBlock->addInstruction(brI);

    // 内层循环头：初始化 j 和检查条件 j < m
    auto initJ = std::make_shared<IRInstruction>(IROpType::STORE);
    initJ->addOperand(std::make_shared<IRConstant>(0));
    initJ->addOperand(varJ);
    innerHeaderBlock->addInstruction(initJ);

    auto loadJ = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadJ->addOperand(varJ);
    innerHeaderBlock->addInstruction(loadJ);

    auto cmpJ = std::make_shared<IRInstruction>(IROpType::LT);
    cmpJ->addOperand(loadJ);
    cmpJ->addOperand(varM);
    innerHeaderBlock->addInstruction(cmpJ);

    auto brJ = std::make_shared<IRInstruction>(IROpType::BR);
    brJ->addOperand(cmpJ);
    brJ->addOperand(innerBodyBlock);
    brJ->addOperand(innerExitBlock);
    innerHeaderBlock->addInstruction(brJ);

    // 内层循环体：result = result + (i * 5) + (j * 3)
    auto loadResult = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadResult->addOperand(varResult);
    innerBodyBlock->addInstruction(loadResult);

    auto loadI2 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadI2->addOperand(varI);
    innerBodyBlock->addInstruction(loadI2);

    auto mul1 = std::make_shared<IRInstruction>(IROpType::MUL);
    mul1->addOperand(loadI2);
    mul1->addOperand(constFive);
    innerBodyBlock->addInstruction(mul1);

    auto loadJ2 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadJ2->addOperand(varJ);
    innerBodyBlock->addInstruction(loadJ2);

    auto mul2 = std::make_shared<IRInstruction>(IROpType::MUL);
    mul2->addOperand(loadJ2);
    mul2->addOperand(constThree);
    innerBodyBlock->addInstruction(mul2);

    auto add1 = std::make_shared<IRInstruction>(IROpType::ADD);
    add1->addOperand(loadResult);
    add1->addOperand(mul1);
    innerBodyBlock->addInstruction(add1);

    auto add2 = std::make_shared<IRInstruction>(IROpType::ADD);
    add2->addOperand(add1);
    add2->addOperand(mul2);
    innerBodyBlock->addInstruction(add2);

    auto storeResult = std::make_shared<IRInstruction>(IROpType::STORE);
    storeResult->addOperand(add2);
    storeResult->addOperand(varResult);
    innerBodyBlock->addInstruction(storeResult);

    // 更新内层循环变量 j++
    auto loadJ3 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadJ3->addOperand(varJ);
    innerBodyBlock->addInstruction(loadJ3);

    auto incJ = std::make_shared<IRInstruction>(IROpType::ADD);
    incJ->addOperand(loadJ3);
    incJ->addOperand(std::make_shared<IRConstant>(1));
    innerBodyBlock->addInstruction(incJ);

    auto storeJ = std::make_shared<IRInstruction>(IROpType::STORE);
    storeJ->addOperand(incJ);
    storeJ->addOperand(varJ);
    innerBodyBlock->addInstruction(storeJ);

    // 跳回内层循环头
    auto jmpJ = std::make_shared<IRInstruction>(IROpType::JMP);
    jmpJ->addOperand(innerHeaderBlock);
    innerBodyBlock->addInstruction(jmpJ);

    // 内层循环出口：更新外层循环变量 i++
    auto loadI3 = std::make_shared<IRInstruction>(IROpType::LOAD);
    loadI3->addOperand(varI);
    innerExitBlock->addInstruction(loadI3);

    auto incI = std::make_shared<IRInstruction>(IROpType::ADD);
    incI->addOperand(loadI3);
    incI->addOperand(std::make_shared<IRConstant>(1));
    innerExitBlock->addInstruction(incI);

    auto storeI = std::make_shared<IRInstruction>(IROpType::STORE);
    storeI->addOperand(incI);
    storeI->addOperand(varI);
    innerExitBlock->addInstruction(storeI);

    // 跳回外层循环头
    auto jmpI = std::make_shared<IRInstruction>(IROpType::JMP);
    jmpI->addOperand(outerHeaderBlock);
    innerExitBlock->addInstruction(jmpI);

    // 添加基本块到函数
    func->addBasicBlock(entryBlock);
    func->addBasicBlock(outerHeaderBlock);
    func->addBasicBlock(innerHeaderBlock);
    func->addBasicBlock(innerBodyBlock);
    func->addBasicBlock(innerExitBlock);
    func->addBasicBlock(outerExitBlock);

    // 运行优化
    bool changed = optimizer_->optimize(func);

    // 验证优化结果
    ASSERT_TRUE(changed);

    // 验证乘法运算被替换为加法
    int foundMultiplications = 0;
    int foundStrengthReductions = 0;

    for (const auto& block : func->getBasicBlocks()) {
        for (const auto& inst : block->getInstructions()) {
            // 检查是否还存在原始的乘法运算
            if (inst->getOpType() == IROpType::MUL) {
                auto operands = inst->getOperands();
                if (operands.size() == 2) {
                    if (auto constant = std::dynamic_pointer_cast<IRConstant>(operands[1])) {
                        auto value = std::get<int64_t>(constant->getValue());
                        if (value == 5 || value == 3) {
                            foundMultiplications++;
                        }
                    }
                }
            }

            // 检查是否添加了强度削弱后的加法运算
            if (inst->getOpType() == IROpType::ADD) {
                auto operands = inst->getOperands();
                if (operands.size() == 2) {
                    if (auto var = std::dynamic_pointer_cast<IRVariable>(operands[0])) {
                        if (var->getName().find("str_") == 0) {
                            foundStrengthReductions++;
                        }
                    }
                }
            }
        }
    }

    // 原始的乘法运算应该被移除
    EXPECT_EQ(foundMultiplications, 0);
    // 应该找到两个强度削弱后的加法运算（一个用于 i*5，一个用于 j*3）
    EXPECT_EQ(foundStrengthReductions, 2);
}

} // namespace test
} // namespace ir
} // namespace collie