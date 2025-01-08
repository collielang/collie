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

        case TokenType::END_OF_FILE: return "END_OF_FILE";
        case TokenType::INVALID: return "INVALID";
        case TokenType::TOKEN_ERROR: return "TOKEN_ERROR";
        case TokenType::LITERAL_NUMBER: return "LITERAL_NUMBER";
        case TokenType::LITERAL_STRING: return "LITERAL_STRING";
        case TokenType::LITERAL_CHAR: return "LITERAL_CHAR";
        case TokenType::LITERAL_CHARACTER: return "LITERAL_CHARACTER";
        case TokenType::LITERAL_BOOL: return "LITERAL_BOOL";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::KW_INTEGER: return "KW_INTEGER";
        case TokenType::KW_DECIMAL: return "KW_DECIMAL";
        case TokenType::KW_TRIBOOL: return "KW_TRIBOOL";
        case TokenType::KW_BIT: return "KW_BIT";
        case TokenType::KW_VAR: return "KW_VAR";
        case TokenType::KW_FUNCTION: return "KW_FUNCTION";
        case TokenType::KW_CLASS: return "KW_CLASS";
        case TokenType::KW_PUBLIC: return "KW_PUBLIC";
        case TokenType::KW_PRIVATE: return "KW_PRIVATE";
        case TokenType::KW_PROTECTED: return "KW_PROTECTED";
        case TokenType::KW_IF: return "KW_IF";
        case TokenType::KW_ELSE: return "KW_ELSE";
        case TokenType::KW_WHILE: return "KW_WHILE";
        case TokenType::KW_FOR: return "KW_FOR";
        case TokenType::KW_RETURN: return "KW_RETURN";
        case TokenType::KW_BREAK: return "KW_BREAK";
        case TokenType::KW_CONTINUE: return "KW_CONTINUE";
        case TokenType::KW_NULL: return "KW_NULL";
        case TokenType::KW_TRUE: return "KW_TRUE";
        case TokenType::KW_FALSE: return "KW_FALSE";
        case TokenType::KW_UNSET: return "KW_UNSET";
        case TokenType::DELIMITER_LPAREN: return "DELIMITER_LPAREN";
        case TokenType::DELIMITER_RPAREN: return "DELIMITER_RPAREN";
        case TokenType::DELIMITER_LBRACE: return "DELIMITER_LBRACE";
        case TokenType::DELIMITER_RBRACE: return "DELIMITER_RBRACE";
        case TokenType::DELIMITER_LBRACKET: return "DELIMITER_LBRACKET";
        case TokenType::DELIMITER_RBRACKET: return "DELIMITER_RBRACKET";
        case TokenType::DELIMITER_COMMA: return "DELIMITER_COMMA";
        case TokenType::DELIMITER_DOT: return "DELIMITER_DOT";
        case TokenType::DELIMITER_SEMICOLON: return "DELIMITER_SEMICOLON";
        case TokenType::OP_PLUS: return "OP_PLUS";
        case TokenType::OP_MINUS: return "OP_MINUS";
        case TokenType::OP_MULTIPLY: return "OP_MULTIPLY";
        case TokenType::OP_DIVIDE: return "OP_DIVIDE";
        case TokenType::OP_MODULO: return "OP_MODULO";
        case TokenType::OP_NOT: return "OP_NOT";
        case TokenType::OP_EQUAL: return "OP_EQUAL";
        case TokenType::OP_NOT_EQUAL: return "OP_NOT_EQUAL";
        case TokenType::OP_LESS: return "OP_LESS";
        case TokenType::OP_LESS_EQ: return "OP_LESS_EQ";
        case TokenType::OP_GREATER: return "OP_GREATER";
        case TokenType::OP_GREATER_EQ: return "OP_GREATER_EQ";
        case TokenType::OP_AND: return "OP_AND";
        case TokenType::OP_OR: return "OP_OR";
        case TokenType::OP_BIT_AND: return "OP_BIT_AND";
        case TokenType::OP_BIT_OR: return "OP_BIT_OR";
        case TokenType::OP_BIT_XOR: return "OP_BIT_XOR";
        case TokenType::OP_BIT_NOT: return "OP_BIT_NOT";
        case TokenType::OP_BIT_LSHIFT: return "OP_BIT_LSHIFT";
        case TokenType::OP_BIT_RSHIFT: return "OP_BIT_RSHIFT";
        case TokenType::OP_ASSIGN: return "OP_ASSIGN";
        case TokenType::OP_EQ_QUESTION: return "OP_EQ_QUESTION";
        case TokenType::OP_QUESTION_EQ: return "OP_QUESTION_EQ";
        case TokenType::OP_QUESTION: return "OP_QUESTION";
        case TokenType::OP_COLON: return "OP_COLON";
        default: return "UNKNOWN";
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