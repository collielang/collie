/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2026-07-25
 * @Description: 解释器端到端测试：源码 -> 词法 -> 语法 -> 语义 -> 解释执行
 */
#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../semantic/semantic_analyzer.h"
#include "../interpreter/interpreter.h"

namespace {

// 将源码完整跑通编译流水线并解释执行，返回程序的标准输出内容。
// 若语义分析报错，则以断言失败并附带首条错误信息。
std::string run_source(const std::string& source) {
    collie::Lexer lexer(source);
    std::vector<collie::Token> tokens = lexer.tokenize();

    collie::Parser parser(tokens);
    std::vector<std::unique_ptr<collie::Stmt>> stmts = parser.parse_program();

    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    if (analyzer.has_errors()) {
        const auto& errors = analyzer.get_errors();
        ADD_FAILURE() << "Unexpected semantic error: "
                      << (errors.empty() ? "<none>" : errors.front().what());
        return "";
    }

    std::ostringstream out;
    collie::Interpreter interpreter(out);
    interpreter.interpret(stmts);
    return out.str();
}

}  // namespace

// 里程碑基线：跑通 Hello, World!
TEST(InterpreterEndToEnd, HelloWorld) {
    EXPECT_EQ(run_source(R"(print("Hello, World!");)"), "Hello, World!\n");
}

TEST(InterpreterEndToEnd, StringEscapes) {
    // 词法器已解码 \n \t \\ \"，解释器原样输出
    EXPECT_EQ(run_source(R"(print("a\tb\\c\"d");)"), "a\tb\\c\"d\n");
}

TEST(InterpreterEndToEnd, ArithmeticPrecedence) {
    EXPECT_EQ(run_source("number a = 1 + 2 * 3; print(a);"), "7\n");
}

TEST(InterpreterEndToEnd, StringConcatenation) {
    EXPECT_EQ(run_source(R"(string s = "foo" + "bar"; print(s);)"), "foobar\n");
}

TEST(InterpreterEndToEnd, VariableReassignment) {
    EXPECT_EQ(run_source("number a = 1; a = a + 4; print(a);"), "5\n");
}

TEST(InterpreterEndToEnd, PrintMultipleArguments) {
    EXPECT_EQ(run_source(R"(print("x", 1, true);)"), "x 1 true\n");
}

TEST(InterpreterEndToEnd, IfElseBranch) {
    EXPECT_EQ(run_source(
                  "number x = 7; if (x > 5) { print(\"big\"); } else { print(\"small\"); }"),
              "big\n");
}

TEST(InterpreterEndToEnd, WhileLoopSum) {
    EXPECT_EQ(run_source(
                  "number i = 0; number sum = 0; while (i < 5) { sum = sum + i; i = i + 1; } print(sum);"),
              "10\n");
}

TEST(InterpreterEndToEnd, ForLoopSum) {
    EXPECT_EQ(run_source(
                  "number total = 0; for (number k = 0; k < 3; k = k + 1) { total = total + k; } print(total);"),
              "3\n");
}

TEST(InterpreterEndToEnd, BreakStopsLoop) {
    EXPECT_EQ(run_source(
                  "number i = 0; while (i < 10) { if (i == 3) { break; } i = i + 1; } print(i);"),
              "3\n");
}

TEST(InterpreterEndToEnd, LogicalShortCircuit) {
    EXPECT_EQ(run_source("bool b = true && false; print(b);"), "false\n");
}
