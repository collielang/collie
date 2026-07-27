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

// 取模为 floor 语义（Python 风格），三个用例均出自设计文档 04-numeric.md
TEST(InterpreterEndToEnd, FloorModuloSemantics) {
    EXPECT_EQ(run_source(R"(
        print(-1 % 5);
        print(-1 % -5);
        print(1 % -5);
    )"), "4\n-1\n-4\n");
}

// floor 取模同样适用于小数操作数
TEST(InterpreterEndToEnd, FloorModuloDecimal) {
    EXPECT_EQ(run_source(R"(
        print(5.5 % 2);
        print(-5.5 % 2);
    )"), "1.5\n0.5\n");
}

// 前导点小数与 f 后缀字面量（见设计文档 04-numeric.md）
TEST(InterpreterEndToEnd, NumberLiteralForms) {
    EXPECT_EQ(run_source(R"(
        number a = .5;
        number b = 2f;
        print(a + .25);
        print(b);
    )"), "0.75\n2\n");
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

// 以下方法调用用例见设计文档 04-numeric.md
TEST(InterpreterEndToEnd, MethodCallNumberPredicates) {
    EXPECT_EQ(run_source(R"(
        number n = 2025;
        print(n.isInteger());
        print(n.isPositive());
        print(n.isFinite());
        print(n.isDecimal());
        print(n.isNegative());
        print(n.isNaN());
    )"), R"(true
true
true
false
false
false
)");
}

TEST(InterpreterEndToEnd, MethodCallAbsAndParts) {
    EXPECT_EQ(run_source(R"(
        number d = -123.456;
        print(d.abs());
        print(d.integerPart());
        print(d.decimalPart());
    )"), "123.456\n-123\n-0.456\n");
}

TEST(InterpreterEndToEnd, MethodCallToStringToNumber) {
    // 与内建函数 toString/toNumber 行为一致；支持字面量接收者
    EXPECT_EQ(run_source(R"(
        number n = 42;
        print(n.toString() + "!");
        print("3.5".toNumber() + 1);
        print(12.toString());
    )"), "42!\n4.5\n12\n");
}

TEST(InterpreterEndToEnd, MethodCallChainedAndIndexed) {
    // 链式调用与索引混合：数字字面量.方法().方法()、数组元素.方法()
    EXPECT_EQ(run_source(R"(
        print(3.7.integerPart().toString() + "|");
        array arr = [-5, 2];
        print(arr[0].abs());
    )"), "3|\n5\n");
}

// ===== Infinity / NaN 字面量与相关语义（见设计文档 04-numeric.md） =====

TEST(InterpreterEndToEnd, InfinityNaNLiteralsToString) {
    // toString 格式：+Infinity / -Infinity / NaN
    EXPECT_EQ(run_source(R"(
        print(Infinity);
        print(-Infinity);
        print(NaN);
        number x = Infinity;
        print(x.toString());
    )"), R"(+Infinity
-Infinity
NaN
+Infinity
)");
}

TEST(InterpreterEndToEnd, InfinityPredicates) {
    EXPECT_EQ(run_source(R"(
        number p = Infinity;
        print(p.isInfinity());
        print(p.isFinite());
        print(p.isPositive());
        print(p.isNegative());
        print(p.isNaN());
        print(p.isInteger());
        print(p.isDecimal());
        number q = -Infinity;
        print(q.isInfinity());
        print(q.isNegative());
    )"), R"(true
false
true
false
false
false
false
true
true
)");
}

TEST(InterpreterEndToEnd, NaNPredicatesAndComparison) {
    // NaN 的所有谓词除 isNaN 外均为 false；NaN 与自身比较不相等（IEEE 754）
    EXPECT_EQ(run_source(R"(
        number n = NaN;
        print(n.isNaN());
        print(n.isInfinity());
        print(n.isFinite());
        print(n.isPositive());
        print(n.isNegative());
        print(n == n);
        print(n != n);
    )"), R"(true
false
false
false
false
false
true
)");
}

TEST(InterpreterEndToEnd, InfinityArithmetic) {
    // 无穷参与算术运算遵循 IEEE 754：Inf+1=Inf、Inf-Inf=NaN、Inf*-1=-Inf
    EXPECT_EQ(run_source(R"(
        number inf = Infinity;
        print(inf + 1);
        print(inf * -1);
        number d = inf - inf;
        print(d.isNaN());
    )"), "+Infinity\n-Infinity\ntrue\n");
}

TEST(InterpreterEndToEnd, ToNumberInfinityForms) {
    // 字符串转数字的特殊形式严格大小写匹配；不可解析返回 NaN 而非报错
    EXPECT_EQ(run_source(R"(
        print(toNumber("Infinity"));
        print(toNumber("+Infinity"));
        print(toNumber("-Infinity"));
        number a = toNumber("infinity");
        print(a.isNaN());
        number b = toNumber("abc");
        print(b.isNaN());
    )"), R"(+Infinity
+Infinity
-Infinity
true
true
)");
}

TEST(InterpreterEndToEnd, DivisionByZeroIEEE754) {
    // 除零遵循 IEEE 754（经作者确认）：1/0 → +Infinity、-1/0 → -Infinity、
    // 0/0 → NaN；取模除数为 0 → NaN（不再报运行时错误）
    EXPECT_EQ(run_source(R"(
        print(1 / 0);
        print(-1 / 0);
        number a = 0 / 0;
        print(a.isNaN());
        number b = 5 % 0;
        print(b.isNaN());
    )"), R"(+Infinity
-Infinity
true
true
)");
}

// ===== 字符串插值 @"...{expr}..."（见设计文档 03-character.md） =====

TEST(InterpreterEndToEnd, StringInterpolationBasic) {
    // 文档示例：变量插值
    EXPECT_EQ(run_source(R"(
        string name = "Lily";
        number age = 18;
        string sex = "girl";
        string str = @"{name} is {age}-year-old {sex}.";
        print(str);
    )"), "Lily is 18-year-old girl.\n");
}

TEST(InterpreterEndToEnd, StringInterpolationExpressions) {
    // 插值段支持任意表达式：算术、方法调用、属性、三元
    EXPECT_EQ(run_source(R"(
        number n = 6;
        string s = "hello";
        print(@"{n * 7} and {s.length} and {n > 5 ? \"big\" : \"small\"}");
    )"), "42 and 5 and big\n");
}

TEST(InterpreterEndToEnd, StringInterpolationEscapes) {
    // \{ \} 输出字面花括号；常规转义与普通字符串一致；纯文本/空串退化
    EXPECT_EQ(run_source(R"(
        number x = 1;
        print(@"\{x\} = {x}");
        print(@"plain");
        print(@"" + "empty");
    )"), "{x} = 1\nplain\nempty\n");
}

TEST(InterpreterEndToEnd, StringInterpolationTypeIsString) {
    // 脱糖结果为 string 类型：可参与拼接与方法调用
    EXPECT_EQ(run_source(R"(
        number n = 3;
        string s = @"n={n}" + "!";
        print(s);
        print(@"  {n}  ".trim());
    )"), "n=3!\n3\n");
}

TEST(InterpreterEndToEnd, StringInterpolationInvalidExprRejected) {
    // 插值段非法表达式：解析期报错
    collie::Lexer lexer("string s = @\"{1 +}\";");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    EXPECT_FALSE(parser.get_errors().empty());
}

TEST(InterpreterEndToEnd, UnknownMethodRejected) {
    // 未知方法在语义层报错
    collie::Lexer lexer(R"(number n = 1; n.foo();)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

TEST(InterpreterEndToEnd, MethodCallWrongReceiverRejected) {
    // number 专属方法用在字符串上：语义层报错
    collie::Lexer lexer(R"(string s = "abc"; s.abs();)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

// 以下属性/字符串方法用例见设计文档 03-character.md
TEST(InterpreterEndToEnd, LengthProperty) {
    // string 按 UTF-8 码点计数，array 按元素数
    EXPECT_EQ(run_source(R"(
        string s = "héllo";
        print(s.length);
        array arr = [1, 2, 3];
        print(arr.length);
        print("".length);
    )"), R"(5
3
0
)");
}

TEST(InterpreterEndToEnd, StringTrimMethods) {
    EXPECT_EQ(run_source(R"(
        string s = "  hi  ";
        print("|" + s.trim() + "|");
        print("|" + s.trimLeft() + "|");
        print("|" + s.trimRight() + "|");
    )"), R"(|hi|
|hi  |
|  hi|
)");
}

TEST(InterpreterEndToEnd, StringSubString) {
    // [start, end) 码点区间；end 缺省/-1 取 length；start >= end 为空串
    EXPECT_EQ(run_source(R"(
        string str = "hello world";
        print(str.subString(0, 2));
        print(str.subString(6));
        print(str.subString(6, -1));
        print("héllo".subString(1, 3));
        print(str.subString(4, 2) + "|");
    )"), R"(he
world
world
él
|
)");
}

TEST(InterpreterEndToEnd, PropertyAndMethodChained) {
    EXPECT_EQ(run_source(R"(
        print("  collie  ".trim().length);
        print("hello".subString(1, 4).length.toString() + "!");
    )"), R"(6
3!
)");
}

TEST(InterpreterEndToEnd, UnknownPropertyRejected) {
    // 未知属性在语义层报错
    collie::Lexer lexer(R"(string s = "abc"; number n = s.foo;)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

TEST(InterpreterEndToEnd, LengthWrongReceiverRejected) {
    // length 用在 number 上：语义层报错
    collie::Lexer lexer(R"(number n = 1; number m = n.length;)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

TEST(InterpreterEndToEnd, SubStringArityRejected) {
    // subString 元数错误（0 参）：语义层报错
    collie::Lexer lexer(R"(string s = "abc"; s.subString();)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

// ---------------------------------------------------------------------------
// class 基础（t34）：Java/C# 风格最小子集（字段/方法/构造器/new/this，
// 见 uncategorized.md 附录，经作者确认）
// ---------------------------------------------------------------------------

TEST(InterpreterEndToEnd, ClassBasics) {
    // 字段（含初始化/缺省）+ 构造器 + 方法 + this 读写字段 + 外部属性读写
    EXPECT_EQ(run_source(R"(
        class Animal {
            public string name;
            private number age = 1;

            public Animal(n string) {
                this.name = n;
            }

            public function speak() none {
                print(this.name + " makes a sound");
            }

            public function getAge() number {
                return this.age;
            }

            public function birthday() none {
                this.age = this.age + 1;
            }
        }

        Animal a = new Animal("Buddy");
        a.speak();
        print(a.name);
        print(a.getAge());
        a.birthday();
        print(a.getAge());
        a.name = "Max";
        a.speak();
    )"), R"(Buddy makes a sound
Buddy
1
2
Max makes a sound
)");
}

TEST(InterpreterEndToEnd, ClassWithoutConstructor) {
    // 无构造器：new 0 实参，字段按初始化表达式/none 缺省
    EXPECT_EQ(run_source(R"(
        class Counter {
            public number count = 0;

            public function inc() none {
                this.count = this.count + 1;
            }
        }

        Counter c = new Counter();
        c.inc();
        c.inc();
        print(c.count);
    )"), "2\n");
}

TEST(InterpreterEndToEnd, ClassInstanceReferenceSemantics) {
    // 实例为引用语义：赋值后两个变量共享同一对象
    EXPECT_EQ(run_source(R"(
        class Animal {
            public string name = "Rex";
        }

        Animal a = new Animal();
        Animal b = a;
        b.name = "Max";
        print(a.name);
    )"), "Max\n");
}

TEST(InterpreterEndToEnd, ClassMethodCallsMethod) {
    // 方法内通过 this 调用同类其他方法；方法带参数
    EXPECT_EQ(run_source(R"(
        class Calc {
            public number base = 10;

            public function add(x number) number {
                return this.base + x;
            }

            public function addTwice(x number) number {
                return this.add(x) + this.add(x);
            }
        }

        Calc calc = new Calc();
        print(calc.add(5));
        print(calc.addTwice(5));
    )"), R"(15
30
)");
}

TEST(InterpreterEndToEnd, ClassUndefinedFieldRejected) {
    // 未声明字段的读取：运行期报错（语义层对 object 动态放行）
    collie::Lexer lexer(R"(
        class Animal { public string name; }
        Animal a = new Animal();
        print(a.foo);
    )");
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

TEST(InterpreterEndToEnd, ClassConstructorArityRejected) {
    // 构造器实参数量不匹配：运行期报错
    collie::Lexer lexer(R"(
        class Animal {
            public string name;
            public Animal(n string) { this.name = n; }
        }
        Animal a = new Animal();
    )");
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

TEST(InterpreterEndToEnd, UndefinedClassRejected) {
    // new 未声明的类：语义层报错
    collie::Lexer lexer(R"(object a = new Ghost();)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

TEST(InterpreterEndToEnd, ThisOutsideClassRejected) {
    // 类外使用 this：语义层报错
    collie::Lexer lexer(R"(print(this);)");
    std::vector<collie::Token> tokens = lexer.tokenize();
    collie::Parser parser(tokens);
    auto stmts = parser.parse_program();
    collie::SemanticAnalyzer analyzer;
    analyzer.analyze(stmts);
    EXPECT_TRUE(analyzer.has_errors());
}

// ---------------------------------------------------------------------------
// 运行期声明类型校验与隐式转换（t35）
// ---------------------------------------------------------------------------

TEST(InterpreterEndToEnd, StringImplicitConversionOnDecl) {
    // 语义层允许 number/bool 隐式转 string，运行期真正转为字符串
    //（此前变量实际绑定的仍是 number，s.length 会误报）
    EXPECT_EQ(run_source(R"(
        string s = 42;
        print(s.length);
        print(s + "!");
        string b = true;
        print(b);
    )"), R"(2
42!
true
)");
}

TEST(InterpreterEndToEnd, StringImplicitConversionOnAssign) {
    // 赋值同样按声明类型隐式转换
    EXPECT_EQ(run_source(R"(
        string s = "a";
        s = 5;
        print(s.length);
        print(s + "!");
    )"), R"(1
5!
)");
}

TEST(InterpreterEndToEnd, ParamImplicitStringConversion) {
    // 实参 number 传给 string 形参：运行期转字符串后绑定
    EXPECT_EQ(run_source(R"(
        function tag(s string) string {
            return "<" + s + ">";
        }
        print(tag(42));
    )"), R"(<42>
)");
}

TEST(InterpreterEndToEnd, DeclTypeMismatchRuntimeRejected) {
    // 动态路径（object）声明初始化：语义层放行，运行期拦截
    collie::Lexer lexer(R"(
        array a = [1, "x"];
        number n = a[1];
    )");
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

TEST(InterpreterEndToEnd, AssignTypeMismatchRuntimeRejected) {
    // 动态路径赋值给 number 变量：运行期拦截
    collie::Lexer lexer(R"(
        array a = ["x"];
        number n = 1;
        n = a[0];
    )");
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

TEST(InterpreterEndToEnd, ParamTypeMismatchRuntimeRejected) {
    // 动态路径实参传给 number 形参：运行期拦截
    collie::Lexer lexer(R"(
        function twice(n number) number {
            return n * 2;
        }
        array a = ["x"];
        twice(a[0]);
    )");
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

TEST(InterpreterEndToEnd, BoolDeclTypeMismatchRuntimeRejected) {
    // 动态路径初始化 bool 变量：运行期拦截（bool 无隐式转换）
    collie::Lexer lexer(R"(
        array a = [1];
        bool b = a[0];
    )");
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
// 类字段类型运行期校验（t36）
// ---------------------------------------------------------------------------

TEST(InterpreterEndToEnd, FieldImplicitStringConversion) {
    // 字段初始化与字段赋值均按字段声明类型隐式转换
    EXPECT_EQ(run_source(R"(
        class Tag {
            public string label = 42;

            public function Tag() none {
            }
        }
        Tag t = new Tag();
        print(t.label.length);
        t.label = true;
        print(t.label + "!");
    )"), R"(2
true!
)");
}

TEST(InterpreterEndToEnd, FieldCtorAssignImplicitConversion) {
    // 构造器内 this.x = 实参 同样经字段声明类型转换
    EXPECT_EQ(run_source(R"(
        class Box {
            public string content = "";

            public function Box(v object) none {
                this.content = v;
            }
        }
        Box b = new Box(99);
        print(b.content.length);
    )"), R"(2
)");
}

TEST(InterpreterEndToEnd, FieldInitTypeMismatchRuntimeRejected) {
    // 字段初始化表达式动态路径类型不匹配：运行期拦截
    collie::Lexer lexer(R"(
        array a = ["x"];
        class Counter {
            public number count = a[0];
        }
        Counter c = new Counter();
    )");
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

TEST(InterpreterEndToEnd, FieldAssignTypeMismatchRuntimeRejected) {
    // 字段赋值动态路径类型不匹配：运行期拦截
    collie::Lexer lexer(R"(
        class Counter {
            public number count = 0;
        }
        array a = ["x"];
        Counter c = new Counter();
        c.count = a[0];
    )");
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
// 函数返回值类型运行期校验（t37）
// ---------------------------------------------------------------------------

TEST(InterpreterEndToEnd, ReturnImplicitStringConversion) {
    // 返回值按声明返回类型隐式转换：number → string
    EXPECT_EQ(run_source(R"(
        function label() string {
            return 42;
        }
        print(label().length);
        print(label() + "!");
    )"), R"(2
42!
)");
}

TEST(InterpreterEndToEnd, MethodReturnImplicitStringConversion) {
    // 类方法返回值同样按声明返回类型隐式转换
    EXPECT_EQ(run_source(R"(
        class Tag {
            public function id() string {
                return 7;
            }
        }
        Tag t = new Tag();
        print(t.id().length);
    )"), R"(1
)");
}

TEST(InterpreterEndToEnd, ReturnTypeMismatchRuntimeRejected) {
    // 动态路径返回值与声明返回类型不匹配：运行期拦截
    collie::Lexer lexer(R"(
        function first(a array) number {
            return a[0];
        }
        first(["x"]);
    )");
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
