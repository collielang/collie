/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 节点实现
 */

#include "ir_node.h"
#include <iostream>
#include <sstream>

namespace collie {
namespace ir {

void IRNode::dump() const {
    std::cout << toString() << std::endl;
}

std::string IRConstant::toString() const {
    std::ostringstream oss;
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            oss << "\"" << arg << "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
            oss << (arg ? "true" : "false");
        } else {
            oss << arg;
        }
    }, value_);
    return oss.str();
}

std::string IRVariable::toString() const {
    return "%" + name_;
}

std::string IRLabel::toString() const {
    return name_ + ":";
}

std::string IRInstruction::toString() const {
    std::ostringstream oss;

    // 添加操作类型
    switch (opType_) {
        case IROpType::ADD: oss << "add"; break;
        case IROpType::SUB: oss << "sub"; break;
        case IROpType::MUL: oss << "mul"; break;
        case IROpType::DIV: oss << "div"; break;
        case IROpType::MOD: oss << "mod"; break;
        case IROpType::AND: oss << "and"; break;
        case IROpType::OR:  oss << "or";  break;
        case IROpType::XOR: oss << "xor"; break;
        case IROpType::NOT: oss << "not"; break;
        case IROpType::SHL: oss << "shl"; break;
        case IROpType::SHR: oss << "shr"; break;
        case IROpType::EQ:  oss << "eq";  break;
        case IROpType::NE:  oss << "ne";  break;
        case IROpType::LT:  oss << "lt";  break;
        case IROpType::LE:  oss << "le";  break;
        case IROpType::GT:  oss << "gt";  break;
        case IROpType::GE:  oss << "ge";  break;
        case IROpType::JMP: oss << "jmp"; break;
        case IROpType::CJMP: oss << "cjmp"; break;
        case IROpType::RET: oss << "ret"; break;
        case IROpType::CALL: oss << "call"; break;
        case IROpType::ALLOCA: oss << "alloca"; break;
        case IROpType::LOAD: oss << "load"; break;
        case IROpType::STORE: oss << "store"; break;
    }

    // 添加操作数
    for (const auto& operand : operands_) {
        oss << " " << operand->toString();
    }

    return oss.str();
}

std::string IRBasicBlock::toString() const {
    std::ostringstream oss;

    // 添加基本块标签
    oss << label_ << ":\n";

    // 添加指令
    for (const auto& inst : instructions_) {
        oss << "    " << inst->toString() << "\n";
    }

    return oss.str();
}

std::string IRFunction::toString() const {
    std::ostringstream oss;

    // 添加函数声明
    oss << "function " << name_ << " {\n";

    // 添加基本块
    for (const auto& block : blocks_) {
        oss << block->toString();
    }

    oss << "}\n";
    return oss.str();
}

void IRBasicBlock::addInstruction(std::shared_ptr<IRInstruction> inst) {
    instructions_.push_back(inst);
}

const std::vector<std::shared_ptr<IRInstruction>>& IRBasicBlock::getInstructions() const {
    return instructions_;
}

void IRFunction::addBasicBlock(std::shared_ptr<IRBasicBlock> block) {
    blocks_.push_back(block);
}

const std::vector<std::shared_ptr<IRBasicBlock>>& IRFunction::getBasicBlocks() const {
    return blocks_;
}

} // namespace ir
} // namespace collie