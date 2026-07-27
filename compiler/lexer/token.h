/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2025-01-05
 */
#ifndef COLLIE_TOKEN_H
#define COLLIE_TOKEN_H

#include <string>
#include <string_view>
#include "utf_convert.h"

namespace collie {

// Token 类型枚举
enum class TokenType {
    // 特殊 token
    END_OF_FILE,    // 文件结束
    INVALID,        // 无效 token
    TOKEN_ERROR,    // 错误 token

    // 字面量
    LITERAL_NUMBER,     // 数字字面量
    LITERAL_STRING,     // 字符串字面量
    LITERAL_CHAR,       // 字符字面量
    LITERAL_CHARACTER,  // UTF-16 字符字面量
    LITERAL_BOOL,       // 布尔字面量
    LITERAL_INTERPOLATED_STRING, // 插值字符串字面量 @"...{expr}..."（lexeme 为引号内原文，由 parser 拆段脱糖）

    // 标识符
    IDENTIFIER,     // 标识符

    // 注解
    ANNOTATION,     // @注解名（如 @override，lexeme 为注解名不含 '@'）

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
    KW_VAR,         // var
    KW_TUPLE,       // Tuple（文档拼写为大写，见 03-tuple.md）
    KW_ARRAY,       // array

    // 关键字 - 控制流
    KW_IF,          // if
    KW_ELSE,        // else
    KW_SWITCH,      // switch
    KW_DEFAULT,     // default
    KW_FOR,         // for
    KW_WHILE,       // while
    KW_DO,          // do
    KW_BREAK,       // break
    KW_CONTINUE,    // continue
    KW_RETURN,      // return

    // 关键字 - 其他
    KW_CLASS,       // class
    KW_CONST,       // const
    KW_PUBLIC,      // public
    KW_PRIVATE,     // private
    KW_NULL,        // null
    KW_TRUE,        // true
    KW_FALSE,       // false
    KW_UNSET,       // unset
    KW_VOID,        // void
    KW_FUNCTION,    // function
    KW_PROTECTED,   // protected
    KW_NEW,         // new（对象实例化）
    KW_THIS,        // this（类方法内引用当前实例）
    KW_EXTENDS,     // extends（类继承）
    KW_BASE,        // base（构造器委托父类构造器）

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
    OP_PLUS_ASSIGN,  // +=
    OP_MINUS_ASSIGN, // -=
    OP_MULTIPLY_ASSIGN, // *=
    OP_DIVIDE_ASSIGN,   // /=
    OP_MODULO_ASSIGN,   // %=
    OP_EQ_QUESTION, // ==?

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

    // Utility methods
    // 辅助方法
    bool is_eof() const { return type_ == TokenType::END_OF_FILE; }
    bool is_invalid() const { return type_ == TokenType::INVALID; }

    // 获取UTF-16字符串
    std::u16string lexeme_utf16() const {
        return utf8_to_utf16(lexeme_);
    }

private:
    TokenType type_;         // token 类型
    std::string lexeme_;     // token 的字面值
    size_t line_;            // token 所在行号
    size_t column_;          // token 所在列号
};

// Helper functions
TokenType get_identifier_type(std::string_view identifier);

/**
 * @brief 根据标识符获取对应的 token 类型
 * @param identifier 标识符字符串
 * @return 如果是关键字则返回对应的 TokenType，否则返回 TokenType::IDENTIFIER
 */
TokenType get_keyword_type(const std::string& identifier);

} // namespace collie

#endif // COLLIE_TOKEN_H
