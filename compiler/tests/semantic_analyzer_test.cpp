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

    // 常量声明
    EXPECT_NO_THROW({
        auto ast = parse("const number x = 42;");
        analyzer.analyze(ast);
    });

    // 常量未初始化
    EXPECT_THROW({
        auto ast = parse("const number x;");
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

// 函数测试
TEST(SemanticAnalyzerTest, Functions) {
    SemanticAnalyzer analyzer;

    // 基本函数定义和调用
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }
            number result = add(1, 2);
        )");
        analyzer.analyze(ast);
    });

    // 返回值类型不匹配
    EXPECT_THROW({
        auto ast = parse(R"(
            number getValue() {
                return "42";  // string 不能返回为 number
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 缺少返回值
    EXPECT_THROW({
        auto ast = parse(R"(
            number getValue() {
                number x = 42;
                // 缺少 return 语句
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 参数类型不匹配
    EXPECT_THROW({
        auto ast = parse(R"(
            void process(number x) {}
            process("42");  // string 不能传给 number 参数
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 控制流测试
TEST(SemanticAnalyzerTest, ControlFlow) {
    SemanticAnalyzer analyzer;

    // if 语句
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            if (true) {
                number x = 1;
            } else {
                number x = 2;
            }
        )");
        analyzer.analyze(ast);
    });

    // while 循环
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number i = 0;
            while (i < 10) {
                i = i + 1;
            }
        )");
        analyzer.analyze(ast);
    });

    // break/continue 语句
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            while (true) {
                if (true) break;
                continue;
            }
        )");
        analyzer.analyze(ast);
    });

    // 循环外使用 break
    EXPECT_THROW({
        auto ast = parse("break;");
        analyzer.analyze(ast);
    }, SemanticError);

    // 循环外使用 continue
    EXPECT_THROW({
        auto ast = parse("continue;");
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

    // 访问外层作用域
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number x = 1;
            {
                x = 2;  // 可以访问外层变量
            }
        )");
        analyzer.analyze(ast);
    });

    // 访问内层作用域（应该失败）
    EXPECT_THROW({
        auto ast = parse(R"(
            {
                number x = 1;
            }
            x = 2;  // 错误：x 不在作用域内
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 未定义变量
    EXPECT_THROW({
        auto ast = parse("y = 42;");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 运算符测试
TEST(SemanticAnalyzerTest, Operators) {
    SemanticAnalyzer analyzer;

    // 算术运算符
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number a = 1 + 2;
            number b = 3 - 4;
            number c = 5 * 6;
            number d = 8 / 2;
            number e = 7 % 3;
        )");
        analyzer.analyze(ast);
    });

    // 比较运算符
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            bool a = 1 < 2;
            bool b = 3 <= 4;
            bool c = 5 > 4;
            bool d = 6 >= 5;
            bool e = 7 == 7;
            bool f = 8 != 9;
        )");
        analyzer.analyze(ast);
    });

    // 逻辑运算符
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            bool a = true && false;
            bool b = true || false;
            bool c = !true;
        )");
        analyzer.analyze(ast);
    });

    // 位运算符
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            byte a = 0xFF & 0x0F;
            byte b = 0x0F | 0xF0;
            byte c = 0xFF ^ 0x0F;
            byte d = 0xFF << 2;
            byte e = 0xFF >> 2;
            byte f = ~0xFF;
        )");
        analyzer.analyze(ast);
    });

    // 类型错误
    EXPECT_THROW({
        auto ast = parse("number x = true + 1;");
        analyzer.analyze(ast);
    }, SemanticError);

    EXPECT_THROW({
        auto ast = parse("bool x = 1 < true;");
        analyzer.analyze(ast);
    }, SemanticError);

    EXPECT_THROW({
        auto ast = parse("byte x = 1.5 & 0xFF;");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 变量初始化测试
TEST(SemanticAnalyzerTest, Initialization) {
    SemanticAnalyzer analyzer;

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
                x = 1;
            }
            number y = x;  // 错误：x 可能未初始化
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 循环中的初始化
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number sum = 0;
            number i = 0;
            while (i < 10) {
                sum = sum + i;
                i = i + 1;
            }
        )");
        analyzer.analyze(ast);
    });
}

// 函数返回值测试
TEST(SemanticAnalyzerTest, FunctionReturns) {
    SemanticAnalyzer analyzer;

    // 所有路径都有返回值
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number max(number a, number b) {
                if (a > b) {
                    return a;
                } else {
                    return b;
                }
            }
        )");
        analyzer.analyze(ast);
    });

    // 缺少返回值路径
    EXPECT_THROW({
        auto ast = parse(R"(
            number max(number a, number b) {
                if (a > b) {
                    return a;
                }
                // 缺少 else 分支的返回值
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // void 函数不需要返回值
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            void process(number x) {
                if (x > 0) {
                    return;  // 可选的返回语句
                }
                // 不需要返回值
            }
        )");
        analyzer.analyze(ast);
    });
}

// 复杂类型转换测试
TEST(SemanticAnalyzerTest, ComplexTypeConversion) {
    SemanticAnalyzer analyzer;

    // 混合类型表达式
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            byte b = 10;
            word w = 100;
            number n = b + w;  // 混合类型算术
            string s = "Count: " + (b + w);  // 混合类型转字符串
        )");
        analyzer.analyze(ast);
    });

    // 字符串连接
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            string s1 = "Hello";
            string s2 = s1 + " " + "World" + "!";
            string s3 = "Number: " + 42 + ", Bool: " + true;
        )");
        analyzer.analyze(ast);
    });

    // 复杂的数值转换
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            byte b1 = 255;
            byte b2 = 1;
            word w = b1 + b2;  // byte 运算结果提升为 word
            number n = w * 1000;  // word 运算结果提升为 number
        )");
        analyzer.analyze(ast);
    });

    // 不合法的类型转换
    EXPECT_THROW({
        auto ast = parse(R"(
            string s = "123";
            number n = s;  // 不允许字符串到数字的隐式转换
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 复杂控制流测试
TEST(SemanticAnalyzerTest, ComplexControlFlow) {
    SemanticAnalyzer analyzer;

    // 嵌套循环和条件
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number i = 0;
            while (i < 10) {
                number j = 0;
                while (j < i) {
                    if (j % 2 == 0) {
                        continue;
                    }
                    if (i * j > 50) {
                        break;
                    }
                    j = j + 1;
                }
                i = i + 1;
            }
        )");
        analyzer.analyze(ast);
    });

    // 复杂的返回值路径
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number findValue(number x) {
                if (x < 0) {
                    return -1;
                }

                while (x > 100) {
                    if (x % 2 == 0) {
                        return x / 2;
                    }
                    x = x - 1;
                }

                return x;
            }
        )");
        analyzer.analyze(ast);
    });

    // 不可达代码检查
    EXPECT_THROW({
        auto ast = parse(R"(
            void process() {
                return;
                number x = 42;  // 不可达代码
            }
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}

// 函数重载测试（如果语言支持）
TEST(SemanticAnalyzerTest, FunctionOverloading) {
    SemanticAnalyzer analyzer;

    // 不同参数类型的重载
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }

            string add(string x, string y) {
                return x + y;
            }
        )");
        analyzer.analyze(ast);
    });

    // 不同参数数量的重载
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number sum(number x) {
                return x;
            }

            number sum(number x, number y) {
                return x + y;
            }

            number sum(number x, number y, number z) {
                return x + y + z;
            }
        )");
        analyzer.analyze(ast);
    });

    // 调用重载函数
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number add(number x, number y) {
                return x + y;
            }

            string add(string x, string y) {
                return x + y;
            }

            number n = add(1, 2);
            string s = add("Hello", "World");
        )");
        analyzer.analyze(ast);
    });
}

// 数组类型测试
TEST(SemanticAnalyzerTest, ArrayTypes) {
    SemanticAnalyzer analyzer;

    // 数组声明和初始化
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number[] arr = [1, 2, 3];
            string[] strs = ["hello", "world"];
        )");
        analyzer.analyze(ast);
    });

    // 数组访问
    EXPECT_NO_THROW({
        auto ast = parse(R"(
            number[] arr = [1, 2, 3];
            number x = arr[0];
            arr[1] = 42;
        )");
        analyzer.analyze(ast);
    });

    // 数组类型错误
    EXPECT_THROW({
        auto ast = parse(R"(
            number[] arr = [1, "hello", true];  // 类型不一致
        )");
        analyzer.analyze(ast);
    }, SemanticError);

    // 数组索引类型错误
    EXPECT_THROW({
        auto ast = parse(R"(
            number[] arr = [1, 2, 3];
            number x = arr["index"];  // 索引必须是数值类型
        )");
        analyzer.analyze(ast);
    }, SemanticError);
}