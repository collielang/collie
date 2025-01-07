/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#ifndef COLLIE_TOKEN_H
#define COLLIE_TOKEN_H

#include <string>
#include <string_view>
#include <codecvt>
#include <locale>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace collie {

// Token 类型枚举
enum class TokenType {
    // 特殊 token
    END_OF_FILE,    // 文件结束
    INVALID,        // 无效 token

    // 字面量
    LITERAL_NUMBER,     // 数字字面量
    LITERAL_STRING,     // 字符串字面量
    LITERAL_CHAR,       // 字符字面量
    LITERAL_CHARACTER,  // UTF-16 字符字面量
    LITERAL_BOOL,       // 布尔字面量

    // 标识符
    IDENTIFIER,     // 标识符

    // 关键字 - 类型
    KW_OBJECT,      // object
    KW_NONE,        // none
    KW_CHAR,        // char
    KW_CHARACTER,   // character
    KW_STRING,      // string
    KW_NUMBER,      // number
    KW_INTEGER,     // integer
    KW_DECIMAL,     // decimal
    KW_BOOL,        // bool
    KW_TRIBOOL,     // tribool
    KW_BIT,         // bit
    KW_BYTE,        // byte
    KW_WORD,        // word
    KW_DWORD,       // dword

    // 关键字 - 控制流
    KW_IF,          // if
    KW_ELSE,        // else
    KW_SWITCH,      // switch
    KW_FOR,         // for
    KW_WHILE,       // while
    KW_DO,          // do
    KW_BREAK,       // break
    KW_CONTINUE,    // continue

    // 关键字 - 其他
    KW_CLASS,       // class
    KW_CONST,       // const
    KW_PUBLIC,      // public
    KW_PRIVATE,     // private
    KW_NULL,        // null
    KW_TRUE,        // true
    KW_FALSE,       // false
    KW_UNSET,       // unset
    KW_RETURN,      // return

    // 运算符 - 算术
    OP_PLUS,        // +
    OP_MINUS,       // -
    OP_MULTIPLY,    // *
    OP_DIVIDE,      // /
    OP_MODULO,      // %

    // 运算符 - 比较
    OP_EQUAL,       // ==
    OP_NOT_EQUAL,   // !=
    OP_GREATER,     // >
    OP_LESS,        // <
    OP_GREATER_EQ,  // >=
    OP_LESS_EQ,     // <=

    // 运算符 - 逻辑
    OP_AND,         // &&
    OP_OR,          // ||
    OP_NOT,         // !

    // 运算符 - 位运算
    OP_BIT_AND,     // &
    OP_BIT_OR,      // |
    OP_BIT_XOR,     // ^
    OP_BIT_NOT,     // ~
    OP_BIT_LSHIFT,  // <<
    OP_BIT_RSHIFT,  // >>

    // 运算符 - 特殊
    OP_QUESTION,    // ?
    OP_COLON,       // :
    OP_ASSIGN,      // =
    OP_QUESTION_EQ, // ?=
    OP_EQ_QUESTION, // =?

    // 分隔符
    DELIMITER_LPAREN,    // (
    DELIMITER_RPAREN,    // )
    DELIMITER_LBRACKET,  // [
    DELIMITER_RBRACKET,  // ]
    DELIMITER_LBRACE,    // {
    DELIMITER_RBRACE,    // }
    DELIMITER_COMMA,     // ,
    DELIMITER_SEMICOLON, // ;
    DELIMITER_DOT,       // .

    // 关键字
    KW_VOID,        // void
    KW_FUNCTION,    // function
    KW_PROTECTED,   // protected
};

// Token 类
class Token {
public:
    Token() : type_(TokenType::INVALID), lexeme_(""), line_(0), column_(0) {}

    Token(TokenType type, std::string_view lexeme, size_t line, size_t column)
        : type_(type), lexeme_(lexeme), line_(line), column_(column) {}

    // Getters
    TokenType type() const { return type_; }
    std::string_view lexeme() const { return lexeme_; }
    size_t line() const { return line_; }
    size_t column() const { return column_; }

    // 辅助方法
    bool is_eof() const { return type_ == TokenType::END_OF_FILE; }
    bool is_invalid() const { return type_ == TokenType::INVALID; }

    // 获取UTF-16字符串
    std::u16string lexeme_utf16() const {
#ifdef _WIN32
        // 先转换为宽字符
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, lexeme_.c_str(), (int)lexeme_.size(), nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, lexeme_.c_str(), (int)lexeme_.size(), &wstr[0], size_needed);

        // 再转换为 UTF-16
        return std::u16string(reinterpret_cast<const char16_t*>(wstr.c_str()), wstr.size());
#else
        // 非 Windows 平台暂时保持原样
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
        return converter.from_bytes(lexeme_);
#endif
    }

private:
    TokenType type_;         // token 类型
    std::string lexeme_;     // token 的字面值
    size_t line_;           // token 所在行号
    size_t column_;         // token 所在列号
};

TokenType get_identifier_type(std::string_view identifier);

/**
 * @brief 根据标识符获取对应的 token 类型
 * @param identifier 标识符字符串
 * @return 如果是关键字则返回对应的 TokenType，否则返回 TokenType::IDENTIFIER
 */
TokenType get_keyword_type(const std::string& identifier);

} // namespace collie

#endif // COLLIE_TOKEN_H