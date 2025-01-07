/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "token.h"
#include <unordered_map>

namespace collie {

// 关键字映射表
static const std::unordered_map<std::string_view, TokenType> keywords = {
    // 类型关键字
    {"object", TokenType::KW_OBJECT},
    {"none", TokenType::KW_NONE},
    {"char", TokenType::KW_CHAR},
    {"character", TokenType::KW_CHARACTER},
    {"string", TokenType::KW_STRING},
    {"number", TokenType::KW_NUMBER},
    {"integer", TokenType::KW_INTEGER},
    {"decimal", TokenType::KW_DECIMAL},
    {"bool", TokenType::KW_BOOL},
    {"tribool", TokenType::KW_TRIBOOL},
    {"bit", TokenType::KW_BIT},
    {"byte", TokenType::KW_BYTE},
    {"word", TokenType::KW_WORD},
    {"dword", TokenType::KW_DWORD},

    // 控制流关键字
    {"if", TokenType::KW_IF},
    {"else", TokenType::KW_ELSE},
    {"switch", TokenType::KW_SWITCH},
    {"for", TokenType::KW_FOR},
    {"while", TokenType::KW_WHILE},
    {"do", TokenType::KW_DO},
    {"break", TokenType::KW_BREAK},
    {"continue", TokenType::KW_CONTINUE},

    // 其他关键字
    {"class", TokenType::KW_CLASS},
    {"const", TokenType::KW_CONST},
    {"public", TokenType::KW_PUBLIC},
    {"private", TokenType::KW_PRIVATE},
    {"null", TokenType::KW_NULL},
    {"true", TokenType::KW_TRUE},
    {"false", TokenType::KW_FALSE},
    {"unset", TokenType::KW_UNSET},
    {"return", TokenType::KW_RETURN},
    {"void", TokenType::KW_VOID},
    {"function", TokenType::KW_FUNCTION},
    {"protected", TokenType::KW_PROTECTED}
};

TokenType get_identifier_type(std::string_view identifier) {
    auto it = keywords.find(identifier);
    return it != keywords.end() ? it->second : TokenType::IDENTIFIER;
}

} // namespace collie