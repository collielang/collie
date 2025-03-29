/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-08
 * @Description: IR 节点实现
 */

#include "ir_node.h"
#include <sstream>

namespace collie {
namespace ir {

std::shared_ptr<IRInstruction> IRVariable::getDefiningInstruction() const {
    return definingInstruction_.lock();
}

IRType IRConstant::getType() const {
    if (std::holds_alternative<bool>(value_)) {
        return IRType::BOOL;
    } else if (std::holds_alternative<int64_t>(value_)) {
        return IRType::INT;
    } else if (std::holds_alternative<double>(value_)) {
        return IRType::FLOAT;
    } else if (std::holds_alternative<std::string>(value_)) {
        return IRType::STRING;
    }
    return IRType::VOID;
}

std::string IRConstant::toString() const {
    std::stringstream ss;
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            ss << "\"" << arg << "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
            ss << (arg ? "true" : "false");
        } else {
            ss << arg;
        }
    }, value_);
    return ss.str();
}

std::vector<std::shared_ptr<IRInstruction>> IRInstruction::getUsers() const {
    std::vector<std::shared_ptr<IRInstruction>> result;
    for (const auto& user : users_) {
        if (auto inst = user.lock()) {
            result.push_back(inst);
        }
    }
    return result;
}

void IRInstruction::replaceAllUsesWith(std::shared_ptr<IROperand> newOperand) {
    for (auto& user : users_) {
        if (auto inst = user.lock()) {
            for (size_t i = 0; i < inst->operands_.size(); ++i) {
                if (inst->operands_[i].get() == this) {
                    inst->operands_[i] = newOperand;
                }
            }
        }
    }
}

std::string IRInstruction::toString() const {
    std::stringstream ss;
    ss << "%" << this << " = ";

    switch (opType_) {
        case IROpType::ADD:
            ss << "add ";
            break;
        case IROpType::SUB:
            ss << "sub ";
            break;
        case IROpType::MUL:
            ss << "mul ";
            break;
        case IROpType::DIV:
            ss << "div ";
            break;
        case IROpType::MOD:
            ss << "mod ";
            break;
        case IROpType::NEG:
            ss << "neg ";
            break;
        case IROpType::EQ:
            ss << "eq ";
            break;
        case IROpType::NE:
            ss << "ne ";
            break;
        case IROpType::LT:
            ss << "lt ";
            break;
        case IROpType::LE:
            ss << "le ";
            break;
        case IROpType::GT:
            ss << "gt ";
            break;
        case IROpType::GE:
            ss << "ge ";
            break;
        case IROpType::AND:
            ss << "and ";
            break;
        case IROpType::OR:
            ss << "or ";
            break;
        case IROpType::NOT:
            ss << "not ";
            break;
        case IROpType::BR:
            ss << "br ";
            break;
        case IROpType::JMP:
            ss << "jmp ";
            break;
        case IROpType::RET:
            ss << "ret ";
            break;
        case IROpType::LOAD:
            ss << "load ";
            break;
        case IROpType::STORE:
            ss << "store ";
            break;
        case IROpType::ALLOCA:
            ss << "alloca ";
            break;
        case IROpType::CALL:
            ss << "call ";
            break;
        case IROpType::CAST:
            ss << "cast ";
            break;
        case IROpType::PHI:
            ss << "phi ";
            break;
        case IROpType::NOP:
            ss << "nop";
            break;
    }

    for (size_t i = 0; i < operands_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << operands_[i]->toString();
    }

    return ss.str();
}

std::shared_ptr<IRInstruction> IRBasicBlock::getTerminator() const {
    if (instructions_.empty()) return nullptr;
    auto& last = instructions_.back();
    switch (last->getOpType()) {
        case IROpType::BR:
        case IROpType::JMP:
        case IROpType::RET:
            return last;
        default:
            return nullptr;
    }
}

std::vector<std::shared_ptr<IRBasicBlock>> IRBasicBlock::getSuccessors() const {
    std::vector<std::shared_ptr<IRBasicBlock>> result;
    auto term = getTerminator();
    if (!term) return result;

    switch (term->getOpType()) {
        case IROpType::BR: {
            // 条件跳转有两个目标
            auto trueBlock = std::dynamic_pointer_cast<IRBasicBlock>(term->getOperands()[1]);
            auto falseBlock = std::dynamic_pointer_cast<IRBasicBlock>(term->getOperands()[2]);
            if (trueBlock) result.push_back(trueBlock);
            if (falseBlock) result.push_back(falseBlock);
            break;
        }
        case IROpType::JMP: {
            // 无条件跳转只有一个目标
            auto target = std::dynamic_pointer_cast<IRBasicBlock>(term->getOperands()[0]);
            if (target) result.push_back(target);
            break;
        }
        default:
            break;
    }

    return result;
}

std::vector<std::shared_ptr<IRBasicBlock>> IRBasicBlock::getPredecessors() const {
    std::vector<std::shared_ptr<IRBasicBlock>> result;
    for (const auto& pred : predecessors_) {
        if (auto block = pred.lock()) {
            result.push_back(block);
        }
    }
    return result;
}

void IRBasicBlock::setInstructions(const std::vector<std::shared_ptr<IRInstruction>>& instructions) {
    instructions_ = instructions;
    for (auto& inst : instructions_) {
        inst->setParent(shared_from_this());
    }
}

std::string IRBasicBlock::toString() const {
    std::stringstream ss;
    ss << "block_" << this << ":\n";
    for (const auto& inst : instructions_) {
        ss << "  " << inst->toString() << "\n";
    }
    return ss.str();
}

std::string IRFunction::toString() const {
    std::stringstream ss;
    ss << "function " << name_ << " {\n";
    for (const auto& block : blocks_) {
        ss << block->toString();
    }
    ss << "}\n";
    return ss.str();
}

} // namespace ir
} // namespace collie
