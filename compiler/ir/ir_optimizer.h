/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 优化器头文件。优化器接口定义了各种优化策略
 */

#ifndef COLLIE_IR_OPTIMIZER_H_
#define COLLIE_IR_OPTIMIZER_H_

#include <memory>
#include <vector>
#include "ir_node.h"
#include <unordered_set>
#include <stack>

namespace collie {
namespace ir {

// IR 优化器基类
class IROptimizer {
public:
    virtual ~IROptimizer() = default;

    // 对给定的 IR 节点进行优化，返回是否进行了修改
    virtual bool optimize(std::shared_ptr<IRNode> ir) = 0;
};

// 常量折叠优化器
class ConstantFoldingOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> ir) override;

private:
    // 对单个指令进行常量折叠
    bool foldConstantExpr(std::shared_ptr<IRInstruction> inst);

    // 检查操作数是否为常量
    bool isConstant(const std::shared_ptr<IROperand>& operand) const;

    // 计算常量表达式的结果
    std::shared_ptr<IRConstant> evaluateConstantExpr(
        IROpType op,
        const std::shared_ptr<IRConstant>& left,
        const std::shared_ptr<IRConstant>& right
    );
};

// 死代码消除优化器
class DeadCodeEliminationOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> ir) override;

private:
    // 消除基本块中的死代码
    bool eliminateDeadCode(std::shared_ptr<IRBasicBlock> block);

    // 检查指令是否为死代码
    bool isDeadInstruction(const std::shared_ptr<IRInstruction>& inst) const;
};

// 基本块合并优化器
class BlockMergingOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> ir) override;

private:
    // 合并函数中的基本块
    bool mergeBlocks(std::shared_ptr<IRFunction> func);

    // 检查两个基本块是否可以合并
    bool canMergeBlocks(
        const std::shared_ptr<IRBasicBlock>& first,
        const std::shared_ptr<IRBasicBlock>& second
    ) const;
};

// 循环优化器
class LoopOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> ir) override;

private:
    // 优化循环
    bool optimizeLoop(std::shared_ptr<IRBasicBlock> header);

    // 检查基本块是否为循环头
    bool isLoopHeader(const std::shared_ptr<IRBasicBlock>& block) const;

    // 检查指令是否可以提升出循环
    bool canHoistInstruction(const std::shared_ptr<IRInstruction>& inst) const;
};

// 优化级别
enum class OptimizationLevel {
    O0 = 0,  // 无优化
    O1 = 1,  // 基本优化
    O2 = 2,  // 中等优化
    O3 = 3   // 激进优化
};

// 优化管理器
class OptimizationManager {
public:
    OptimizationManager(OptimizationLevel level = OptimizationLevel::O1)
        : optimizationLevel_(level), maxIterations_(100) {}

    // 添加优化器
    void addOptimizer(std::shared_ptr<IROptimizer> optimizer);

    // 运行所有优化
    bool runOptimizations(std::shared_ptr<IRNode> ir);

    // 设置优化级别
    void setOptimizationLevel(OptimizationLevel level) {
        optimizationLevel_ = level;
    }

    // 设置最大迭代次数
    void setMaxIterations(int maxIterations) {
        maxIterations_ = maxIterations;
    }

private:
    std::vector<std::shared_ptr<IROptimizer>> optimizers_;
    OptimizationLevel optimizationLevel_;
    int maxIterations_;
};

/**
 * 控制流图
 */
class ControlFlowGraph {
public:
    using BlockSet = std::unordered_set<std::shared_ptr<IRBasicBlock>>;
    using BlockVector = std::vector<std::shared_ptr<IRBasicBlock>>;

    void addEdge(std::shared_ptr<IRBasicBlock> from,
                std::shared_ptr<IRBasicBlock> to) {
        successors_[from].insert(to);
        predecessors_[to].insert(from);
    }

    const BlockSet& getSuccessors(std::shared_ptr<IRBasicBlock> block) const {
        static const BlockSet empty;
        auto it = successors_.find(block);
        return it != successors_.end() ? it->second : empty;
    }

    const BlockSet& getPredecessors(std::shared_ptr<IRBasicBlock> block) const {
        static const BlockSet empty;
        auto it = predecessors_.find(block);
        return it != predecessors_.end() ? it->second : empty;
    }

private:
    std::unordered_map<std::shared_ptr<IRBasicBlock>, BlockSet> successors_;
    std::unordered_map<std::shared_ptr<IRBasicBlock>, BlockSet> predecessors_;
};

/**
 * 循环结构
 */
struct Loop {
    std::shared_ptr<IRBasicBlock> header;  // 循环头
    std::unordered_set<std::shared_ptr<IRBasicBlock>> blocks;  // 循环体中的基本块
};

/**
 * 构建函数的控制流图
 */
inline ControlFlowGraph buildCFG(std::shared_ptr<IRFunction> func) {
    ControlFlowGraph cfg;

    for (auto& block : func->getBasicBlocks()) {
        auto term = block->getTerminator();
        if (!term) continue;

        // 根据终止指令类型添加边
        switch (term->getOpType()) {
            case IROpType::BR: {
                // 条件跳转有两个目标
                auto trueBlock = std::dynamic_pointer_cast<IRBasicBlock>(
                    term->getOperand(1));
                auto falseBlock = std::dynamic_pointer_cast<IRBasicBlock>(
                    term->getOperand(2));
                if (trueBlock) cfg.addEdge(block, trueBlock);
                if (falseBlock) cfg.addEdge(block, falseBlock);
                break;
            }
            case IROpType::JMP: {
                // 无条件跳转只有一个目标
                auto target = std::dynamic_pointer_cast<IRBasicBlock>(
                    term->getOperand(0));
                if (target) cfg.addEdge(block, target);
                break;
            }
            default:
                break;
        }
    }

    return cfg;
}

/**
 * 判断一个基本块是否支配另一个基本块
 */
inline bool dominates(std::shared_ptr<IRBasicBlock> dominator,
                     std::shared_ptr<IRBasicBlock> block,
                     const ControlFlowGraph& cfg) {
    if (dominator == block) return true;

    std::unordered_set<std::shared_ptr<IRBasicBlock>> visited;
    std::stack<std::shared_ptr<IRBasicBlock>> workList;

    // 从 block 开始反向遍历
    workList.push(block);

    while (!workList.empty()) {
        auto current = workList.top();
        workList.pop();

        if (visited.count(current) > 0) continue;
        visited.insert(current);

        // 如果找到了 dominator，说明它不支配 block
        if (current == dominator) return false;

        // 将所有前驱加入工作列表
        for (auto pred : cfg.getPredecessors(current)) {
            if (visited.count(pred) == 0) {
                workList.push(pred);
            }
        }
    }

    return true;
}

} // namespace ir
} // namespace collie

#endif // COLLIE_IR_OPTIMIZER_H_