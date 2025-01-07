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

// 错误恢复测试
TEST(SemanticErrorTest, ErrorRecovery) {
    SemanticAnalyzer analyzer;

    // 多个错误的代码
    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误1：类型不匹配
        string x = 42;

        // 错误2：未定义的变量
        y = 10;

        // 正确的代码，应该能继续分析
        number z = 100;

        // 错误3：重复定义
        number z = 200;

        // 错误4：常量重新赋值
        const number PI = 3.14;
        PI = 3.14159;
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    // 验证错误收集
    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 4);  // 应该有4个错误

    // 验证错误信息
    EXPECT_TRUE(errors[0].what() != nullptr);  // 类型不匹配错误
    EXPECT_TRUE(errors[1].what() != nullptr);  // 未定义变量错误
    EXPECT_TRUE(errors[2].what() != nullptr);  // 重复定义错误
    EXPECT_TRUE(errors[3].what() != nullptr);  // 常量重新赋值错误
}

// 函数相关错误测试
TEST(SemanticErrorTest, FunctionErrors) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误1：函数重复定义
        number add(number x, number y) {
            return x + y;
        }
        number add(number x, number y) {
            return x + y;
        }

        // 错误2：缺少返回值
        number getValue() {
            number x = 42;
            // 没有 return 语句
        }

        // 错误3：返回值类型不匹配
        number getString() {
            return "hello";
        }

        // 错误4：参数类型不匹配的函数调用
        void process(number x) {
            return;
        }
        process("42");
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    // 验证错误收集
    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 4);  // 应该有4个错误
}

// 作用域和初始化错误测试
TEST(SemanticErrorTest, ScopeAndInitializationErrors) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误1：使用未初始化的变量
        number x;
        number y = x + 1;

        // 错误2：访问内层作用域的变量
        {
            number inner = 42;
        }
        number z = inner;  // inner 不在作用域内

        // 错误3：条件分支中的变量可能未初始化
        number a;
        if (true) {
            a = 1;
        }
        number b = a;  // a 可能未初始化

        // 错误4：循环外使用 break
        break;
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    // 验证错误收集
    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 4);  // 应该有4个错误
}

// 类型转换错误测试
TEST(SemanticErrorTest, TypeConversionErrors) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误1：不允许的类型转换
        number n = "42";

        // 错误2：位运算类型错误
        byte b = 1.5 & 0xFF;

        // 错误3：比较运算类型错误
        bool c = true < 42;

        // 错误4：字符串连接类型错误
        string s = "hello" + true && false;
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    // 验证错误收集
    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 4);  // 应该有4个错误
}

// 错误位置信息测试
TEST(SemanticErrorTest, ErrorLocation) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number x = 1;
        string y = x;  // 第2行，类型错误
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 1);

    // 验证错误位置信息
    EXPECT_EQ(errors[0].line(), 2);
    EXPECT_GT(errors[0].column(), 0);
}

// 错误恢复和继续分析测试
TEST(SemanticErrorTest, ContinueAfterError) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误代码
        string x = 42;

        // 正确的代码，应该能继续分析
        number y = 100;
        number z = y + 200;

        // 另一个错误
        bool b = y;
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 2);  // 应该有2个错误

    // 验证错误恢复后的分析是否正确
    // 这里我们只能验证错误数量，因为中间的正确代码应该被正确分析
    EXPECT_TRUE(errors[0].what() != nullptr);  // 第一个错误
    EXPECT_TRUE(errors[1].what() != nullptr);  // 第二个错误
}

// 添加新的测试：错误消息格式测试
TEST(SemanticErrorTest, ErrorMessageFormat) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number x = "hello";  // 类型错误
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 1);

    // 验证错误消息格式
    std::string error_message = errors[0].what();
    EXPECT_TRUE(error_message.find("Line") != std::string::npos);
    EXPECT_TRUE(error_message.find("Column") != std::string::npos);
    EXPECT_TRUE(error_message.find("type") != std::string::npos);
}

// 数组错误测试
TEST(SemanticErrorTest, ArrayErrors) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 错误1：数组越界检查
        number[] arr = [1, 2, 3];
        number x = arr[3];  // 索引超出范围

        // 错误2：数组类型不匹配
        string[] strs = ["hello"];
        number y = strs[0];  // 类型不匹配

        // 错误3：多维数组类型错误
        number[][] matrix = [[1, 2], ["3", 4]];  // 内部数组类型不一致

        // 错误4：数组操作类型错误
        number[] nums = [1, 2];
        nums = nums + [3, 4];  // 不支持的操作
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 4);  // 应该有4个错误
}