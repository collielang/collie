/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include <gtest/gtest.h>
#include "../semantic/semantic_analyzer.h"
#include "../parser/parser.h"
#include "../lexer/lexer.h"

using namespace collie;

// 辅助函数：解析源代码并返回AST
std::vector<std::unique_ptr<Stmt>> parse(const std::string& source) {
    Lexer lexer(source);
    Parser parser(lexer);
    return parser.parse_program();
}

// 基本变量声明测试
TEST(SemanticAnalyzerTest, BasicVariableDeclaration) {
    SemanticAnalyzer analyzer;

    // 正确的变量声明
    EXPECT_NO_THROW({
        auto ast = parse("number x = 42;");
        analyzer.analyze(ast);
    });

    // 重复声明
    EXPECT_THROW({
        auto ast = parse("number x = 1; number x = 2;");
        analyzer.analyze(ast);
    }, SemanticError);

    // 类型不匹配
    EXPECT_THROW({
        auto ast = parse("string x = 42;");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 作用域测试
TEST(SemanticAnalyzerTest, Scopes) {
    SemanticAnalyzer analyzer;

    // 不同作用域的同名变量
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number x = 1;
            {
                number x = 2;  // 合法的遮蔽
            }
        )");
        analyzer.analyze(ast);
    });

    // 未定义变量
    EXPECT_THROW({
        auto ast = parse("y = 42;");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 函数声明和调用测试
TEST(SemanticAnalyzerTest, Functions) {
    SemanticAnalyzer analyzer;

    // 正确的函数声明和调用
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }
            number result = add(1, 2);
        )");
        analyzer.analyze(ast);
    });

    // 参数类型不匹配
    EXPECT_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }
            number result = add("hello", 2);
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 参数数量不匹配
    EXPECT_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }
            number result = add(1);
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 类型检查测试
TEST(SemanticAnalyzerTest, TypeChecking) {
    SemanticAnalyzer analyzer;

    // 算术运算
    EXPECT_NO_THROW({
        auto ast = parse("number x = 1 + 2 * 3;");
        analyzer.analyze(ast);
    });

    // 类型不兼容的运算
    EXPECT_THROW({
        auto ast = parse(R"(number x = "hello" + 42;)");
        analyzer.analyze(ast);
    }, SemanticError);

    // 条件表达式类型检查
    EXPECT_THROW({
        auto ast = parse("if (42) { number x = 1; }");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 添加一元操作符测试
TEST(SemanticAnalyzerTest, UnaryOperators) {
    SemanticAnalyzer analyzer;

    // 数字取负
    EXPECT_NO_THROW({
        auto ast = parse("number x = -42;");
        analyzer.analyze(ast);
    });

    // 布尔取反
    EXPECT_NO_THROW({
        auto ast = parse("bool x = !true;");
        analyzer.analyze(ast);
    });

    // 位运算取反
    EXPECT_NO_THROW({
        auto ast = parse("byte x = ~0xFF;");
        analyzer.analyze(ast);
    });

    // 类型错误
    EXPECT_THROW({
        auto ast = parse("number x = !42;");
        analyzer.analyze(ast);
    }, SemanticError);
}

/**
 * return 语句测试
 */
TEST(SemanticAnalyzerTest, ReturnStatement) {
    SemanticAnalyzer analyzer;

    // 正确的返回值类型
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }
        )");
        analyzer.analyze(ast);
    });

    // 返回值类型不匹配
    EXPECT_THROW({
        auto ast = parse(R"(
            number getValue() {
                return "hello";
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 缺少返回值
    EXPECT_THROW({
        auto ast = parse(R"(
            number getValue() {
                return;
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 函数外的 return 语句
    EXPECT_THROW({
        auto ast = parse("return 42;");
        analyzer.analyze(ast);
    }, SemanticError);
}

/**
 * 二元操作符测试
 */
TEST(SemanticAnalyzerTest, BinaryOperators) {
    SemanticAnalyzer analyzer;

    // 字符串连接
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            string s1 = "hello" + " world";
            string s2 = "value: " + 42;
            string s3 = true + " is boolean";
        )");
        analyzer.analyze(ast);
    });

    // 数值运算
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number n1 = 1 + 2 * 3;
            number n2 = 10 / 2;
            number n3 = 7 % 3;
        )");
        analyzer.analyze(ast);
    });

    // 比较运算
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            bool b1 = 1 < 2;
            bool b2 = "abc" <= "def";
            bool b3 = 'a' >= 'b';
            bool b4 = 42 == 42;
        )");
        analyzer.analyze(ast);
    });

    // 逻辑运算
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            bool b1 = true && false;
            bool b2 = true || false;
        )");
        analyzer.analyze(ast);
    });

    // 位运算
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            byte b1 = 0xFF & 0x0F;
            byte b2 = 0x0F | 0xF0;
            byte b3 = 0xFF ^ 0x0F;
            byte b4 = 0xFF << 4;
            byte b5 = 0xFF >> 4;
        )");
        analyzer.analyze(ast);
    });

    // 类型错误
    EXPECT_THROW({
        auto ast = parse(R"(string s = 42 + true;)");
        analyzer.analyze(ast);
    }, SemanticError);

    EXPECT_THROW({
        auto ast = parse(R"(bool b = 1 + 2 < "hello";)");
        analyzer.analyze(ast);
    }, SemanticError);

    EXPECT_THROW({
        auto ast = parse(R"(number n = true + 42;)");
        analyzer.analyze(ast);
    }, SemanticError);
}

/**
 * 控制流语句测试
 */
TEST(SemanticAnalyzerTest, ControlFlow) {
    SemanticAnalyzer analyzer;

    // if 语句
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number x = 42;
            if (x > 0) {
                number y = x + 1;
            } else {
                number y = x - 1;
            }
        )");
        analyzer.analyze(ast);
    });

    // while 语句
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number x = 10;
            while (x > 0) {
                x = x - 1;
            }
        )");
        analyzer.analyze(ast);
    });

    // for 语句
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number sum = 0;
            for (number i = 0; i < 10; i = i + 1) {
                sum = sum + i;
            }
        )");
        analyzer.analyze(ast);
    });

    // 非布尔条件
    EXPECT_THROW({
        auto ast = parse(R"(
            if (42) {
                number x = 1;
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 作用域测试
    EXPECT_THROW({
        auto ast = parse(R"(
            if (true) {
                number x = 1;
            }
            number y = x;  // x 不在作用域内
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

/**
 * 函数作用域测试
 */
TEST(SemanticAnalyzerTest, FunctionScope) {
    SemanticAnalyzer analyzer;

    // 函数参数作用域
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                number z = x + y;
                return z;
            }
        )");
        analyzer.analyze(ast);
    });

    // 重复的参数名
    EXPECT_THROW({
        auto ast = parse(R"(
            number add(number x, number x) {
                return x;
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 访问函数外的变量
    EXPECT_THROW({
        auto ast = parse(R"(
            number x = 42;
            number getValue() {
                x = x + 1;  // 不允许访问全局变量
                return x;
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

/**
 * 常量和变量初始化测试
 */
TEST(SemanticAnalyzerTest, ConstAndInitialization) {
    SemanticAnalyzer analyzer;

    // 常量声明
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            const number PI = 3.14159;
            const string MESSAGE = "Hello";
        )");
        analyzer.analyze(ast);
    });

    // 常量未初始化
    EXPECT_THROW({
        auto ast = parse("const number x;");
        analyzer.analyze(ast);
    }, SemanticError);

    // 常量重新赋值
    EXPECT_THROW({
        auto ast = parse(R"(
            const number x = 42;
            x = 43;  // 错误：不能给常量赋值
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 使用未初始化的变量
    EXPECT_THROW({
        auto ast = parse(R"(
            number x;
            number y = x + 1;  // 错误：使用未初始化的变量
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 条件分支中的初始化
    EXPECT_THROW({
        auto ast = parse(R"(
            number x;
            if (true) {
                x = 42;
            }
            number y = x;  // 错误：x 可能未初始化
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

/**
 * 类型转换测试
 */
TEST(SemanticAnalyzerTest, TypeConversion) {
    SemanticAnalyzer analyzer;

    // 数值类型转换
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            byte b = 42;
            number n = b;  // byte 到 number 的隐式转换
            word w = 1000;
            number n2 = w;  // word 到 number 的隐式转换
        )");
        analyzer.analyze(ast);
    });

    // 字符类型转换
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            char c = 'A';
            character ch = c;  // char 到 character 的转换
            string s = c;      // char 到 string 的转换
            string s2 = ch;    // character 到 string 的转换
        )");
        analyzer.analyze(ast);
    });

    // 字符串转换
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            string s1 = "Value: " + 42;        // number 到 string 的转换
            string s2 = "Flag: " + true;       // bool 到 string 的转换
            string s3 = "Byte: " + byte(255);  // byte 到 string 的转换
        )");
        analyzer.analyze(ast);
    });

    // 不允许的转换
    EXPECT_THROW({
        auto ast = parse(R"(
            number n = "42";  // string 到 number 的隐式转换不允许
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    EXPECT_THROW({
        auto ast = parse(R"(
            bool b = 1;  // number 到 bool 的隐式转换不允许
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}