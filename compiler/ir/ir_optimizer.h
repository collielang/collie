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

} // namespace ir
} // namespace collie

#endif // COLLIE_IR_OPTIMIZER_H_