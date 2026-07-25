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

// ---- 用户自定义函数 (t11) ----

TEST(InterpreterEndToEnd, BasicFunctionCall) {
    EXPECT_EQ(run_source(R"(
        function add(a number, b number) number {
            return a + b;
        }
        print(add(3, 4));
    )"), "7\n");
}

TEST(InterpreterEndToEnd, FunctionReturnString) {
    EXPECT_EQ(run_source(R"(
        function greet(name string) string {
            return "Hello, " + name + "!";
        }
        print(greet("World"));
    )"), "Hello, World!\n");
}

TEST(InterpreterEndToEnd, RecursiveFactorial) {
    EXPECT_EQ(run_source(R"(
        function factorial(n number) number {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
        print(factorial(5));
    )"), "120\n");
}

TEST(InterpreterEndToEnd, NestedFunctionCalls) {
    EXPECT_EQ(run_source(R"(
        function double_val(x number) number {
            return x * 2;
        }
        function add_one(x number) number {
            return x + 1;
        }
        print(double_val(add_one(3)));
    )"), "8\n");
}

TEST(InterpreterEndToEnd, VoidFunction) {
    // 返回类型为 none 的函数，无显式 return
    EXPECT_EQ(run_source(R"(
        function say_hi() none {
            print("hi");
        }
        say_hi();
    )"), "hi\n");
}

TEST(InterpreterEndToEnd, FunctionWithLocalVariables) {
    EXPECT_EQ(run_source(R"(
        function sum_range(n number) number {
            number total = 0;
            number i = 1;
            while (i <= n) {
                total = total + i;
                i = i + 1;
            }
            return total;
        }
        print(sum_range(10));
    )"), "55\n");
}

TEST(InterpreterEndToEnd, EarlyReturn) {
    EXPECT_EQ(run_source(R"(
        function abs_val(x number) number {
            if (x < 0) {
                return -x;
            }
            return x;
        }
        print(abs_val(-7));
        print(abs_val(3));
    )"), "7\n3\n");
}

// ---- const 变量保护 (t14) ----

TEST(InterpreterEndToEnd, ConstVariableDeclaration) {
    // const 变量可以正常声明并读取
    EXPECT_EQ(run_source("const number x = 42; print(x);"), "42\n");
}

TEST(InterpreterEndToEnd, ConstStringVariable) {
    EXPECT_EQ(run_source(R"(const string msg = "hello"; print(msg);)"), "hello\n");
}

TEST(InterpreterEndToEnd, ConstAssignmentError) {
    // const 变量不允许重新赋值——语义层即拦截
    const std::string source = "const number x = 42; x = 99; print(x);";
    collie::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors()) << "semantic should reject const reassignment";
}

TEST(InterpreterEndToEnd, MutableVariableStillWorks) {
    // 非 const 变量仍可正常重新赋值
    EXPECT_EQ(run_source("number x = 1; x = 2; print(x);"), "2\n");
}

// =============================================================================
// do-while 循环（t18）
// =============================================================================

TEST(InterpreterEndToEnd, DoWhileBasic) {
    // 基本 do-while：至少执行一次
    EXPECT_EQ(run_source(R"(
        number x = 0;
        do {
            x = x + 1;
        } while (x < 3);
        print(x);
    )"), "3\n");
}

TEST(InterpreterEndToEnd, DoWhileRunsAtLeastOnce) {
    // 条件初始为 false 时仍执行一次循环体
    EXPECT_EQ(run_source(R"(
        number x = 10;
        do {
            x = x + 1;
        } while (x < 5);
        print(x);
    )"), "11\n");
}

TEST(InterpreterEndToEnd, DoWhileBreak) {
    // break 在 do-while 中正常工作
    EXPECT_EQ(run_source(R"(
        number x = 0;
        do {
            x = x + 1;
            if (x == 2) { break; }
        } while (x < 10);
        print(x);
    )"), "2\n");
}

TEST(InterpreterEndToEnd, DoWhileContinue) {
    // continue 在 do-while 中跳过后续语句但仍检查条件
    EXPECT_EQ(run_source(R"(
        number sum = 0;
        number i = 0;
        do {
            i = i + 1;
            if (i == 3) { continue; }
            sum = sum + i;
        } while (i < 5);
        print(sum);
    )"), "12\n");
}

// =============================================================================
// switch 语句（t19）
// =============================================================================

TEST(InterpreterEndToEnd, SwitchBasic) {
    // 基本 switch 匹配
    EXPECT_EQ(run_source(R"(
        number x = 2;
        switch (x) {
            1 { print("one"); }
            2 { print("two"); }
            3 { print("three"); }
        }
    )"), "two\n");
}

TEST(InterpreterEndToEnd, SwitchDefault) {
    // 无匹配时执行 default
    EXPECT_EQ(run_source(R"(
        number x = 99;
        switch (x) {
            1 { print("one"); }
            2 { print("two"); }
            default { print("other"); }
        }
    )"), "other\n");
}

TEST(InterpreterEndToEnd, SwitchMultiValue) {
    // 多值匹配（逗号分隔）
    EXPECT_EQ(run_source(R"(
        number x = 3;
        switch (x) {
            1, 2 { print("small"); }
            3, 4 { print("medium"); }
            default { print("large"); }
        }
    )"), "medium\n");
}

TEST(InterpreterEndToEnd, SwitchString) {
    // 字符串匹配
    EXPECT_EQ(run_source(R"(
        string lang = "collie";
        switch (lang) {
            "rust" { print("systems"); }
            "collie" { print("shepherd"); }
            default { print("unknown"); }
        }
    )"), "shepherd\n");
}

TEST(InterpreterEndToEnd, SwitchNoMatchNoDefault) {
    // 无匹配且无 default：不执行任何分支
    EXPECT_EQ(run_source(R"(
        number x = 5;
        switch (x) {
            1 { print("one"); }
            2 { print("two"); }
        }
        print("done");
    )"), "done\n");
}

// ===== 复合赋值运算符 =====

TEST(InterpreterEndToEnd, CompoundAssignPlus) {
    EXPECT_EQ(run_source(R"(
        number x = 10;
        x += 5;
        print(x);
    )"), "15\n");
}

TEST(InterpreterEndToEnd, CompoundAssignMinus) {
    EXPECT_EQ(run_source(R"(
        number x = 10;
        x -= 3;
        print(x);
    )"), "7\n");
}

TEST(InterpreterEndToEnd, CompoundAssignMultiply) {
    EXPECT_EQ(run_source(R"(
        number x = 4;
        x *= 3;
        print(x);
    )"), "12\n");
}

TEST(InterpreterEndToEnd, CompoundAssignDivide) {
    EXPECT_EQ(run_source(R"(
        number x = 20;
        x /= 4;
        print(x);
    )"), "5\n");
}

TEST(InterpreterEndToEnd, CompoundAssignModulo) {
    EXPECT_EQ(run_source(R"(
        number x = 17;
        x %= 5;
        print(x);
    )"), "2\n");
}

TEST(InterpreterEndToEnd, CompoundAssignStringConcat) {
    // += 用于字符串拼接
    EXPECT_EQ(run_source(R"(
        string s = "hello";
        s += " world";
        print(s);
    )"), "hello world\n");
}

TEST(InterpreterEndToEnd, CompoundAssignInLoop) {
    // 在循环中使用复合赋值
    EXPECT_EQ(run_source(R"(
        number sum = 0;
        number i = 1;
        while (i <= 5) {
            sum += i;
            i += 1;
        }
        print(sum);
    )"), "15\n");
}

// ===== 三元条件运算符 =====

TEST(InterpreterEndToEnd, TernaryBasicTrue) {
    EXPECT_EQ(run_source(R"(
        number x = 10;
        number y = x > 5 ? 100 : 200;
        print(y);
    )"), "100\n");
}

TEST(InterpreterEndToEnd, TernaryBasicFalse) {
    EXPECT_EQ(run_source(R"(
        number x = 3;
        number y = x > 5 ? 100 : 200;
        print(y);
    )"), "200\n");
}

TEST(InterpreterEndToEnd, TernaryString) {
    EXPECT_EQ(run_source(R"(
        bool ok = true;
        string msg = ok ? "yes" : "no";
        print(msg);
    )"), "yes\n");
}

TEST(InterpreterEndToEnd, TernaryNested) {
    // 嵌套三元（右结合）：x=15 命中第二分支
    EXPECT_EQ(run_source(R"(
        number x = 15;
        string size = x < 10 ? "small" : x < 20 ? "medium" : "large";
        print(size);
    )"), "medium\n");
}

TEST(InterpreterEndToEnd, TernaryInExpression) {
    // 三元作为子表达式参与算术
    EXPECT_EQ(run_source(R"(
        number a = 7;
        number b = (a % 2 == 0 ? 0 : 1) + 10;
        print(b);
    )"), "11\n");
}

TEST(InterpreterEndToEnd, TernaryLazyEvaluation) {
    // 未命中的分支不求值（否则除零会报运行时错误）
    EXPECT_EQ(run_source(R"(
        number x = 5;
        number y = x > 0 ? x * 2 : x / 0;
        print(y);
    )"), "10\n");
}

// ===== 内建函数 len / toString / toNumber =====

TEST(InterpreterEndToEnd, BuiltinLenAscii) {
    EXPECT_EQ(run_source(R"(
        print(len("hello"));
    )"), "5\n");
}

TEST(InterpreterEndToEnd, BuiltinLenUnicode) {
    // 中文按码点计数（非字节数）
    EXPECT_EQ(run_source(R"(
        print(len("牛羊犬"));
    )"), "3\n");
}

TEST(InterpreterEndToEnd, BuiltinLenEmpty) {
    EXPECT_EQ(run_source(R"(
        print(len(""));
    )"), "0\n");
}

TEST(InterpreterEndToEnd, BuiltinToString) {
    EXPECT_EQ(run_source(R"(
        string s = toString(42);
        print(s + "!");
    )"), "42!\n");
}

TEST(InterpreterEndToEnd, BuiltinToStringBool) {
    EXPECT_EQ(run_source(R"(
        print(toString(true));
    )"), "true\n");
}

TEST(InterpreterEndToEnd, BuiltinToNumber) {
    EXPECT_EQ(run_source(R"(
        number n = toNumber("123");
        print(n + 1);
    )"), "124\n");
}

TEST(InterpreterEndToEnd, BuiltinToNumberDecimal) {
    EXPECT_EQ(run_source(R"(
        print(toNumber("3.5") * 2);
    )"), "7\n");
}

TEST(InterpreterEndToEnd, BuiltinToNumberBool) {
    EXPECT_EQ(run_source(R"(
        print(toNumber(true) + toNumber(false));
    )"), "1\n");
}

TEST(InterpreterEndToEnd, BuiltinChained) {
    // 组合使用：len(toString(12345)) = 5
    EXPECT_EQ(run_source(R"(
        print(len(toString(12345)));
    )"), "5\n");
}

// ===== 数组字面量与索引 =====

TEST(InterpreterEndToEnd, ArrayLiteralPrint) {
    EXPECT_EQ(run_source(R"(
        array a = [1, 2, 3];
        print(a);
    )"), "[1, 2, 3]\n");
}

TEST(InterpreterEndToEnd, ArrayIndexRead) {
    // 索引读取，并可赋给具体类型变量
    EXPECT_EQ(run_source(R"(
        array a = [10, 20, 30];
        number x = a[0];
        print(x, a[2]);
    )"), "10 30\n");
}

TEST(InterpreterEndToEnd, ArrayNegativeIndex) {
    // 负索引：-1 为最后一个元素
    EXPECT_EQ(run_source(R"(
        array a = [1, 2, 3];
        print(a[-1], a[-3]);
    )"), "3 1\n");
}

TEST(InterpreterEndToEnd, ArrayIndexAssign) {
    EXPECT_EQ(run_source(R"(
        array a = [1, 2, 3];
        a[1] = 42;
        print(a);
    )"), "[1, 42, 3]\n");
}

TEST(InterpreterEndToEnd, ArrayLen) {
    EXPECT_EQ(run_source(R"(
        array a = [5, 6, 7, 8];
        print(len(a));
    )"), "4\n");
}

TEST(InterpreterEndToEnd, ArrayEmpty) {
    EXPECT_EQ(run_source(R"(
        array e = [];
        print(len(e), e);
    )"), "0 []\n");
}

TEST(InterpreterEndToEnd, ArrayNestedIndex) {
    // 嵌套数组与链式索引
    EXPECT_EQ(run_source(R"(
        array m = [[1, 2], [3, 4]];
        print(m[0][1], m[1][0]);
    )"), "2 3\n");
}

TEST(InterpreterEndToEnd, ArrayReferenceSemantics) {
    // 数组为引用语义：赋值后共享同一底层存储
    EXPECT_EQ(run_source(R"(
        array a = [1, 2];
        array b = a;
        b[0] = 9;
        print(a[0]);
    )"), "9\n");
}

TEST(InterpreterEndToEnd, ArrayTrailingComma) {
    // 尾逗号（语言设计稿约定支持）
    EXPECT_EQ(run_source(R"(
        array a = [1, 2, 3,];
        print(len(a));
    )"), "3\n");
}

TEST(InterpreterEndToEnd, ArraySumInLoop) {
    // 循环遍历累加
    EXPECT_EQ(run_source(R"(
        array a = [10, 20, 30];
        number s = 0;
        for (number i = 0; i < len(a); i += 1) {
            s += a[i];
        }
        print(s);
    )"), "60\n");
}

TEST(InterpreterEndToEnd, ArrayIndexOutOfRange) {
    // 越界访问抛运行时错误
    collie::Lexer lexer("array a = [1, 2, 3]; print(a[3]);");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    ASSERT_FALSE(analyzer.has_errors());
    std::ostringstream out;
    collie::Interpreter interpreter(out);
    EXPECT_THROW(interpreter.interpret(stmts), collie::RuntimeError);
}

// ---------------------------------------------------------------------------
// 字符串索引（t24）：按 UTF-8 码点索引，返回单字符子串；支持负索引
// ---------------------------------------------------------------------------

TEST(InterpreterEndToEnd, StringIndexRead) {
    EXPECT_EQ(run_source(R"(string s = "hello"; print(s[0], s[4]);)"), "h o\n");
}

TEST(InterpreterEndToEnd, StringNegativeIndex) {
    EXPECT_EQ(run_source(R"(string s = "hello"; print(s[-1], s[-5]);)"), "o h\n");
}

TEST(InterpreterEndToEnd, StringIndexUnicode) {
    // 按码点而非字节索引：中文字符占 3 字节但算 1 个字符
    EXPECT_EQ(run_source(R"(string s = "牛→犬a"; print(s[0], s[1], s[2], s[3]);)"),
              "牛 → 犬 a\n");
}

TEST(InterpreterEndToEnd, StringIndexInExpression) {
    // 索引结果是 string，可参与拼接与比较
    EXPECT_EQ(run_source(R"(
        string s = "abc";
        string r = s[2] + s[1] + s[0];
        print(r, s[0] == "a");
    )"),
              "cba true\n");
}

TEST(InterpreterEndToEnd, StringIndexLoop) {
    // 配合 len 遍历字符串
    EXPECT_EQ(run_source(R"(
        string s = "dog";
        string r = "";
        for (number i = 0; i < len(s); i += 1) {
            r += s[i] + ".";
        }
        print(r);
    )"),
              "d.o.g.\n");
}

TEST(InterpreterEndToEnd, StringIndexOutOfRange) {
    collie::Lexer lexer(R"(string s = "abc"; print(s[3]);)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    ASSERT_FALSE(analyzer.has_errors());
    std::ostringstream out;
    collie::Interpreter interpreter(out);
    EXPECT_THROW(interpreter.interpret(stmts), collie::RuntimeError);
}

TEST(InterpreterEndToEnd, StringIndexAssignRejected) {
    // 字符串不可变：索引赋值在语义层报错
    collie::Lexer lexer(R"(string s = "abc"; s[0] = "x";)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}
