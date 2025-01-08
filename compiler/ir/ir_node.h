/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 节点定义，包含基本的 IR 指令结构和类型
 */

#ifndef COLLIE_IR_NODE_H
#define COLLIE_IR_NODE_H

#include <string>
#include <vector>
#include <memory>
#include <variant>

namespace collie {
namespace ir {

// IR 操作类型
enum class IROpType {
    // 算术运算
    ADD,    // 加法
    SUB,    // 减法
    MUL,    // 乘法
    DIV,    // 除法
    MOD,    // 取模

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
    JMP,    // 无条件跳转
    CJMP,   // 条件跳转
    RET,    // 返回
    CALL,   // 函数调用

    // 内存操作
    ALLOCA, // 分配内存
    LOAD,   // 加载
    STORE,  // 存储
};

// IR 节点基类
class IRNode {
public:
    virtual ~IRNode() = default;
    virtual std::string toString() const = 0;
    virtual void dump() const;
};

// IR 操作数基类
class IROperand : public IRNode {
public:
    virtual ~IROperand() = default;
    virtual bool isConstant() const = 0;
    virtual bool isVariable() const = 0;
};

// IR 常量操作数
class IRConstant : public IROperand {
public:
    using ValueType = std::variant<int64_t, double, bool, std::string>;

    explicit IRConstant(ValueType value) : value_(value) {}

    bool isConstant() const override { return true; }
    bool isVariable() const override { return false; }

    const ValueType& getValue() const { return value_; }
    std::string toString() const override;

private:
    ValueType value_;
};

// IR 变量操作数
class IRVariable : public IROperand {
public:
    explicit IRVariable(const std::string& name) : name_(name) {}

    bool isConstant() const override { return false; }
    bool isVariable() const override { return true; }

    const std::string& getName() const { return name_; }
    std::string toString() const override;

private:
    std::string name_;
};

// IR 标签操作数
class IRLabel : public IROperand {
public:
    explicit IRLabel(const std::string& name) : name_(name) {}

    bool isConstant() const override { return true; }
    bool isVariable() const override { return false; }

    const std::string& getName() const { return name_; }
    std::string toString() const override;

private:
    std::string name_;
};

// IR 指令基类
class IRInstruction : public IRNode {
public:
    IRInstruction(IROpType op) : opType_(op) {}
    virtual ~IRInstruction() = default;

    IROpType getOpType() const { return opType_; }
    void setOpType(IROpType op) { opType_ = op; }

    void addOperand(std::shared_ptr<IROperand> operand) {
        operands_.push_back(operand);
    }

    const std::vector<std::shared_ptr<IROperand>>& getOperands() const {
        return operands_;
    }

    std::string toString() const override;

protected:
    IROpType opType_;
    std::vector<std::shared_ptr<IROperand>> operands_;
};

// IR 基本块
class IRBasicBlock : public IRNode {
public:
    explicit IRBasicBlock(const std::string& label) : label_(label) {}
    virtual ~IRBasicBlock() = default;

    void addInstruction(std::shared_ptr<IRInstruction> inst);
    const std::vector<std::shared_ptr<IRInstruction>>& getInstructions() const;
    std::string toString() const override;

    const std::string& getLabel() const { return label_; }
    void setLabel(const std::string& label) { label_ = label; }

private:
    std::string label_;
    std::vector<std::shared_ptr<IRInstruction>> instructions_;
};

// IR 函数
class IRFunction : public IRNode {
public:
    explicit IRFunction(const std::string& name) : name_(name) {}
    virtual ~IRFunction() = default;

    void addBasicBlock(std::shared_ptr<IRBasicBlock> block);
    const std::vector<std::shared_ptr<IRBasicBlock>>& getBasicBlocks() const;
    std::string toString() const override;

    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

private:
    std::string name_;
    std::vector<std::shared_ptr<IRBasicBlock>> blocks_;
};

} // namespace ir
} // namespace collie

#endif // COLLIE_IR_NODE_H