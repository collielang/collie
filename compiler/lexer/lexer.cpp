/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 词法分析器实现，包含 UTF-8/UTF-16 编码支持
 */
#include "lexer.h"
#include <stdexcept>
#include <codecvt>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace collie {

Lexer::Lexer(std::string_view source, Encoding encoding)
    : source_(source), position_(0), line_(1), column_(1), encoding_(encoding) {
    if (encoding == Encoding::UTF16) {
        try {
            std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
            source_utf16_ = converter.from_bytes(std::string(source));
            utf16_position_ = 0;
        } catch (const std::exception&) {
            throw LexError("Invalid UTF-16 sequence", line_, column_);
        }
    }
}

char32_t Lexer::nextUtf8Char() {
    if (position_ >= source_.length()) {
        return 0;
    }

    unsigned char c = static_cast<unsigned char>(source_[position_]);

    // 检查无效的 UTF-8 序列
    if ((c & 0x80) != 0 && (c & 0xC0) != 0xC0) {
        throw LexError("Invalid UTF-8 sequence", line_, column_);
    }

    char32_t codepoint = 0;
    size_t bytes = 0;

    // 确定 UTF-8 字符的字节数
    if ((c & 0x80) == 0) {          // 0xxxxxxx (ASCII)
        bytes = 1;
        codepoint = c;
    } else if ((c & 0xE0) == 0xC0) { // 110xxxxx 10xxxxxx
        bytes = 2;
        codepoint = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) { // 1110xxxx 10xxxxxx 10xxxxxx
        bytes = 3;
        codepoint = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        bytes = 4;
        codepoint = c & 0x07;
    } else {
        throw LexError("Invalid UTF-8 sequence", line_, column_);
    }

    // 检查是否有足够的字节
    if (position_ + bytes > source_.length()) {
        throw LexError("Incomplete UTF-8 sequence", line_, column_);
    }

    // 读取后续字节
    for (size_t i = 1; i < bytes; ++i) {
        c = static_cast<unsigned char>(source_[position_ + i]);
        if ((c & 0xC0) != 0x80) { // 检查是否是合法的后续字节 (10xxxxxx)
            throw LexError("Invalid UTF-8 continuation byte", line_, column_);
        }
        codepoint = (codepoint << 6) | (c & 0x3F);
    }

    // 更新位置和列号
    position_ += bytes;
    column_++;

    return codepoint;
}

bool Lexer::isUtf8Start(char c) const {
    return (c & 0xC0) != 0x80;  // 不是 UTF-8 后续字节
}

bool Lexer::isIdentifierChar(char32_t c) const {
    // 基本的 ASCII 字母数字
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_') {
        return true;
    }

    // 中文字符范围 (CJK Unified Ideographs)
    if (c >= 0x4E00 && c <= 0x9FFF) {
        return true;
    }

    // CJK 扩展
    if (c >= 0x3400 && c <= 0x4DBF) {
        return true;
    }

    return false;
}

Token Lexer::scan_identifier() {
    size_t start_pos = position_;
    size_t start_col = column_;
    std::string identifier;

    if (encoding_ == Encoding::UTF8) {
        // UTF-8 模式
        bool first_char = true;
        while (!is_at_end()) {
            size_t current_pos = position_;
            try {
                char32_t c = nextUtf8Char();
                if (first_char && !is_alpha(c) && c != '_' && c < 0x4E00) {
                    position_ = current_pos;
                    break;
                }
                if (!first_char && !isIdentifierChar(c)) {
                    position_ = current_pos;
                    break;
                }
                identifier.append(source_.substr(current_pos, position_ - current_pos));
                first_char = false;
            } catch (const LexError&) {
                position_ = current_pos;
                break;
            }
        }
    } else {
        // 原有的 ASCII/UTF-16 处理逻辑
        while (!is_at_end() && (is_alphanumeric(peek()) || peek() == '_')) {
            advance();
        }
        identifier = std::string(source_.substr(start_pos, position_ - start_pos));
    }

    // 检查是否是关键字
    TokenType type = get_identifier_type(identifier);
    if (type == TokenType::IDENTIFIER) {
        return Token(type, identifier, line_, start_col);
    }
    return Token(type, identifier, line_, start_col);
}

Token Lexer::next_token() {
    while (!is_at_end()) {
        switch (current_state_) {
            case State::START:
                handle_start_state();
                break;
            case State::IN_NUMBER:
                handle_number_state();
                break;
            case State::IN_IDENTIFIER:
                handle_identifier_state();
                break;
            // ... 其他状态处理
        }
    }
    return make_token();
}

void Lexer::handle_start_state() {
    char c = peek();
    if (is_digit(c)) {
        current_state_ = State::IN_NUMBER;
    } else if (is_alpha(c)) {
        current_state_ = State::IN_IDENTIFIER;
    } else if (c == '"') {
        if (match('"') && match('"')) {
            current_state_ = State::IN_MULTILINE_STRING;
        } else {
            current_state_ = State::IN_STRING;
        }
    }
    // ... 其他状态转换
}

Token Lexer::peek_token() {
    // 保存当前状态
    size_t old_pos = position_;
    size_t old_line = line_;
    size_t old_column = column_;

    // 获取下一个 token
    Token token = next_token();

    // 恢复状态
    position_ = old_pos;
    line_ = old_line;
    column_ = old_column;

    return token;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = next_token();
        tokens.push_back(token);
        if (token.is_eof()) break;
    }
    return tokens;
}

// 私有辅助方法实现
char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[position_];
}

char Lexer::peek_next() const {
    if (position_ + 1 >= source_.length()) return '\0';
    return source_[position_ + 1];
}

char Lexer::advance() {
    char c = peek();
    position_++;
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::is_at_end() const {
    return position_ >= source_.length();
}

bool Lexer::match(char expected) {
    if (is_at_end() || peek() != expected) return false;
    advance();
    return true;
}

bool Lexer::is_digit(char c) const {
    return c >= '0' && c <= '9';
}

bool Lexer::is_alpha(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool Lexer::is_alphanumeric(char c) const {
    return is_digit(c) || is_alpha(c);
}

Token Lexer::make_error_token(const std::string& message) {
    return Token(TokenType::INVALID, message, line_, column_);
}

void Lexer::skip_whitespace() {
    while (!is_at_end()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
                advance();
                break;
            case '\n':
                advance();
                break;
            case '/':  // 处理注释
                if (peek_next() == '/') {
                    advance(); // 消费第一个 '/'
                    advance(); // 消费第二个 '/'
                    skip_line_comment();
                } else if (peek_next() == '*') {
                    advance(); // 消费 '/'
                    advance(); // 消费 '*'
                    skip_block_comment();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

void Lexer::skip_line_comment() {
    while (!is_at_end() && peek() != '\n') {
        advance();
    }
}

void Lexer::skip_block_comment() {
    int nesting = 1;
    while (!is_at_end() && nesting > 0) {
        if (peek() == '/' && peek_next() == '*') {
            advance(); advance();
            nesting++;
        } else if (peek() == '*' && peek_next() == '/') {
            advance(); advance();
            nesting--;
        } else {
            advance();
        }
    }
}

Token Lexer::scan_number() {
    size_t start_pos = position_;
    size_t start_col = column_;
    bool has_dot = false;

    // 整数部分
    while (!is_at_end() && is_digit(peek())) {
        advance();
    }

    // 小数部分
    if (peek() == '.' && is_digit(peek_next())) {
        has_dot = true;
        advance(); // 消费 '.'

        while (!is_at_end() && is_digit(peek())) {
            advance();
        }
    }

    // 科学计数法部分
    if (peek() == 'e' || peek() == 'E') {
        char next = peek_next();
        if (is_digit(next) || next == '+' || next == '-') {
            advance(); // 消费 'e' 或 'E'
            if (next == '+' || next == '-') advance();

            if (!is_digit(peek())) {
                return make_error_token("Invalid scientific notation");
            }

            while (!is_at_end() && is_digit(peek())) {
                advance();
            }
        }
    }

    std::string_view number = std::string_view(source_).substr(start_pos, position_ - start_pos);
    return Token(TokenType::LITERAL_NUMBER, number, line_, start_col);
}

Token Lexer::scan_string() {
    size_t start_pos = position_;
    size_t start_col = column_;

    advance(); // 消费开始的引号

    // 检查是否是多行字符串
    bool is_multiline = false;
    if (peek() == '"' && peek_next() == '"') {
        advance(); // 消费第二个引号
        advance(); // 消费第三个引号
        is_multiline = true;

        // 如果多行字符串后面紧跟着换行，跳过这个换行
        if (peek() == '\n') {
            advance();
        }
    }

    std::string value;
    size_t base_indent = 0;  // 基准缩进级别
    bool first_line = true;

    if (is_multiline) {
        std::string current_line;

        while (!is_at_end()) {
            // 检查结束标记
            if (peek() == '"' && peek_next() == '"' &&
                position_ + 2 < source_.length() && source_[position_ + 2] == '"') {
                if (!current_line.empty()) {
                    value += current_line;
                }
                advance(); // 消费第一个引号
                advance(); // 消费第二个引号
                advance(); // 消费第三个引号
                break;
            }

            char c = peek();
            if (c == '\n') {
                if (!first_line || !current_line.empty()) {
                    value += current_line + '\n';
                }
                advance();
                current_line.clear();

                // 处理下一行的缩进
                size_t current_indent = 0;
                while (!is_at_end() && (peek() == ' ' || peek() == '\t')) {
                    current_indent++;
                    advance();
                }

                // 如果是第一个非空行，记录基准缩进
                if (first_line && peek() != '\n' && peek() != '"') {
                    base_indent = current_indent;
                    first_line = false;
                } else if (!first_line && peek() != '\n' && peek() != '"') {
                    // 检查后续行的缩进是否正确
                    if (current_indent < base_indent) {
                        return make_error_token("Invalid indentation in multiline string");
                    }
                    // 保留额外的缩进
                    current_line = std::string(current_indent - base_indent, ' ');
                }
            } else {
                current_line += advance();
            }
        }

        if (is_at_end()) {
            return make_error_token("Unterminated multiline string");
        }
    } else {
        // 单行字符串处理逻辑
        while (!is_at_end() && peek() != '"') {
            if (peek() == '\\') {
                advance();
                switch (peek()) {
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    default: return make_error_token("Invalid escape sequence");
                }
                advance();
            } else {
                value += advance();
            }
        }

        if (is_at_end()) {
            return make_error_token("Unterminated string");
        }

        advance(); // 消费结束的引号
    }

    return Token(TokenType::LITERAL_STRING, value, line_, start_col);
}

Token Lexer::scan_character() {
    // 检查是否是UTF-16字符
    char16_t next = peek_utf16();
    if (next >= 0x80) {
        return scan_utf16_character();
    }

    size_t start_col = column_;
    advance(); // 消费开始的单引号

    if (is_at_end()) {
        return make_error_token("Unterminated character literal");
    }

    char value;
    if (peek() == '\\') {
        advance();
        switch (peek()) {
            case '\'': value = '\''; break;
            case '\\': value = '\\'; break;
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            default: return make_error_token("Invalid escape sequence");
        }
        advance();
    } else {
        value = advance();
    }

    if (peek() != '\'') {
        return make_error_token("Unterminated character literal");
    }

    advance(); // 消费结束的单引号

    return Token(TokenType::LITERAL_CHAR, std::string(1, value), line_, start_col);
}

char16_t Lexer::peek_utf16() const {
    if (utf16_position_ >= source_utf16_.length()) return 0;
    return source_utf16_[utf16_position_];
}

char16_t Lexer::peek_next_utf16() const {
    if (utf16_position_ + 1 >= source_utf16_.length()) return 0;
    return source_utf16_[utf16_position_ + 1];
}

char16_t Lexer::advance_utf16() {
    char16_t c = peek_utf16();
    utf16_position_++;

    // 更新行号和列号
    if (c == u'\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }

    return c;
}

bool Lexer::is_surrogate_pair(char16_t c) const {
    return c >= 0xD800 && c <= 0xDBFF;
}

char32_t Lexer::combine_surrogates(char16_t high, char16_t low) const {
    return (((char32_t)(high - 0xD800) << 10) | (low - 0xDC00)) + 0x10000;
}

Token Lexer::scan_utf16_character() {
    size_t start_col = column_;
    advance(); // 消费开始的单引号

    if (is_at_end()) {
        return make_error_token("Unterminated character literal");
    }

    std::u16string value;
    char16_t c = peek_utf16();

    if (c == u'\\') {
        advance_utf16();
        switch (peek_utf16()) {
            case u'\'': value = u'\''; break;
            case u'\\': value = u'\\'; break;
            case u'n': value = u'\n'; break;
            case u't': value = u'\t'; break;
            case u'r': value = u'\r'; break;
            default: return make_error_token("Invalid escape sequence");
        }
        advance_utf16();
    } else {
        if (is_surrogate_pair(c)) {
            char16_t high = advance_utf16();
            char16_t low = peek_utf16();
            if (low >= 0xDC00 && low <= 0xDFFF) {
                value.push_back(high);
                value.push_back(advance_utf16());
            } else {
                return make_error_token("Invalid surrogate pair");
            }
        } else {
            value.push_back(advance_utf16());
        }
    }

    if (peek_utf16() != u'\'') {
        return make_error_token("Unterminated character literal");
    }

    advance_utf16(); // 消费结束的单引号

    // 转换回 UTF-8 用于存储
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    std::string utf8_value;
    try {
        utf8_value = converter.to_bytes(value);
    } catch (const std::exception&) {
        return make_error_token("Invalid UTF-16 sequence");
    }

    return Token(TokenType::LITERAL_CHARACTER, utf8_value, line_, start_col);
}

std::string utf16_to_utf8(const std::u16string& utf16str) {
#ifdef _WIN32
    const wchar_t* wstr = reinterpret_cast<const wchar_t*>(utf16str.c_str());
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, (int)utf16str.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, (int)utf16str.size(), &utf8str[0], size_needed, nullptr, nullptr);
    return utf8str;
#else
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
    return converter.to_bytes(utf16str);
#endif
}

/**
 * 数字字面量状态处理
 * 解析整数和浮点数字面量
 * @return 数字类型的 token
 */
void Lexer::handle_number_state() {
    size_t start_pos = position_;
    size_t start_col = column_;
    std::string number;
    bool has_dot = false;

    while (!is_at_end()) {
        if (peek() == '.' && !has_dot) {
            has_dot = true;
            advance();
        } else if (!is_digit(peek())) {
            break;
        } else {
            advance();
        }
    }

    number = source_.substr(start_pos, position_ - start_pos);
    current_state_ = State::START;
    return Token(TokenType::LITERAL_NUMBER, number, line_, start_col);
}

/**
 * 标识符状态处理
 * 解析标识符和关键字,支持 UTF-8 编码的中文标识符
 * @return 标识符或关键字类型的 token
 */
void Lexer::handle_identifier_state() {
    size_t start_pos = position_;
    size_t start_col = column_;
    std::string identifier;

    if (encoding_ == Encoding::UTF8) {
        bool first_char = true;
        while (!is_at_end()) {
            size_t current_pos = position_;
            try {
                char32_t c = nextUtf8Char();
                if (first_char && !is_alpha(c) && c != '_' && c < 0x4E00) {
                    position_ = current_pos;
                    break;
                }
                if (!first_char && !isIdentifierChar(c)) {
                    position_ = current_pos;
                    break;
                }
                identifier.append(source_.substr(current_pos, position_ - current_pos));
                first_char = false;
            } catch (const LexError&) {
                position_ = current_pos;
                break;
            }
        }
    } else {
        while (!is_at_end() && (is_alphanumeric(peek()) || peek() == '_')) {
            advance();
        }
        identifier = source_.substr(start_pos, position_ - start_pos);
    }

    current_state_ = State::START;
    TokenType type = get_identifier_type(identifier);
    return Token(type, identifier, line_, start_col);
}

/**
 * 字符串字面量状态处理
 * 解析字符串字面量,支持转义序列
 * @return 字符串类型的 token
 */
void Lexer::handle_string_state() {
    size_t start_col = column_;
    advance(); // 消费开始的引号
    std::string value;

    while (!is_at_end() && peek() != '"') {
        if (peek() == '\\') {
            advance();
            switch (peek()) {
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                default:
                    current_state_ = State::START;
                    return make_error_token("Invalid escape sequence");
            }
            advance();
        } else {
            value += advance();
        }
    }

    if (is_at_end()) {
        current_state_ = State::START;
        return make_error_token("Unterminated string");
    }

    advance(); // 消费结束的引号
    current_state_ = State::START;
    return Token(TokenType::LITERAL_STRING, value, line_, start_col);
}

/**
 * 多行字符串状态处理
 * 解析三引号包围的多行字符串,保持缩进对齐
 * @return 字符串类型的 token
 */
void Lexer::handle_multiline_string_state() {
    size_t start_col = column_;
    std::string value;
    size_t indent = column_ - 1;

    while (!is_at_end()) {
        if (peek() == '"' && peek_next() == '"' && source_[position_ + 2] == '"') {
            // 检查结束引号的缩进
            size_t current_indent = 0;
            size_t pos = position_;
            while (pos > 0 && source_[pos - 1] == ' ') {
                current_indent++;
                pos--;
            }
            if (current_indent != indent) {
                current_state_ = State::START;
                return make_error_token("Misaligned closing quotes in multiline string");
            }
            position_ += 3;
            column_ += 3;
            break;
        }
        value += advance();
    }

    if (is_at_end()) {
        current_state_ = State::START;
        return make_error_token("Unterminated multiline string");
    }

    current_state_ = State::START;
    return Token(TokenType::LITERAL_STRING, value, line_, start_col);
}

/**
 * 运算符状态处理
 * 解析单字符和多字符运算符
 * @return 运算符类型的 token
 */
void Lexer::handle_operator_state() {
    size_t start_col = column_;
    char c = advance();

    switch (c) {
        case '+': return Token(TokenType::OP_PLUS, "+", line_, start_col);
        case '-': return Token(TokenType::OP_MINUS, "-", line_, start_col);
        case '*': return Token(TokenType::OP_MULTIPLY, "*", line_, start_col);
        case '/':
            if (match('/')) {
                current_state_ = State::IN_COMMENT;
                return next_token();
            }
            return Token(TokenType::OP_DIVIDE, "/", line_, start_col);
        // ... 其他运算符处理
    }

    current_state_ = State::START;
    return make_error_token("Invalid operator");
}

/**
 * 注释状态处理
 * 跳过单行注释
 * @return 下一个有效的 token
 */
void Lexer::handle_comment_state() {
    while (!is_at_end() && peek() != '\n') {
        advance();
    }
    current_state_ = State::START;
    return next_token();
}

} // namespace collie