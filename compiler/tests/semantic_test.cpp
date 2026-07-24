/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2025-01-05
 * @Description: 语义层 break/continue 循环上下文检查测试
 */
#include <gtest/gtest.h>
#include "../semantic/semantic_analyzer.h"
#include "../parser/parser.h"
#include "../lexer/lexer.h"
#include "test_utils.h"

using namespace collie;

// break/continue 是否位于循环内属于上下文约束，由语义层负责检查（见 PROGRESS.md D9）：
// 语法层（parser）只接受其语法，循环外使用应在语义分析阶段报错。

// 循环内使用 break：合法，不应产生语义错误
TEST(SemanticBreakContinueTest, BreakInsideLoopIsValid) {
    auto [ast, tokens] = test::parse_and_get_tokens(R"(
        while (true) {
            break;
        }
    )");

    SemanticAnalyzer analyzer;
    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_FALSE(analyzer.has_errors());
}

// 循环内使用 continue：合法，不应产生语义错误
TEST(SemanticBreakContinueTest, ContinueInsideLoopIsValid) {
    auto [ast, tokens] = test::parse_and_get_tokens(R"(
        while (true) {
            continue;
        }
    )");

    SemanticAnalyzer analyzer;
    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_FALSE(analyzer.has_errors());
}

// 循环外使用 break：应报语义错误
TEST(SemanticBreakContinueTest, BreakOutsideLoopIsError) {
    auto [ast, tokens] = test::parse_and_get_tokens("break;");

    SemanticAnalyzer analyzer;
    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
}

// 循环外使用 continue：应报语义错误
TEST(SemanticBreakContinueTest, ContinueOutsideLoopIsError) {
    auto [ast, tokens] = test::parse_and_get_tokens("continue;");

    SemanticAnalyzer analyzer;
    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
}
