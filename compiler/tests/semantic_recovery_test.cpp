/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include <gtest/gtest.h>
#include "../semantic/semantic_analyzer.h"
#include "../parser/parser.h"
#include "../lexer/lexer.h"

using namespace collie;

// 辅助函数：解析源代码并返回AST和tokens
std::pair<std::vector<std::unique_ptr<Stmt>>, std::vector<Token>>
parse_and_get_tokens(const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return {parser.parse_program(), tokens};
}

// 测试在语句边界的错误恢复
TEST(SemanticRecoveryTest, StatementBoundaryRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误语句
        string x = 42;

        // 正确语句，应该能继续分析
        number y = 100;

        // 另一个错误语句
        bool z = y + "hello";

        // 正确语句，应该能继续分析
        number w = y + 50;
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 2);  // 应该只有两个错误
}

// 测试在块语句中的错误恢复
TEST(SemanticRecoveryTest, BlockRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        {
            // 错误语句
            string x = 42;

            // 正确语句
            number y = 100;

            {
                // 嵌套块中的错误
                bool z = y + "hello";

                // 正确语句
                number w = y + 50;
            }
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 2);
}

// 测试在函数定义中的错误恢复
TEST(SemanticRecoveryTest, FunctionRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number func1() {
            // 错误语句
            string x = 42;

            // 正确语句
            number y = 100;
            return y;
        }

        number func2() {
            // 错误语句
            bool z = "hello" + 42;

            // 正确语句
            number w = 50;
            return w;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 2);
}

// 测试在控制流语句中的错误恢复
TEST(SemanticRecoveryTest, ControlFlowRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number x = 1;

        if (x > 0) {
            // 错误语句
            string y = x;

            // 正确语句
            number z = x + 1;
        } else {
            // 错误语句
            bool w = x + "hello";

            // 正确语句
            number v = x - 1;
        }

        while (x < 10) {
            // 错误语句
            string a = x;

            // 正确语句
            x = x + 1;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);  // 应该有三个错误
}

// 测试在表达式中的错误恢复
TEST(SemanticRecoveryTest, ExpressionRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number x = 1;
        number y = (x + "hello") + (x + 2);  // 第一个表达式错误，第二个正确
        number z = (true + 42) + (x + 3);    // 第一个表达式错误，第二个正确
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 2);
}

// 测试在声明语句中的错误恢复
TEST(SemanticRecoveryTest, DeclarationRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误的变量声明
        string x = 42;

        // 正确的变量声明
        number y = 100;

        // 错误的函数声明
        number add(string x, bool y) {
            return x + y;  // 错误的返回语句
        }

        // 正确的函数声明
        number multiply(number x, number y) {
            return x * y;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_GE(errors.size(), 2);  // 至少应该有两个错误
}