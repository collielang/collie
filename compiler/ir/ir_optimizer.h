/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 优化器头文件。优化器接口定义了各种优化策略
 */

#ifndef COLLIE_IR_OPTIMIZER_H_
#define COLLIE_IR_OPTIMIZER_H_

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <optional>
#include <stack>
#include "ir_node.h"

namespace collie {
namespace ir {

// 前向声明
class IRNode;
class IRFunction;
class IRBasicBlock;
class IRInstruction;
class IROperand;
class IRVariable;
class IRConstant;

/**
 * 循环结构
 */
struct Loop {
    std::shared_ptr<IRBasicBlock> header;  // 循环头基本块
    std::unordered_set<std::shared_ptr<IRBasicBlock>> blocks;  // 循环中的所有基本块
};

/**
 * IR优化器基类
 */
class IROptimizer {
public:
    virtual ~IROptimizer() = default;

    // 对给定的 IR 节点进行优化，返回是否进行了修改
    virtual bool optimize(std::shared_ptr<IRNode> node) = 0;
};

/**
 * 常量折叠优化器
 */
class ConstantFoldingOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> node) override;

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

/**
 * 死代码消除优化器
 */
class DeadCodeEliminationOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> node) override;

private:
    // 消除基本块中的死代码
    bool eliminateDeadCode(std::shared_ptr<IRBasicBlock> block);

    // 检查指令是否为死代码
    bool isDeadInstruction(const std::shared_ptr<IRInstruction>& inst) const;
};

/**
 * 基本块合并优化器
 */
class BlockMergingOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> node) override;

private:
    // 合并函数中的基本块
    bool mergeBlocks(std::shared_ptr<IRFunction> func);

    // 检查两个基本块是否可以合并
    bool canMergeBlocks(
        const std::shared_ptr<IRBasicBlock>& first,
        const std::shared_ptr<IRBasicBlock>& second
    ) const;
};

/**
 * 循环优化器
 */
class LoopOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> node) override;

private:
    // 优化循环
    bool optimizeLoop(std::shared_ptr<IRBasicBlock> header);

    // 检查基本块是否为循环头
    bool isLoopHeader(const std::shared_ptr<IRBasicBlock>& block) const;

    // 检查指令是否可以提升出循环
    bool canHoistInstruction(const std::shared_ptr<IRInstruction>& inst) const;
};

/**
 * 循环展开优化器
 * 通过复制循环体来减少循环控制开销，提高指令级并行性
 */
class LoopUnrollingOptimizer : public IROptimizer {
public:
    explicit LoopUnrollingOptimizer(size_t unrollFactor = 4)
        : unrollFactor_(unrollFactor) {}

    bool optimize(std::shared_ptr<IRNode> node) override;

private:
    /**
     * 判断循环是否适合展开
     * @param loop 要分析的循环
     * @return 是否适合展开
     */
    bool shouldUnroll(const Loop& loop) const;

    /**
     * 获取循环的迭代次数
     * @param loop 要分析的循环
     * @return 如果能确定迭代次数，返回迭代次数；否则返回 std::nullopt
     */
    std::optional<int64_t> getTripCount(const Loop& loop) const;

    /**
     * 展开循环
     * @param loop 要展开的循环
     * @param tripCount 循环迭代次数
     * @return 是否成功展开
     */
    bool unrollLoop(Loop& loop, int64_t tripCount);

    /**
     * 复制循环体
     * @param loop 循环信息
     * @param oldToNew 旧指令到新指令的映射
     * @return 复制的基本块
     */
    std::shared_ptr<IRBasicBlock> cloneLoopBody(
        const Loop& loop,
        std::unordered_map<std::shared_ptr<IRNode>, std::shared_ptr<IRNode>>& oldToNew
    );

    /**
     * 更新循环变量
     * @param loop 循环信息
     * @param increment 每次迭代的增量
     */
    void updateInductionVariable(
        const Loop& loop,
        int64_t increment
    );

    size_t unrollFactor_;  // 展开因子
};

/**
 * 循环不变量外提优化器
 * 识别循环中不变的计算并将其移动到循环外部，减少循环内的计算开销
 */
class LoopInvariantMotionOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> node) override;

private:
    /**
     * 优化单个函数
     * @param func 要优化的函数
     * @return 如果进行了优化返回优化后的函数，否则返回nullptr
     */
    std::shared_ptr<IRNode> optimizeFunction(std::shared_ptr<IRFunction> func);

    /**
     * 优化单个循环
     * @param loop 要优化的循环
     * @return 是否进行了优化
     */
    bool optimizeLoop(const Loop& loop);

    /**
     * 判断指令是否是循环不变量
     * @param inst 要分析的指令
     * @param loop 循环信息
     * @return 是否是循环不变量
     */
    bool isLoopInvariant(std::shared_ptr<IRInstruction> inst, const Loop& loop);

    /**
     * 创建循环前导基本块
     * @param loop 循环信息
     * @return 创建的前导基本块
     */
    std::shared_ptr<IRBasicBlock> createLoopPreheader(const Loop& loop);

    /**
     * 将指令移动到指定基本块
     * @param inst 要移动的指令
     * @param block 目标基本块
     */
    void moveInstructionToBlock(
        std::shared_ptr<IRInstruction> inst,
        std::shared_ptr<IRBasicBlock> block
    );

    /**
     * 重定向基本块的终止指令
     * @param block 要更新的基本块
     * @param oldTarget 原目标基本块
     * @param newTarget 新目标基本块
     */
    void redirectTerminator(
        std::shared_ptr<IRBasicBlock> block,
        std::shared_ptr<IRBasicBlock> oldTarget,
        std::shared_ptr<IRBasicBlock> newTarget
    );

    /**
     * 检查指令是否有副作用
     * @param inst 要检查的指令
     * @return 是否有副作用
     */
    bool hasSideEffects(std::shared_ptr<IRInstruction> inst);
};

/**
 * 循环强度削弱优化器
 */
class LoopStrengthReductionOptimizer : public IROptimizer {
public:
    bool optimize(std::shared_ptr<IRNode> node) override;

private:
    struct InductionVariable {
        std::shared_ptr<IRVariable> var;      // 归纳变量
        std::shared_ptr<IRInstruction> init;  // 初始值指令
        std::shared_ptr<IRInstruction> step;  // 步长指令
        std::vector<std::shared_ptr<IRInstruction>> uses;  // 使用该变量的指令
    };

    bool optimizeFunction(std::shared_ptr<IRFunction> func);
    bool optimizeLoop(const Loop& loop);
    std::vector<InductionVariable> collectInductionVariables(const Loop& loop);
    std::shared_ptr<IRVariable> isInductionVariableUpdate(
        std::shared_ptr<IRInstruction> inst,
        const Loop& loop
    );
    std::shared_ptr<IRInstruction> findInitialValue(
        std::shared_ptr<IRVariable> var,
        const Loop& loop
    );
    std::vector<std::shared_ptr<IRInstruction>> findVariableUses(
        std::shared_ptr<IRVariable> var,
        const Loop& loop
    );
    bool isDefinedInLoop(
        std::shared_ptr<IRVariable> var,
        const Loop& loop
    );
    bool reduceStrength(const InductionVariable& iv, const Loop& loop);
    bool convertMultiplicationToAddition(
        std::shared_ptr<IRInstruction> mulInst,
        const InductionVariable& iv,
        std::shared_ptr<IROperand> factor,
        const Loop& loop
    );
    bool isLoopInvariant(
        std::shared_ptr<IROperand> operand,
        const Loop& loop
    );
    std::vector<Loop> identifyLoops(std::shared_ptr<IRFunction> func);
};

/**
 * 优化级别
 */
enum class OptimizationLevel {
    O0 = 0,  // 无优化
    O1 = 1,  // 基本优化
    O2 = 2,  // 中等优化
    O3 = 3   // 激进优化
};

/**
 * 优化管理器
 */
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

// 辅助函数声明
std::optional<int64_t> getInitialValue(std::shared_ptr<IROperand> var);
std::optional<int64_t> getStepValue(std::shared_ptr<IROperand> var, const Loop& loop);
bool dominates(std::shared_ptr<IRBasicBlock> a, std::shared_ptr<IRBasicBlock> b, const ControlFlowGraph& cfg);
Loop analyzeLoop(std::shared_ptr<IRBasicBlock> header, const ControlFlowGraph& cfg);
bool isLoopHeader(std::shared_ptr<IRBasicBlock> block, const ControlFlowGraph& cfg);
std::shared_ptr<IRBasicBlock> createLoopPreheader(const Loop& loop);

} // namespace ir
} // namespace collie

#endif // COLLIE_IR_OPTIMIZER_H_