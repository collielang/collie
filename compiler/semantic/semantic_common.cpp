/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语义分析器公共定义的实现
 */
#include "semantic_common.h"
#include <sstream>

namespace collie {

bool FunctionSignature::operator==(const FunctionSignature& other) const {
    if (name != other.name || return_type != other.return_type) {
        return false;
    }

    if (parameter_types.size() != other.parameter_types.size()) {
        return false;
    }

    for (size_t i = 0; i < parameter_types.size(); ++i) {
        if (parameter_types[i] != other.parameter_types[i]) {
            return false;
        }
    }

    return true;
}

std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::KW_NUMBER: return "number";
        case TokenType::KW_STRING: return "string";
        case TokenType::KW_BOOL: return "bool";
        case TokenType::KW_CHAR: return "char";
        case TokenType::KW_CHARACTER: return "character";
        case TokenType::KW_BYTE: return "byte";
        case TokenType::KW_WORD: return "word";
        case TokenType::KW_DWORD: return "dword";
        case TokenType::KW_NONE: return "none";
        case TokenType::KW_OBJECT: return "object";
        default: return "unknown";
    }
}

std::string FunctionSignature::to_string() const {
    std::ostringstream oss;
    oss << name << "(";

    for (size_t i = 0; i < parameter_types.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << token_type_to_string(parameter_types[i]);
    }

    oss << ") -> " << token_type_to_string(return_type);
    return oss.str();
}

} // namespace collie