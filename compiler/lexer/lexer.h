/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#pragma once
#ifndef COLLIE_LEXER_H
#define COLLIE_LEXER_H

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <stdexcept>
#include "token.h"

namespace collie {

// 添加词法分析器异常类
class LexError : public std::runtime_error {
public:
    LexError(const std::string& message, size_t line = 0, size_t column = 0)
        : std::runtime_error(message)
        , line_(line)
        , column_(column) {}

    size_t line() const { return line_; }
    size_t column() const { return column_; }

private:
    size_t line_;
    size_t column_;
};

class Lexer {
public:
    // 构造函数，接收源代码字符串
    explicit Lexer(std::string_view source);

    // 获取下一个 token
    Token next_token();

    // 预览下一个 token，但不移动指针
    Token peek_token();

    // 获取所有 tokens
    std::vector<Token> tokenize();

    // 获取当前行号和列号
    size_t current_line() const { return line_; }
    size_t current_column() const { return column_; }

private:
    // 源代码相关
    std::string source_;          // 源代码
    size_t position_;            // 当前位置
    size_t line_;               // 当前行号
    size_t column_;             // 当前列号

    // 辅助方法
    char peek() const;           // 预览当前字符
    char peek_next() const;      // 预览下一个字符
    char advance();              // 移动到下一个字符并返回当前字符
    bool is_at_end() const;      // 是否到达源码末尾

    // Token 解析方法
    Token scan_token();          // 扫描下一个 token
    Token scan_identifier();     // 扫描标识符
    Token scan_number();         // 扫描数字
    Token scan_string();         // 扫描字符串
    Token scan_character();      // 扫描字符

    // 字符判断辅助方法
    bool match(char expected);   // 如果当前字符匹配预期字符，则移动指针
    bool is_digit(char c) const;        // 是否是数字
    bool is_alpha(char c) const;        // 是否是字母
    bool is_alphanumeric(char c) const; // 是否是字母或数字

    // 错误处理
    Token make_error_token(const std::string& message);

    // 跳过空白字符和注释
    void skip_whitespace();
    void skip_line_comment();
    void skip_block_comment();

    // UTF-16相关方法
    char16_t peek_utf16() const;
    char16_t peek_next_utf16() const;
    char16_t advance_utf16();
    bool is_surrogate_pair(char16_t c) const;
    char32_t combine_surrogates(char16_t high, char16_t low) const;
    Token scan_utf16_character();

    // 用于处理UTF-16的内部状态
    std::u16string source_utf16_;
    size_t utf16_position_;
};

} // namespace collie

#endif // COLLIE_LEXER_H