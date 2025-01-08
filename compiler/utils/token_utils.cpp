/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "token_utils.h"

namespace collie {

std::string token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::KW_NUMBER: return "KW_NUMBER";
        case TokenType::KW_STRING: return "KW_STRING";
        case TokenType::KW_BOOL: return "KW_BOOL";
        case TokenType::KW_CHAR: return "KW_CHAR";
        case TokenType::KW_CHARACTER: return "KW_CHARACTER";
        case TokenType::KW_BYTE: return "KW_BYTE";
        case TokenType::KW_WORD: return "KW_WORD";
        case TokenType::KW_DWORD: return "KW_DWORD";
        case TokenType::KW_NONE: return "KW_NONE";
        case TokenType::KW_OBJECT: return "KW_OBJECT";
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
        case TokenType::OP_QUESTION: return "OP_QUESTION";
        case TokenType::OP_COLON: return "OP_COLON";
        case TokenType::OP_ASSIGN: return "OP_ASSIGN";
        case TokenType::OP_QUESTION_EQ: return "OP_QUESTION_EQ";
        case TokenType::OP_EQ_QUESTION: return "OP_EQ_QUESTION";
        default: return "UNKNOWN";
    }
}

} // namespace collie