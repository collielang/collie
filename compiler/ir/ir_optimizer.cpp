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

// BlockMergingOptimizer 实现
bool BlockMergingOptimizer::optimize(std::shared_ptr<IRNode> ir) {
    bool modified = false;

    // 如果是函数，对其基本块进行合并优化
    if (auto func = std::dynamic_pointer_cast<IRFunction>(ir)) {
        modified |= mergeBlocks(func);
    }

    return modified;
}

bool BlockMergingOptimizer::mergeBlocks(std::shared_ptr<IRFunction> func) {
    bool modified = false;
    auto& blocks = func->getBasicBlocks();

    // 从前向后遍历基本块
    for (auto it = blocks.begin(); it != blocks.end(); ) {
        auto current = *it;
        auto next = std::next(it);

        // 如果已经到达最后一个基本块，退出循环
        if (next == blocks.end()) {
            break;
        }

        // 检查当前基本块和下一个基本块是否可以合并
        if (canMergeBlocks(current, *next)) {
            // 将下一个基本块的所有指令移动到当前基本块
            auto& currentInsts = current->getInstructions();
            auto& nextInsts = (*next)->getInstructions();

            // 移除当前基本块末尾的跳转指令（如果有）
            if (!currentInsts.empty() &&
                currentInsts.back()->getOpType() == IROpType::JMP) {
                currentInsts.pop_back();
            }

            // 将下一个基本块的指令追加到当前基本块
            currentInsts.insert(
                currentInsts.end(),
                nextInsts.begin(),
                nextInsts.end()
            );

            // 更新所有使用下一个基本块的指令，将其替换为当前基本块
            for (auto user : (*next)->getUsers()) {
                user->replaceOperand(*next, current);
            }

            // 删除已合并的基本块
            it = blocks.erase(next);
            modified = true;
        } else {
            ++it;
        }
    }

    return modified;
}

bool BlockMergingOptimizer::canMergeBlocks(
    const std::shared_ptr<IRBasicBlock>& first,
    const std::shared_ptr<IRBasicBlock>& second
) const {
    // 检查两个基本块是否可以合并的条件：

    // 1. 第一个基本块只能有一个后继（即只能跳转到第二个基本块）
    if (first->getSuccessors().size() != 1) {
        return false;
    }

    // 2. 第二个基本块只能有一个前驱（即只能从第一个基本块跳转过来）
    if (second->getPredecessors().size() != 1) {
        return false;
    }

    // 3. 第一个基本块的最后一条指令如果是跳转指令，必须跳转到第二个基本块
    const auto& firstInsts = first->getInstructions();
    if (!firstInsts.empty()) {
        auto lastInst = firstInsts.back();
        if (lastInst->getOpType() == IROpType::JMP) {
            auto target = std::dynamic_pointer_cast<IRBasicBlock>(
                lastInst->getOperands()[0]
            );
            if (target != second) {
                return false;
            }
        }
    }

    // 4. 第一个基本块不能以条件跳转指令结束
    if (!firstInsts.empty() &&
        firstInsts.back()->getOpType() == IROpType::BR) {
        return false;
    }

    return true;
}

/**
 * 公共子表达式消除优化器
 */
class CommonSubexpressionEliminationOptimizer : public IROptimizer {
public:
    std::shared_ptr<IRNode> optimize(std::shared_ptr<IRNode> node) override {
        if (auto func = std::dynamic_pointer_cast<IRFunction>(node)) {
            return optimizeFunction(func);
        }
        return node;
    }

private:
    std::shared_ptr<IRNode> optimizeFunction(std::shared_ptr<IRFunction> func) {
        bool changed = false;
        for (auto& block : func->getBasicBlocks()) {
            changed |= optimizeBasicBlock(block);
        }
        return changed ? func : nullptr;
    }

    bool optimizeBasicBlock(std::shared_ptr<IRBasicBlock> block) {
        bool changed = false;
        std::unordered_map<std::string, std::shared_ptr<IRInstruction>> expressions;

        auto& instructions = block->getInstructions();
        std::vector<std::shared_ptr<IRInstruction>> newInstructions;

        for (auto& inst : instructions) {
            // 只处理二元运算指令
            if (!isBinaryOperation(inst->getOpType())) {
                newInstructions.push_back(inst);
                continue;
            }

            // 生成表达式的哈希键
            std::string key = getExpressionKey(inst);

            // 检查是否存在相同的表达式
            auto it = expressions.find(key);
            if (it != expressions.end()) {
                // 找到相同的表达式，用之前的结果替换当前指令
                replaceInstruction(inst, it->second);
                changed = true;
                continue;
            }

            // 将新表达式添加到映射中
            expressions[key] = inst;
            newInstructions.push_back(inst);
        }

        if (changed) {
            block->setInstructions(newInstructions);
        }
        return changed;
    }

    bool isBinaryOperation(IROpType opType) {
        return opType == IROpType::ADD || opType == IROpType::SUB ||
               opType == IROpType::MUL || opType == IROpType::DIV;
    }

    std::string getExpressionKey(std::shared_ptr<IRInstruction> inst) {
        std::stringstream ss;
        ss << static_cast<int>(inst->getOpType());

        for (const auto& operand : inst->getOperands()) {
            if (auto constant = std::dynamic_pointer_cast<IRConstant>(operand)) {
                ss << "C" << std::get<int64_t>(constant->getValue());
            } else if (auto var = std::dynamic_pointer_cast<IRVariable>(operand)) {
                ss << "V" << var->getName();
            }
        }

        return ss.str();
    }

    void replaceInstruction(std::shared_ptr<IRInstruction> oldInst,
                          std::shared_ptr<IRInstruction> newInst) {
        // 将所有使用 oldInst 结果的指令更新为使用 newInst 的结果
        for (auto user : oldInst->getUsers()) {
            user->replaceOperand(oldInst, newInst);
        }
    }
};

/**
 * 循环优化器 - 循环不变量外提
 */
class LoopInvariantMotionOptimizer : public IROptimizer {
public:
    std::shared_ptr<IRNode> optimize(std::shared_ptr<IRNode> node) override {
        if (auto func = std::dynamic_pointer_cast<IRFunction>(node)) {
            return optimizeFunction(func);
        }
        return node;
    }

private:
    std::shared_ptr<IRNode> optimizeFunction(std::shared_ptr<IRFunction> func) {
        bool changed = false;

        // 识别循环结构
        auto loops = identifyLoops(func);

        // 对每个循环进行优化
        for (auto& loop : loops) {
            changed |= optimizeLoop(loop);
        }

        return changed ? func : nullptr;
    }

    /**
     * 识别函数中的循环结构
     */
    std::vector<Loop> identifyLoops(std::shared_ptr<IRFunction> func) {
        std::vector<Loop> loops;

        // 构建控制流图
        auto cfg = buildCFG(func);

        // 查找循环头（具有回边的基本块）
        for (auto& block : func->getBasicBlocks()) {
            if (isLoopHeader(block, cfg)) {
                auto loop = analyzeLoop(block, cfg);
                loops.push_back(loop);
            }
        }

        return loops;
    }

    /**
     * 判断基本块是否是循环头
     */
    bool isLoopHeader(std::shared_ptr<IRBasicBlock> block,
                     const ControlFlowGraph& cfg) {
        // 循环头有一条指向自己的回边
        for (auto pred : cfg.getPredecessors(block)) {
            if (dominates(block, pred, cfg)) {
                return true;
            }
        }
        return false;
    }

    /**
     * 分析循环结构
     */
    Loop analyzeLoop(std::shared_ptr<IRBasicBlock> header,
                    const ControlFlowGraph& cfg) {
        Loop loop;
        loop.header = header;

        // 收集循环体中的基本块
        std::stack<std::shared_ptr<IRBasicBlock>> workList;
        workList.push(header);

        while (!workList.empty()) {
            auto block = workList.top();
            workList.pop();

            if (loop.blocks.count(block) > 0) {
                continue;
            }

            loop.blocks.insert(block);

            // 将后继基本块加入工作列表
            for (auto succ : cfg.getSuccessors(block)) {
                if (dominates(header, succ, cfg)) {
                    workList.push(succ);
                }
            }
        }

        return loop;
    }

    /**
     * 优化单个循环
     */
    bool optimizeLoop(const Loop& loop) {
        bool changed = false;

        // 收集循环不变量
        std::vector<std::shared_ptr<IRInstruction>> invariants;
        for (auto block : loop.blocks) {
            for (auto& inst : block->getInstructions()) {
                if (isLoopInvariant(inst, loop)) {
                    invariants.push_back(inst);
                }
            }
        }

        // 将循环不变量移到循环前
        if (!invariants.empty()) {
            auto preheader = createLoopPreheader(loop);
            for (auto& inst : invariants) {
                moveInstructionToBlock(inst, preheader);
            }
            changed = true;
        }

        return changed;
    }

    /**
     * 判断指令是否是循环不变量
     */
    bool isLoopInvariant(std::shared_ptr<IRInstruction> inst,
                        const Loop& loop) {
        // 检查指令的所有操作数是否都是循环不变量
        for (auto& operand : inst->getOperands()) {
            if (auto defInst = operand->getDefiningInstruction()) {
                // 如果操作数是在循环内定义的，且不是循环不变量，则当前指令不是循环不变量
                if (loop.blocks.count(defInst->getParent()) > 0 &&
                    !isLoopInvariant(defInst, loop)) {
                    return false;
                }
            }
        }

        // 检查指令是否有副作用
        return !hasSideEffects(inst);
    }

    /**
     * 创建循环前导基本块
     */
    std::shared_ptr<IRBasicBlock> createLoopPreheader(const Loop& loop) {
        auto preheader = std::make_shared<IRBasicBlock>();

        // 将所有指向循环头的非回边重定向到前导块
        auto header = loop.header;
        std::vector<std::shared_ptr<IRBasicBlock>> predecessors;
        for (auto pred : header->getPredecessors()) {
            if (!loop.blocks.count(pred)) {
                predecessors.push_back(pred);
            }
        }

        for (auto pred : predecessors) {
            redirectTerminator(pred, header, preheader);
        }

        // 添加跳转到循环头的指令
        auto jmp = std::make_shared<IRInstruction>(IROpType::JMP);
        jmp->addOperand(header);
        preheader->addInstruction(jmp);

        return preheader;
    }

    /**
     * 将指令移动到指定基本块
     */
    void moveInstructionToBlock(std::shared_ptr<IRInstruction> inst,
                              std::shared_ptr<IRBasicBlock> block) {
        // 从原基本块中移除指令
        auto oldBlock = inst->getParent();
        oldBlock->removeInstruction(inst);

        // 添加到新基本块
        block->addInstruction(inst);
    }

    /**
     * 重定向基本块的终止指令
     */
    void redirectTerminator(std::shared_ptr<IRBasicBlock> block,
                          std::shared_ptr<IRBasicBlock> oldTarget,
                          std::shared_ptr<IRBasicBlock> newTarget) {
        auto term = block->getTerminator();
        if (!term) return;

        // 更新终止指令的目标
        for (size_t i = 0; i < term->getOperands().size(); ++i) {
            if (term->getOperand(i) == oldTarget) {
                term->setOperand(i, newTarget);
            }
        }
    }

    /**
     * 检查指令是否有副作用
     */
    bool hasSideEffects(std::shared_ptr<IRInstruction> inst) {
        switch (inst->getOpType()) {
            case IROpType::STORE:
            case IROpType::CALL:
            case IROpType::BR:
            case IROpType::JMP:
            case IROpType::RET:
                return true;
            default:
                return false;
        }
    }
};

std::shared_ptr<IRNode> LoopUnrollingOptimizer::optimize(std::shared_ptr<IRNode> node) {
    if (auto func = std::dynamic_pointer_cast<IRFunction>(node)) {
        bool changed = false;

        // 识别循环
        auto cfg = buildCFG(func);
        std::vector<Loop> loops;

        // 查找循环头
        for (auto& block : func->getBasicBlocks()) {
            if (isLoopHeader(block, cfg)) {
                auto loop = analyzeLoop(block, cfg);
                loops.push_back(loop);
            }
        }

        // 对每个循环尝试展开
        for (auto& loop : loops) {
            if (shouldUnroll(loop)) {
                if (auto tripCount = getTripCount(loop)) {
                    changed |= unrollLoop(loop, *tripCount);
                }
            }
        }

        return changed ? func : nullptr;
    }
    return node;
}

bool LoopUnrollingOptimizer::shouldUnroll(const Loop& loop) const {
    // 检查循环是否适合展开：
    // 1. 循环体不能太大（指令数量限制）
    size_t instructionCount = 0;
    for (auto block : loop.blocks) {
        instructionCount += block->getInstructions().size();
    }
    if (instructionCount > 50) return false;  // 循环体太大，不展开

    // 2. 循环必须是规范形式（有一个入口，一个出口）
    if (loop.blocks.size() != 2) return false;  // 只有循环头和循环体

    // 3. 循环变量必须是简单的递增或递减
    auto header = loop.header;
    auto terminator = header->getTerminator();
    if (!terminator || terminator->getOpType() != IROpType::BR) return false;

    return true;
}

std::optional<int64_t> LoopUnrollingOptimizer::getTripCount(const Loop& loop) const {
    // 分析循环的迭代次数：
    // 1. 找到循环变量和边界条件
    auto header = loop.header;
    auto terminator = header->getTerminator();
    auto condition = terminator->getOperand(0);

    // 检查条件指令
    auto condInst = std::dynamic_pointer_cast<IRInstruction>(condition);
    if (!condInst || condInst->getOpType() != IROpType::LT) return std::nullopt;

    // 获取循环变量和边界值
    auto inductionVar = condInst->getOperand(0);
    auto boundVar = condInst->getOperand(1);

    // 2. 检查循环变量的初始值和步长
    auto initValue = getInitialValue(inductionVar);
    auto stepValue = getStepValue(inductionVar, loop);

    if (!initValue || !stepValue) return std::nullopt;

    // 3. 如果边界值是常量，计算迭代次数
    if (auto boundConst = std::dynamic_pointer_cast<IRConstant>(boundVar)) {
        auto bound = std::get<int64_t>(boundConst->getValue());
        auto init = *initValue;
        auto step = *stepValue;

        // 计算迭代次数：(bound - init + step - 1) / step
        return (bound - init + step - 1) / step;
    }

    return std::nullopt;
}

bool LoopUnrollingOptimizer::unrollLoop(Loop& loop, int64_t tripCount) {
    // 如果迭代次数太少，不展开
    if (tripCount <= 2) return false;

    // 计算实际展开次数（不超过迭代次数和展开因子）
    size_t actualUnrollFactor = std::min(static_cast<size_t>(tripCount), unrollFactor_);

    // 1. 复制循环体
    std::vector<std::shared_ptr<IRBasicBlock>> unrolledBlocks;
    std::unordered_map<std::shared_ptr<IRNode>, std::shared_ptr<IRNode>> oldToNew;

    for (size_t i = 0; i < actualUnrollFactor - 1; ++i) {
        auto newBlock = cloneLoopBody(loop, oldToNew);
        unrolledBlocks.push_back(newBlock);
    }

    // 2. 更新循环变量
    updateInductionVariable(loop, actualUnrollFactor);

    // 3. 更新循环头的条件
    auto header = loop.header;
    auto terminator = header->getTerminator();
    auto condition = terminator->getOperand(0);

    // 4. 连接展开的基本块
    auto originalBody = *std::next(loop.blocks.begin());
    auto& function = header->getParent();

    // 在原循环体后插入展开的基本块
    auto it = std::find(function->getBasicBlocks().begin(),
                       function->getBasicBlocks().end(),
                       originalBody);
    if (it != function->getBasicBlocks().end()) {
        ++it;  // 移到下一个位置
        function->getBasicBlocks().insert(it, unrolledBlocks.begin(), unrolledBlocks.end());
    }

    return true;
}

std::shared_ptr<IRBasicBlock> LoopUnrollingOptimizer::cloneLoopBody(
    const Loop& loop,
    std::unordered_map<std::shared_ptr<IRNode>, std::shared_ptr<IRNode>>& oldToNew
) {
    auto newBlock = std::make_shared<IRBasicBlock>();

    // 获取原循环体（第二个基本块）
    auto originalBody = *std::next(loop.blocks.begin());

    // 复制指令
    for (auto& inst : originalBody->getInstructions()) {
        auto newInst = std::make_shared<IRInstruction>(inst->getOpType());

        // 复制操作数，更新引用
        for (auto& operand : inst->getOperands()) {
            auto it = oldToNew.find(operand);
            if (it != oldToNew.end()) {
                newInst->addOperand(it->second);
            } else {
                newInst->addOperand(operand);
            }
        }

        newBlock->addInstruction(newInst);
        oldToNew[inst] = newInst;
    }

    return newBlock;
}

void LoopUnrollingOptimizer::updateInductionVariable(
    const Loop& loop,
    int64_t increment
) {
    // 找到循环变量的递增指令
    auto body = *std::next(loop.blocks.begin());
    for (auto& inst : body->getInstructions()) {
        if (inst->getOpType() == IROpType::ADD) {
            auto operand = inst->getOperand(1);
            if (auto constant = std::dynamic_pointer_cast<IRConstant>(operand)) {
                // 更新递增值
                auto newValue = std::get<int64_t>(constant->getValue()) * increment;
                inst->setOperand(1, std::make_shared<IRConstant>(newValue));
                break;
            }
        }
    }
}

} // namespace ir
} // namespace collie