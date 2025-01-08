/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 节点定义，包含基本的 IR 指令结构和类型
 */

#ifndef COLLIE_IR_NODE_H_
#define COLLIE_IR_NODE_H_

#include <memory>
#include <vector>
#include <string>
#include <variant>
#include <unordered_map>
#include <unordered_set>

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
 * IR操作类型
 */
enum class IROpType {
    // 算术运算
    ADD,    // 加法
    SUB,    // 减法
    MUL,    // 乘法
    DIV,    // 除法
    MOD,    // 取模
    NEG,    // 取负

    // 位运算
    AND,    // 按位与
    OR,     // 按位或
    XOR,    // 按位异或
    NOT,    // 按位取反
    SHL,    // 左移
    SHR,    // 右移

    // 比较运算
    EQ,     // 等于
    NE,     // 不等于
    LT,     // 小于
    LE,     // 小于等于
    GT,     // 大于
    GE,     // 大于等于

    // 控制流
    BR,     // 条件跳转
    JMP,    // 无条件跳转
    RET,    // 返回
    CALL,   // 函数调用

    // 内存操作
    ALLOCA, // 分配栈空间
    LOAD,   // 加载
    STORE,  // 存储

    // 类型转换
    CAST,   // 类型转换

    // 其他
    PHI,    // φ函数
    NOP     // 空操作
};

/**
 * IR类型
 */
enum class IRType {
    VOID,   // 空类型
    BOOL,   // 布尔类型
    INT,    // 整数类型
    FLOAT,  // 浮点类型
    STRING  // 字符串类型
};

/**
 * IR节点基类
 */
class IRNode {
public:
    virtual ~IRNode() = default;
    virtual std::string toString() const = 0;
    virtual void dump() const;
};

/**
 * IR操作数基类
 */
class IROperand : public IRNode {
public:
    virtual ~IROperand() = default;
    virtual IRType getType() const = 0;
    virtual std::shared_ptr<IRInstruction> getDefiningInstruction() const = 0;
    virtual bool isConstant() const = 0;
    virtual bool isVariable() const = 0;
};

/**
 * IR变量
 */
class IRVariable : public IROperand {
public:
    IRVariable(const std::string& name, IRType type)
        : name_(name), type_(type) {}

    bool isConstant() const override { return false; }
    bool isVariable() const override { return true; }
    std::string getName() const { return name_; }
    IRType getType() const override { return type_; }
    std::string toString() const override { return "%" + name_; }
    std::shared_ptr<IRInstruction> getDefiningInstruction() const override;

private:
    std::string name_;
    IRType type_;
    std::weak_ptr<IRInstruction> definingInstruction_;
};

/**
 * IR常量
 */
class IRConstant : public IROperand {
public:
    using ValueType = std::variant<bool, int64_t, double, std::string>;

    explicit IRConstant(const ValueType& value) : value_(value) {}
    bool isConstant() const override { return true; }
    bool isVariable() const override { return false; }
    IRType getType() const override;
    const ValueType& getValue() const { return value_; }
    std::string toString() const override;
    std::shared_ptr<IRInstruction> getDefiningInstruction() const override { return nullptr; }

private:
    ValueType value_;
};

/**
 * IR标签操作数
 */
class IRLabel : public IROperand {
public:
    explicit IRLabel(const std::string& name) : name_(name) {}

    bool isConstant() const override { return true; }
    bool isVariable() const override { return false; }
    IRType getType() const override { return IRType::VOID; }
    const std::string& getName() const { return name_; }
    std::string toString() const override { return name_ + ":"; }
    std::shared_ptr<IRInstruction> getDefiningInstruction() const override { return nullptr; }

private:
    std::string name_;
};

/**
 * IR指令
 */
class IRInstruction : public IRNode, public std::enable_shared_from_this<IRInstruction> {
public:
    explicit IRInstruction(IROpType opType) : opType_(opType) {}

    IROpType getOpType() const { return opType_; }
    const std::vector<std::shared_ptr<IROperand>>& getOperands() const { return operands_; }
    std::vector<std::shared_ptr<IROperand>>& getOperands() { return operands_; }
    void addOperand(std::shared_ptr<IROperand> operand) { operands_.push_back(operand); }
    void setOperand(size_t index, std::shared_ptr<IROperand> operand) { operands_[index] = operand; }
    std::shared_ptr<IRBasicBlock> getParent() const { return parent_.lock(); }
    void setParent(std::shared_ptr<IRBasicBlock> parent) { parent_ = parent; }
    std::vector<std::shared_ptr<IRInstruction>> getUsers() const;
    void replaceAllUsesWith(std::shared_ptr<IROperand> newOperand);
    std::string toString() const override;

private:
    IROpType opType_;
    std::vector<std::shared_ptr<IROperand>> operands_;
    std::weak_ptr<IRBasicBlock> parent_;
    std::vector<std::weak_ptr<IRInstruction>> users_;
};

/**
 * IR基本块
 */
class IRBasicBlock : public IRNode, public std::enable_shared_from_this<IRBasicBlock> {
public:
    const std::vector<std::shared_ptr<IRInstruction>>& getInstructions() const { return instructions_; }
    std::vector<std::shared_ptr<IRInstruction>>& getInstructions() { return instructions_; }
    void addInstruction(std::shared_ptr<IRInstruction> inst) {
        inst->setParent(shared_from_this());
        instructions_.push_back(inst);
    }
    std::shared_ptr<IRFunction> getParent() const { return parent_.lock(); }
    void setParent(std::shared_ptr<IRFunction> parent) { parent_ = parent; }
    std::shared_ptr<IRInstruction> getTerminator() const;
    std::vector<std::shared_ptr<IRBasicBlock>> getSuccessors() const;
    std::vector<std::shared_ptr<IRBasicBlock>> getPredecessors() const;
    void setInstructions(const std::vector<std::shared_ptr<IRInstruction>>& instructions);
    std::string toString() const override;

private:
    std::vector<std::shared_ptr<IRInstruction>> instructions_;
    std::weak_ptr<IRFunction> parent_;
    std::vector<std::weak_ptr<IRBasicBlock>> predecessors_;
};

/**
 * IR函数
 */
class IRFunction : public IRNode, public std::enable_shared_from_this<IRFunction> {
public:
    explicit IRFunction(const std::string& name) : name_(name) {}

    const std::string& getName() const { return name_; }
    const std::vector<std::shared_ptr<IRBasicBlock>>& getBasicBlocks() const { return blocks_; }
    std::vector<std::shared_ptr<IRBasicBlock>>& getBasicBlocks() { return blocks_; }
    void addBasicBlock(std::shared_ptr<IRBasicBlock> block) {
        block->setParent(shared_from_this());
        blocks_.push_back(block);
    }
    std::string toString() const override;

private:
    std::string name_;
    std::vector<std::shared_ptr<IRBasicBlock>> blocks_;
};

/**
 * 控制流图
 */
class ControlFlowGraph {
public:
    using BlockSet = std::unordered_set<std::shared_ptr<IRBasicBlock>>;

    void addEdge(std::shared_ptr<IRBasicBlock> from, std::shared_ptr<IRBasicBlock> to) {
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
                    term->getOperands()[1]);
                auto falseBlock = std::dynamic_pointer_cast<IRBasicBlock>(
                    term->getOperands()[2]);
                if (trueBlock) cfg.addEdge(block, trueBlock);
                if (falseBlock) cfg.addEdge(block, falseBlock);
                break;
            }
            case IROpType::JMP: {
                // 无条件跳转只有一个目标
                auto target = std::dynamic_pointer_cast<IRBasicBlock>(
                    term->getOperands()[0]);
                if (target) cfg.addEdge(block, target);
                break;
            }
            default:
                break;
        }
    }

    return cfg;
}

} // namespace ir
} // namespace collie

#endif // COLLIE_IR_NODE_H_