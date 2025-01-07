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

// 测试复杂嵌套结构中的错误恢复
TEST(SemanticRecoveryTest, ComplexNestedRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number outer = 1;

        // 复杂嵌套结构
        if (outer > 0) {
            number x = 10;
            while (x > 0) {
                if (x % 2 == 0) {
                    // 错误1：类型不匹配
                    string y = x;

                    // 正确语句
                    number z = x - 1;
                } else {
                    // 错误2：未定义变量
                    number w = undefined_var;

                    // 正确语句
                    number v = x + 1;
                }
                x = x - 1;
            }
        } else {
            // 错误3：运算符类型错误
            bool result = "hello" + 42;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);  // 应该有三个错误
}

// 测试函数调用链中的错误恢复
TEST(SemanticRecoveryTest, FunctionCallChainRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number add(number x, number y) {
            return x + y;
        }

        string concat(string a, string b) {
            return a + b;
        }

        void process() {
            // 错误1：参数类型不匹配
            number result1 = add("hello", 42);

            // 正确调用
            number result2 = add(1, 2);

            // 错误2：参数数量不匹配
            string result3 = concat("hello");

            // 错误3：函数类型不匹配
            number result4 = concat("a", "b");

            // 正确调用
            string result5 = concat("hello", "world");
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);  // 应该有三个错误
}

// 测试复杂表达式中的错误恢复
TEST(SemanticRecoveryTest, ComplexExpressionRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number x = 1;
        number y = 2;
        string s = "hello";
        bool b = true;

        // 复杂表达式中的错误
        number result = (
            (x + "world") *           // 错误1：字符串不能参与乘法
            (y + (s > 42)) +          // 错误2：字符串不能与数字比较
            (b + 3) *                 // 错误3：布尔值不能参与乘法
            (x + y)                   // 正确的表达式
        );
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);  // 应该有三个错误
}

// 测试变量作用域和生命周期的错误恢复
TEST(SemanticRecoveryTest, ScopeLifetimeRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        {
            number x = 1;
            {
                // 错误1：重复声明
                number x = 2;

                {
                    // 错误2：访问超出作用域的变量
                    number y = outer_var;

                    // 正确的声明
                    number z = x + 1;
                }
                // 错误3：使用超出作用域的变量
                number w = z;
            }
            // 正确的访问
            number v = x + 1;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);  // 应该有三个错误
}

// 测试错误恢复对符号表的影响
TEST(SemanticRecoveryTest, SymbolTableConsistency) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 正确的声明
        number x = 1;

        {
            // 错误的声明不应影响符号表
            string x = true;

            // 正确的声明应该能继续
            number y = x + 2;  // 这里的 x 应该引用外层的 number x
        }

        // 正确的使用
        number z = x + 3;  // 这里的 x 应该仍然是有效的
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 1);  // 只应该有一个错误
}

// 测试错误恢复的边界情况
TEST(SemanticRecoveryTest, EdgeCaseRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 空函数体中的错误
        void empty() {
            // 错误1：空函数体中的无效语句
            42;
        }

        // 空块中的错误
        {
            // 错误2：无效的表达式语句
            "hello" + true;
        }

        // 空循环中的错误
        while (true) {
            // 错误3：无效的类型转换
            bool x = 42;
            break;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);
}

// 测试递归函数中的错误恢复
TEST(SemanticRecoveryTest, RecursiveFunctionRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number factorial(number n) {
            if (n <= 1) {
                // 错误1：返回类型不匹配
                return "one";
            }

            // 错误2：递归调用参数类型错误
            return n * factorial("n-1");
        }

        number compute() {
            // 错误3：递归调用参数数量错误
            return factorial();
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);
}

// 测试类型推导中的错误恢复
TEST(SemanticRecoveryTest, TypeInferenceRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 复杂的类型推导场景
        number x = 1;
        string s = "hello";
        bool b = true;

        // 错误1：混合类型运算
        number result1 = (x + s) * (b + 2);

        // 错误2：条件运算中的类型
        if (x + "world") {  // 条件必须是布尔类型
            number y = 2;
        }

        // 错误3：复杂表达式中的类型推导
        number result2 = (x > 0 ? "yes" : 42) + 1;
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);
}

// 测试错误恢复的优先级
TEST(SemanticRecoveryTest, ErrorRecoveryPriority) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        void process(number x) {
            // 多个错误在同一表达式中
            string result = (
                undefined_var +        // 错误1：未定义变量（最高优先级）
                (x + "hello") *       // 错误2：类型不匹配
                (true > 42)           // 错误3：比较类型错误
            );
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();

    // 验证错误的优先级
    EXPECT_EQ(errors.size(), 3);
    EXPECT_TRUE(errors[0].what() != nullptr);
    EXPECT_TRUE(std::string(errors[0].what()).find("undefined") != std::string::npos);
}

// 测试错误恢复的状态一致性
TEST(SemanticRecoveryTest, StateConsistencyRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 测试错误后的状态恢复
        number global = 1;

        void test_state() {
            number x = 1;
            {
                // 错误：类型不匹配
                string x = true;
                number y = x + 1;  // 应该使用外层的 x
            }
            number z = x + global;  // 应该能正确访问 x 和 global
        }

        // 测试函数状态恢复
        number get_value() {
            // 错误：返回类型不匹配
            return "error";
        }

        void caller() {
            number val = get_value() + 1;  // 应该能继续分析
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_GE(errors.size(), 2);
}

// 测试循环和条件语句中的错误恢复
TEST(SemanticRecoveryTest, LoopConditionRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        number count = 0;

        // 测试循环中的错误恢复
        while (count < 10) {
            if (count % 2 == 0) {
                // 错误：类型不匹配
                string temp = count;
                continue;
            } else {
                // 错误：未定义变量
                count = undefined + 1;
            }

            // 这里应该能继续执行
            count = count + 1;
        }

        // 测试条件语句中的错误恢复
        if (count > 5) {
            // 错误：无效运算
            bool result = "text" + true;
        } else if (count < 0) {
            // 正确的代码
            count = 0;
        }
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_GE(errors.size(), 3);
}

// 测试复杂的类型转换错误恢复
TEST(SemanticRecoveryTest, ComplexTypeConversionRecovery) {
    SemanticAnalyzer analyzer;

    auto [ast, tokens] = parse_and_get_tokens(R"(
        // 测试隐式类型转换
        byte b = 255;
        word w = b;      // 正确：byte 可以转换为 word
        number n = w;    // 正确：word 可以转换为 number

        // 错误：不允许的类型转换
        string s1 = n;   // 错误1：number 不能直接转换为 string
        bool b1 = w;     // 错误2：word 不能转换为 bool
        byte b2 = n;     // 错误3：number 不能自动转换为 byte（可能溢出）

        // 测试运算中的类型转换
        number result = (b + w) * n;  // 正确：自动提升为 number
    )");

    analyzer.set_tokens(tokens);
    analyzer.analyze(ast);

    EXPECT_TRUE(analyzer.has_errors());
    const auto& errors = analyzer.get_errors();
    EXPECT_EQ(errors.size(), 3);
}