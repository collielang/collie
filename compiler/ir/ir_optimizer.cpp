/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 优化器实现
 */

#include "ir_optimizer.h"
#include <stdexcept>

namespace collie {
namespace ir {

// ConstantFoldingOptimizer 实现
bool ConstantFoldingOptimizer::optimize(std::shared_ptr<IRNode> ir) {
    bool modified = false;

    // 如果是指令节点，尝试进行常量折叠
    if (auto inst = std::dynamic_pointer_cast<IRInstruction>(ir)) {
        modified |= foldConstantExpr(inst);
    }

    // 如果是基本块，对每条指令进行常量折叠
    if (auto block = std::dynamic_pointer_cast<IRBasicBlock>(ir)) {
        for (auto& inst : block->getInstructions()) {
            modified |= foldConstantExpr(inst);
        }
    }

    // 如果是函数，对每个基本块进行常量折叠
    if (auto func = std::dynamic_pointer_cast<IRFunction>(ir)) {
        for (auto& block : func->getBasicBlocks()) {
            modified |= optimize(block);
        }
    }

    return modified;
}

bool ConstantFoldingOptimizer::foldConstantExpr(std::shared_ptr<IRInstruction> inst) {
    const auto& operands = inst->getOperands();
    if (operands.size() != 2) return false;

    // 检查两个操作数是否都是常量
    if (!isConstant(operands[0]) || !isConstant(operands[1])) {
        return false;
    }

    auto left = std::dynamic_pointer_cast<IRConstant>(operands[0]);
    auto right = std::dynamic_pointer_cast<IRConstant>(operands[1]);

    // 计算常量表达式的结果
    auto result = evaluateConstantExpr(inst->getOpType(), left, right);
    if (!result) return false;

    // 用计算结果替换原指令的操作数
    inst->getOperands().clear();
    inst->addOperand(result);

    return true;
}

bool ConstantFoldingOptimizer::isConstant(const std::shared_ptr<IROperand>& operand) const {
    return std::dynamic_pointer_cast<IRConstant>(operand) != nullptr;
}

std::shared_ptr<IRConstant> ConstantFoldingOptimizer::evaluateConstantExpr(
    IROpType op,
    const std::shared_ptr<IRConstant>& left,
    const std::shared_ptr<IRConstant>& right
) {
    // 目前只处理整数常量
    if (std::holds_alternative<int64_t>(left->getValue()) &&
        std::holds_alternative<int64_t>(right->getValue())) {
        int64_t leftVal = std::get<int64_t>(left->getValue());
        int64_t rightVal = std::get<int64_t>(right->getValue());
        int64_t result = 0;

        switch (op) {
            case IROpType::ADD:
                result = leftVal + rightVal;
                break;
            case IROpType::SUB:
                result = leftVal - rightVal;
                break;
            case IROpType::MUL:
                result = leftVal * rightVal;
                break;
            case IROpType::DIV:
                if (rightVal == 0) return nullptr;
                result = leftVal / rightVal;
                break;
            case IROpType::MOD:
                if (rightVal == 0) return nullptr;
                result = leftVal % rightVal;
                break;
            default:
                return nullptr;
        }

        return std::make_shared<IRConstant>(result);
    }

    return nullptr;
}

// DeadCodeEliminationOptimizer 实现
bool DeadCodeEliminationOptimizer::optimize(std::shared_ptr<IRNode> ir) {
    bool modified = false;

    // 如果是基本块，对其进行死代码消除
    if (auto block = std::dynamic_pointer_cast<IRBasicBlock>(ir)) {
        modified |= eliminateDeadCode(block);
    }

    // 如果是函数，对每个基本块进行死代码消除
    if (auto func = std::dynamic_pointer_cast<IRFunction>(ir)) {
        for (auto& block : func->getBasicBlocks()) {
            modified |= eliminateDeadCode(block);
        }
    }

    return modified;
}

bool DeadCodeEliminationOptimizer::eliminateDeadCode(std::shared_ptr<IRBasicBlock> block) {
    bool modified = false;
    auto& instructions = block->getInstructions();

    // 第一遍：标记所有有副作用或结果被使用的指令
    std::unordered_set<std::shared_ptr<IRInstruction>> liveInstructions;

    // 从后向前遍历指令，构建活跃指令集
    for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
        auto inst = *it;

        // 如果指令有副作用或其结果被使用，则标记为活跃
        if (!isDeadInstruction(inst)) {
            liveInstructions.insert(inst);

            // 标记所有操作数对应的指令为活跃
            for (const auto& operand : inst->getOperands()) {
                if (auto defInst = operand->getDefiningInstruction()) {
                    liveInstructions.insert(defInst);
                }
            }
        }
    }

    // 第二遍：移除死代码
    auto it = instructions.begin();
    while (it != instructions.end()) {
        if (liveInstructions.find(*it) == liveInstructions.end()) {
            it = instructions.erase(it);
            modified = true;
        } else {
            ++it;
        }
    }

    return modified;
}

bool DeadCodeEliminationOptimizer::isDeadInstruction(
    const std::shared_ptr<IRInstruction>& inst
) const {
    // 以下类型的指令不能被消除：
    // 1. 存储指令（有副作用）
    // 2. 函数调用（可能有副作用）
    // 3. 返回指令
    // 4. 分支指令
    // 5. 跳转指令
    switch (inst->getOpType()) {
        case IROpType::STORE:
        case IROpType::CALL:
        case IROpType::RET:
        case IROpType::BR:
        case IROpType::JMP:
            return false;
        default:
            // 检查指令的结果是否被使用
            return inst->getUsers().empty();
    }
}

// OptimizationManager 实现
void OptimizationManager::addOptimizer(std::shared_ptr<IROptimizer> optimizer) {
    optimizers_.push_back(optimizer);
}

bool OptimizationManager::runOptimizations(std::shared_ptr<IRNode> ir) {
    bool modified = false;
    int iterations = 0;

    // 根据优化级别选择要运行的优化器
    std::vector<std::shared_ptr<IROptimizer>> activeOptimizers;
    for (size_t i = 0; i < optimizers_.size() && i <= static_cast<size_t>(optimizationLevel_); ++i) {
        activeOptimizers.push_back(optimizers_[i]);
    }

    // 迭代运行优化，直到没有更多优化机会或达到最大迭代次数
    do {
        bool iterationModified = false;

        // 运行每个优化器
        for (auto& optimizer : activeOptimizers) {
            iterationModified |= optimizer->optimize(ir);
        }

        modified |= iterationModified;

        if (!iterationModified) break;
    } while (++iterations < maxIterations_);

    return modified;
}

} // namespace ir
} // namespace collie