/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2025-01-05
 */
#include <gtest/gtest.h>
#include <iostream>
#ifdef _WIN32
    #include <Windows.h>
    #include <fcntl.h>
    #include <io.h>
#endif

#include "../lexer/lexer.h"

using namespace collie;

// 辅助函数：打印token序列
void PrintTokens(const std::vector<Token>& tokens) {
    std::cout << "Tokens(" << tokens.size() << "):" << std::endl;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::cout << i << ": " << static_cast<int>(tokens[i].type())
                 << " '" << tokens[i].lexeme() << "'" << std::endl;
    }
}

// 基本token测试
TEST(LexerTest, BasicTokens) {
    std::string source = "number x = 42;";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize(); // [tokens] number, x, =, 42, ;
    PrintTokens(tokens);  // 添加这行来帮助调试
    ASSERT_EQ(tokens.size() - 1/* 减去 EOF token */, 5);

    EXPECT_EQ(tokens[0].type(), TokenType::KW_NUMBER);
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].type(), TokenType::OP_ASSIGN);
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[4].type(), TokenType::DELIMITER_SEMICOLON);
}

// 数字字面量形式：前导点小数、f 后缀、科学计数法（见设计文档 04-numeric.md）
TEST(LexerTest, NumberLiteralForms) {
    std::string source = ".5 2f 3.14 1e3 2.5f;";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize();
    PrintTokens(tokens);
    ASSERT_EQ(tokens.size() - 1/* 减去 EOF token */, 6);

    EXPECT_EQ(tokens[0].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[0].lexeme(), ".5");
    EXPECT_EQ(tokens[1].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[1].lexeme(), "2f");  // f 后缀计入 lexeme（t42：下游据此判定 decimal）
    EXPECT_EQ(tokens[2].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[2].lexeme(), "3.14");
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[3].lexeme(), "1e3");
    EXPECT_EQ(tokens[4].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[4].lexeme(), "2.5f");  // f 后缀计入 lexeme（t42）
    EXPECT_EQ(tokens[5].type(), TokenType::DELIMITER_SEMICOLON);
}

// f 后面跟标识符字符时不视为后缀（避免误吞 2fx 中的 f）
TEST(LexerTest, NumberFSuffixNotGreedy) {
    std::string source = "2fx";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size() - 1, 2);
    EXPECT_EQ(tokens[0].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[0].lexeme(), "2");
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].lexeme(), "fx");
}

// 特殊数值字面量 Infinity/NaN 归为数字字面量；大小写敏感（见 04-numeric.md）
TEST(LexerTest, InfinityNaNLiterals) {
    std::string source = "Infinity NaN infinity nan";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size() - 1, 4);
    EXPECT_EQ(tokens[0].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[0].lexeme(), "Infinity");
    EXPECT_EQ(tokens[1].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[1].lexeme(), "NaN");
    // 小写拼写不是特殊字面量，仍是普通标识符
    EXPECT_EQ(tokens[2].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[3].type(), TokenType::IDENTIFIER);
}

// 插值字符串 @"...{expr}..."：lexeme 保留引号内原文（见 03-character.md）
TEST(LexerTest, InterpolatedStringLiteral) {
    std::string source = "string s = @\"a{name}b\";";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size() - 1, 5);
    EXPECT_EQ(tokens[0].type(), TokenType::KW_STRING);
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].type(), TokenType::OP_ASSIGN);
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_INTERPOLATED_STRING);
    EXPECT_EQ(tokens[3].lexeme(), "a{name}b");
    EXPECT_EQ(tokens[4].type(), TokenType::DELIMITER_SEMICOLON);
}

// 字符串测试
TEST(LexerTest, StringLiterals) {
    std::string source = R"("Hello, world!" 'c')";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 3);  // "Hello, world!", 'c', EOF

    EXPECT_EQ(tokens[0].type(), TokenType::LITERAL_STRING);
    EXPECT_EQ(tokens[0].lexeme(), "Hello, world!");
    EXPECT_EQ(tokens[1].type(), TokenType::LITERAL_CHAR);
    EXPECT_EQ(tokens[1].lexeme(), "c");
}

// 注释测试
TEST(LexerTest, Comments) {
    std::string source = R"(
        // 这是单行注释
        number x = 1; /* 这是多行
        注释 */ number y = 2;
    )";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize(); // [tokens] number, x, =, 1, ;, number, y, =, 2, ;, EOF
    ASSERT_EQ(tokens.size(), 11);  // 10个token + EOF

    // 验证具体的 token
    EXPECT_EQ(tokens[0].type(), TokenType::KW_NUMBER);
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].type(), TokenType::OP_ASSIGN);
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[4].type(), TokenType::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[5].type(), TokenType::KW_NUMBER);
    EXPECT_EQ(tokens[6].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[7].type(), TokenType::OP_ASSIGN);
    EXPECT_EQ(tokens[8].type(), TokenType::LITERAL_NUMBER);
    EXPECT_EQ(tokens[9].type(), TokenType::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[10].type(), TokenType::END_OF_FILE);
}

// 错误处理测试
TEST(LexerTest, ErrorHandling) {
    std::string source = "\"未闭合的字符串";
    Lexer lexer(source, Encoding::UTF8);

    auto token = lexer.next_token();
    EXPECT_EQ(token.type(), TokenType::INVALID);
}

// 位置信息测试
TEST(LexerTest, LocationTracking) {
    std::string source = "number\nx = 42;";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize();
    EXPECT_EQ(tokens[0].line(), 1);
    EXPECT_EQ(tokens[1].line(), 2);
}

// UTF-16字符测试
TEST(LexerTest, UTF16Characters) {
    std::string source = "character c = '世';";
    Lexer lexer(source, Encoding::UTF16);

    auto tokens = lexer.tokenize(); // [tokens] character, c, =, '世', ;, EOF
    PrintTokens(tokens);  // 添加调试输出

    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type(), TokenType::KW_CHARACTER);
    EXPECT_EQ(tokens[1].type(), TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].type(), TokenType::OP_ASSIGN);
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_CHARACTER);
    EXPECT_EQ(tokens[4].type(), TokenType::DELIMITER_SEMICOLON);
    EXPECT_EQ(tokens[5].type(), TokenType::END_OF_FILE);

    // 测试代理对字符
    source = "character c = '𐍈';";  // 这是一个需要代理对的字符
    lexer = Lexer(source, Encoding::UTF16);

    tokens = lexer.tokenize(); // [tokens] character, c, =, '𐍈', ;, EOF
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type(), TokenType::KW_CHARACTER);  // 这里也要修改
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_CHARACTER);
}

TEST(LexerTest, UTF16Strings) {
    std::string source = R"("你好，世界！")";
    Lexer lexer(source, Encoding::UTF16);

    auto tokens = lexer.tokenize();
    ASSERT_EQ(tokens.size(), 2);  // "你好，世界！", EOF
    EXPECT_EQ(tokens[0].type(), TokenType::LITERAL_STRING);

    // 验证UTF-16字符串内容
    std::u16string expected = u"你好，世界！";
    EXPECT_EQ(tokens[0].lexeme_utf16(), expected);
}

// 多行字符串测试
TEST(LexerTest, MultilineStrings) {
    // 基本的多行字符串
    std::string source = R"(
        const text = """
            Hello,
            World!
            """;
    )";
    Lexer lexer(source, Encoding::UTF8);

    auto tokens = lexer.tokenize(); // [tokens] const, text, =, string_literal, ;
    ASSERT_EQ(tokens.size() - 1, 5);
    EXPECT_EQ(tokens[3].type(), TokenType::LITERAL_STRING);
    EXPECT_EQ(tokens[3].lexeme(), "Hello,\nWorld!\n");

    // 测试缩进对齐
    source = R"(
        const text = """
            Hello,
                World!
            """;
    )";
    lexer = Lexer(source, Encoding::UTF8);
    tokens = lexer.tokenize();
    EXPECT_EQ(tokens[3].lexeme(), "Hello,\n    World!\n");

    // 测试错误的缩进
    source = R"(
        const text = """
            Hello,
        World!
            """;
    )";
    lexer = Lexer(source, Encoding::UTF8);
    auto token = lexer.next_token(); // const
    token = lexer.next_token(); // text
    token = lexer.next_token(); // =
    token = lexer.next_token(); // 应该返回错误
    EXPECT_EQ(token.type(), TokenType::INVALID);

    // 测试未闭合的多行字符串
    source = R"(
        const text = """
            Hello,
            World!
    )";
    lexer = Lexer(source, Encoding::UTF8);
    token = lexer.next_token(); // const
    token = lexer.next_token(); // text
    token = lexer.next_token(); // =
    token = lexer.next_token(); // 应该返回错误
    EXPECT_EQ(token.type(), TokenType::INVALID);
}

TEST(LexerTest, Utf8Characters) {
    std::string source = "\"你好，世界！\"";
    Lexer lexer(source, Encoding::UTF8);

    Token token = lexer.next_token();
    EXPECT_EQ(token.type(), TokenType::LITERAL_STRING);
    EXPECT_EQ(token.lexeme(), "你好，世界！");
}

TEST(LexerTest, InvalidUtf8) {
    std::string source = "\"\xFF\xFF\"";  // 无效的 UTF-8 序列
    Lexer lexer(source, Encoding::UTF8);

    EXPECT_THROW({
        lexer.next_token();
    }, LexError);
}

TEST(LexerTest, ChineseIdentifiers) {
    std::string source = "变量名 = 42;";
    Lexer lexer(source, Encoding::UTF8);

    Token token = lexer.next_token();
    EXPECT_EQ(token.type(), TokenType::IDENTIFIER);
    EXPECT_EQ(token.lexeme(), "变量名");
}

#ifdef _WIN32
void SetupWindowsConsole() {
    // 设置控制台代码页为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // 启用控制台二进制模式
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    SetupWindowsConsole();
#elif defined(__linux__) || defined(__APPLE__)
    // Linux 和 macOS 默认支持 UTF-8，不需要特殊设置
    setlocale(LC_ALL, "");
#endif

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
