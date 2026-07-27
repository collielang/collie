/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2025-01-05
 */
#include <gtest/gtest.h>
#include <memory>
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#endif

#include "../parser/parser.h"
#include "../lexer/lexer.h"

using namespace collie;

// 辅助函数：创建一个简单的表达式访问者来验证AST结构
class TestExprVisitor : public ExprVisitor {
public:
    void visitLiteral(const LiteralExpr& expr) override {
        result_ = std::string(expr.token().lexeme());
    }

    void visitIdentifier(const IdentifierExpr& expr) override {
        result_ = std::string(expr.name().lexeme());
    }

    void visitBinary(const BinaryExpr& expr) override {
        expr.left()->accept(*this);
        std::string left = result_;
        expr.right()->accept(*this);
        result_ = "(" + left + std::string(expr.op().lexeme()) + result_ + ")";
    }

    void visitUnary(const UnaryExpr& expr) override {
        expr.operand()->accept(*this);
        result_ = std::string(expr.op().lexeme()) + result_;
    }

    void visitAssign(const AssignExpr& expr) override {
        expr.value()->accept(*this);
        result_ = std::string(expr.name().lexeme()) + " = " + result_;
    }

    void visitCall(const CallExpr& expr) override {
        // 获取被调用者
        expr.callee()->accept(*this);
        std::string out = result_ + "(";

        // 添加参数列表：arg->accept 会覆盖 result_，
        // 需先取出再追加，否则会丢失被调用者与已拼接的参数
        bool first = true;
        for (const auto& arg : expr.arguments()) {
            if (!first) out += ", ";
            arg->accept(*this);
            out += result_;
            first = false;
        }
        out += ")";
        result_ = out;
    }

    void visitTuple(const TupleExpr& expr) override {
        // element->accept 会覆盖 result_，需先取出再追加（同 visitCall）
        std::string out = "(";
        bool first = true;
        for (size_t i = 0; i < expr.elements().size(); ++i) {
            if (!first) out += ", ";
            if (!expr.names()[i].empty()) {
                out += expr.names()[i] + ": ";
            }
            expr.elements()[i]->accept(*this);
            out += result_;
            first = false;
        }
        out += ")";
        result_ = out;
    }

    void visitTernary(const TernaryExpr& expr) override {
        result_ += "(";
        expr.condition()->accept(*this);
        result_ += " ? ";
        expr.then_expr()->accept(*this);
        result_ += " : ";
        expr.else_expr()->accept(*this);
        result_ += ")";
    }

    void visitMultiMatch(const MultiMatchExpr& expr) override {
        expr.target()->accept(*this);
        std::string out = "(" + result_ + " ==? ";
        bool first_branch = true;
        for (const auto& branch : expr.branches()) {
            if (!first_branch) out += ", ";
            bool first_value = true;
            for (const auto& value : branch.values) {
                if (!first_value) out += ", ";
                value->accept(*this);
                out += result_;
                first_value = false;
            }
            out += ": ";
            branch.result->accept(*this);
            out += result_;
            first_branch = false;
        }
        if (expr.default_expr() != nullptr) {
            out += ", ";
            expr.default_expr()->accept(*this);
            out += result_;
        }
        out += ")";
        result_ = out;
    }

    void visitArrayLiteral(const ArrayLiteralExpr& expr) override {
        std::string out = "[";
        bool first = true;
        for (const auto& element : expr.elements()) {
            if (!first) out += ", ";
            element->accept(*this);
            out += result_;
            first = false;
        }
        out += "]";
        result_ = out;
    }

    void visitIndex(const IndexExpr& expr) override {
        expr.object()->accept(*this);
        std::string out = result_ + "[";
        expr.index()->accept(*this);
        out += result_ + "]";
        result_ = out;
    }

    void visitIndexAssign(const IndexAssignExpr& expr) override {
        expr.object()->accept(*this);
        std::string out = result_ + "[";
        expr.index()->accept(*this);
        out += result_ + "] = ";
        expr.value()->accept(*this);
        out += result_;
        result_ = out;
    }

    void visitMethodCall(const MethodCallExpr& expr) override {
        expr.object()->accept(*this);
        std::string out = result_ + "." + std::string(expr.name().lexeme()) + "(";
        bool first = true;
        for (const auto& argument : expr.arguments()) {
            if (!first) out += ", ";
            argument->accept(*this);
            out += result_;
            first = false;
        }
        out += ")";
        result_ = out;
    }

    void visitProperty(const PropertyExpr& expr) override {
        expr.object()->accept(*this);
        result_ = result_ + "." + std::string(expr.name().lexeme());
    }

    void visitPropertyAssign(const PropertyAssignExpr& expr) override {
        expr.object()->accept(*this);
        std::string out = result_ + "." + std::string(expr.name().lexeme()) + " = ";
        expr.value()->accept(*this);
        out += result_;
        result_ = out;
    }

    void visitNew(const NewExpr& expr) override {
        std::string out = "new " + std::string(expr.class_name().lexeme()) + "(";
        bool first = true;
        for (const auto& argument : expr.arguments()) {
            if (!first) out += ", ";
            argument->accept(*this);
            out += result_;
            first = false;
        }
        out += ")";
        result_ = out;
    }

    void visitThis(const ThisExpr& /*expr*/) override {
        result_ = "this";
    }

    void visitBaseCall(const BaseCallExpr& expr) override {
        std::string out = "base(";
        bool first = true;
        for (const auto& arg : expr.arguments()) {
            if (!first) out += ", ";
            arg->accept(*this);
            out += result_;
            first = false;
        }
        out += ")";
        result_ = out;
    }

    void visitBaseMethodCall(const BaseMethodCallExpr& expr) override {
        std::string out = "base." + std::string(expr.method().lexeme()) + "(";
        bool first = true;
        for (const auto& arg : expr.arguments()) {
            if (!first) out += ", ";
            arg->accept(*this);
            out += result_;
            first = false;
        }
        out += ")";
        result_ = out;
    }

    std::string result() const { return result_; }

private:
    std::string result_;
};

// 辅助函数：创建一个简单的语句访问者来验证AST结构
class TestStmtVisitor : public StmtVisitor {
public:
    void visitExpression(const ExpressionStmt& stmt) override {
        TestExprVisitor expr_visitor;
        stmt.expression()->accept(expr_visitor);
        last_result_ = expr_visitor.result() + ";";
        result_ = last_result_;
    }

    void visitVarDecl(const VarDeclStmt& stmt) override {
        std::string init = "";
        if (stmt.initializer()) {
            TestExprVisitor expr_visitor;
            stmt.initializer()->accept(expr_visitor);
            init = " = " + expr_visitor.result();
        }
        last_result_ = std::string(stmt.type().lexeme()) + " " +
                      std::string(stmt.name().lexeme()) + init + ";";
        result_ = last_result_;
    }

    void visitBlock(const BlockStmt& stmt) override {
        std::string block = "{\n";
        for (const auto& s : stmt.statements()) {
            s->accept(*this);
            block += indent(last_result_) + "\n";
        }
        block += "}";
        last_result_ = block;
        result_ = block;
    }

    void visitIf(const IfStmt& stmt) override {
        TestExprVisitor expr_visitor;
        stmt.condition()->accept(expr_visitor);
        std::string result = "if (" + expr_visitor.result() + ") ";

        stmt.then_branch()->accept(*this);
        result += last_result_;

        if (stmt.else_branch()) {
            result += " else ";
            stmt.else_branch()->accept(*this);
            result += last_result_;
        }

        last_result_ = result;
        result_ = result;
    }

    void visitWhile(const WhileStmt& stmt) override {
        TestExprVisitor expr_visitor;
        stmt.condition()->accept(expr_visitor);
        std::string result = "while (" + expr_visitor.result() + ") ";

        stmt.body()->accept(*this);
        result += last_result_;

        last_result_ = result;
        result_ = result;
    }

    void visitFor(const ForStmt& stmt) override {
        std::string result = "for (";

        // 初始化部分
        if (stmt.initializer()) {
            stmt.initializer()->accept(*this);
            result += last_result_.substr(0, last_result_.length() - 1); // 移除分号
        }
        result += "; ";

        // 条件部分
        if (stmt.condition()) {
            TestExprVisitor expr_visitor;
            stmt.condition()->accept(expr_visitor);
            result += expr_visitor.result();
        }
        result += "; ";

        // 增量部分
        if (stmt.increment()) {
            TestExprVisitor expr_visitor;
            stmt.increment()->accept(expr_visitor);
            result += expr_visitor.result();
        }
        result += ") ";

        // 循环体
        stmt.body()->accept(*this);
        result += last_result_;

        last_result_ = result;
        result_ = result;
    }

    void visitDoWhile(const DoWhileStmt& stmt) override {
        std::string result = "do ";

        stmt.body()->accept(*this);
        result += last_result_;

        TestExprVisitor expr_visitor;
        stmt.condition()->accept(expr_visitor);
        result += " while (" + expr_visitor.result() + ")";

        last_result_ = result;
        result_ = result;
    }

    void visitSwitch(const SwitchStmt& stmt) override {
        TestExprVisitor expr_visitor;
        stmt.condition()->accept(expr_visitor);
        std::string result = "switch (" + expr_visitor.result() + ") { }";
        last_result_ = result;
        result_ = result;
    }

    void visitFunction(const FunctionStmt& stmt) override {
        // 返回类型和函数名
        std::string result = std::string(stmt.return_type().lexeme()) + " " +
                            std::string(stmt.name().lexeme()) + "(";

        // 参数列表
        bool first = true;
        for (const auto& param : stmt.parameters()) {
            if (!first) result += ", ";
            result += std::string(param.type.lexeme()) + " " +
                     std::string(param.name.lexeme());
            first = false;
        }
        result += ") ";

        // 函数体
        stmt.body()->accept(*this);
        result += last_result_;

        last_result_ = result;
        result_ = result;
    }

    void visitReturn(const ReturnStmt& stmt) override {
        std::string result = "return";
        if (stmt.value()) {
            TestExprVisitor expr_visitor;
            stmt.value()->accept(expr_visitor);
            result += " " + expr_visitor.result();
        }
        result += ";";
        last_result_ = result;
        result_ = result;
    }

    void visitClass(const ClassStmt& stmt) override {
        std::string result = "class " + std::string(stmt.name().lexeme()) + " {\n";
        for (const auto& member : stmt.members()) {
            member->accept(*this);
            result += "  " + last_result_ + "\n";
        }
        result += "}";
        last_result_ = result;
        result_ = result;
    }

    void visitBreak(const BreakStmt& stmt) override {
        last_result_ = "break;";
        result_ = last_result_;
    }

    void visitContinue(const ContinueStmt& stmt) override {
        last_result_ = "continue;";
        result_ = last_result_;
    }

    std::string result() const { return result_; }

private:
    // 将多行文本整体缩进两个空格（每一行行首都加），
    // 使嵌套块能正确体现层级缩进
    static std::string indent(const std::string& text) {
        std::string out = "  ";
        for (char c : text) {
            out += c;
            if (c == '\n') out += "  ";
        }
        return out;
    }

    std::string result_;     // 最终结果
    std::string last_result_; // 最近一次访问的结果
};

// 基本表达式测试
TEST(ParserTest, BasicExpressions) {
    std::string source = "42 + x * 3;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "(42+(x*3));");
}

// 变量声明测试
TEST(ParserTest, VariableDeclaration) {
    std::string source = "number x = 42;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "number x = 42;");
}

// 块语句测试
TEST(ParserTest, BlockStatement) {
    std::string source = "{ number x = 42; x = x + 1; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "{\n  number x = 42;\n  x = (x+1);\n}");
}

// 错误恢复测试
TEST(ParserTest, ErrorRecovery) {
    // number x = ;    // 错误：缺少初始化表达式，应被跳过并恢复
    // number y = 42;  // 这条语句应该能正确解析
    std::string source = "number x = ; number y = 42;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    // parse_program 会跳过出错的第一条声明，恢复后继续解析后续语句
    auto statements = parser.parse_program();
    ASSERT_EQ(statements.size(), 1u);  // 只有第二条语句被成功解析

    TestStmtVisitor visitor;
    statements[0]->accept(visitor);
    EXPECT_EQ(visitor.result(), "number y = 42;");
}

// 复杂表达式测试
TEST(ParserTest, ComplexExpressions) {
    // std::string source = "x = 2 * (3 + 4);";
    std::string source = "x = (a + b) * (c - d);";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    // EXPECT_EQ(visitor.result(), "(x=(2*(3+4)));");
    EXPECT_EQ(visitor.result(), "x = ((a+b)*(c-d));");
}

// if 语句测试
TEST(ParserTest, IfStatement) {
    // std::string source = "if (x > 0) { x = x - 1; } else { x = 0; }";
    std::string source = "if (x > 0) { number y = 42; } else y = 0;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    // EXPECT_EQ(visitor.result(), "if (x>0) {\n  (x-1);\n} else {\n  x = 0;\n}");
    EXPECT_EQ(visitor.result(),
        "if ((x>0)) {\n  number y = 42;\n} else y = 0;");
}

// while 语句测试
TEST(ParserTest, WhileStatement) {
    std::string source = "while (x > 0) { x = x - 1; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "while ((x>0)) {\n  x = (x-1);\n}");
}

// 嵌套 while 语句测试
TEST(ParserTest, NestedWhileStatement) {
    std::string source = R"(
        while (x > 0) {
            while (y > 0) {
                y = y - 1;
            }
            x = x - 1;
        }
    )";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "while ((x>0)) {\n  while ((y>0)) {\n    y = (y-1);\n  }\n  x = (x-1);\n}");
}

// for 语句测试
TEST(ParserTest, ForStatement) {
    std::string source = "for (number i = 0; i < 10; i = i + 1) { x = x + i; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "for (number i = 0; (i<10); i = (i+1)) {\n  x = (x+i);\n}");
}

// for 初始化支持全量类型关键字（t51 修复：旧列表缺 integer/decimal 等）
TEST(ParserTest, ForStatementIntegerInitializer) {
    std::string source = "for (integer i = 0; i < 5; i = i + 1) { x = x + i; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "for (integer i = 0; (i<5); i = (i+1)) {\n  x = (x+i);\n}");
}

// 空 for 语句测试
TEST(ParserTest, EmptyForStatement) {
    // std::string source = "for (;;) { x = x + 1; }";
    std::string source = "for (;;) { break; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "for (; ; ) {\n  break;\n}");
}

// 函数声明测试
TEST(ParserTest, FunctionDeclaration) {
    // 当前 parser 采用 function 关键字文法：
    //   function 名(参数名 参数类型, ...) 返回类型 { ... }
    // 参数类型与返回类型均为标识符（自定义类型名）。
    std::string source = "function add(a Num, b Num) Num { return a + b; }";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "Num add(Num a, Num b) {\n  return (a+b);\n}");
}

// 函数调用测试
TEST(ParserTest, FunctionCall) {
    std::string source = "add(1, 2 * 3);";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "add(1, (2*3));");
}

// 嵌套函数调用测试
TEST(ParserTest, NestedFunctionCall) {
    std::string source = "print(add(1, mul(2, 3)));";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "print(add(1, mul(2, 3)));");
}

/**
 * @brief 测试 break 和 continue 语句的解析
 */
TEST(ParserTest, BreakContinueStatements) {
    // break 语句测试
    {
        std::string source = "while (true) { break; }";
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);

        auto stmt = parser.parse();
        ASSERT_NE(stmt, nullptr);

        TestStmtVisitor visitor;
        stmt->accept(visitor);
        EXPECT_EQ(visitor.result(), "while (true) {\n  break;\n}");
    }

    // continue 语句测试
    {
        std::string source = "while (true) { continue; }";
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);

        auto stmt = parser.parse();
        ASSERT_NE(stmt, nullptr);

        TestStmtVisitor visitor;
        stmt->accept(visitor);
        EXPECT_EQ(visitor.result(), "while (true) {\n  continue;\n}");
    }

    // 测试在循环内使用 break
    {
        std::string source = R"(
            while (true) {
                if (x > 10) break;
            }
        )";
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);

        auto stmt = parser.parse();
        ASSERT_NE(stmt, nullptr);

        auto* while_stmt = dynamic_cast<WhileStmt*>(stmt.get());
        ASSERT_NE(while_stmt, nullptr);

        auto* block = dynamic_cast<const BlockStmt*>(while_stmt->body());
        ASSERT_NE(block, nullptr);
        ASSERT_EQ(block->statements().size(), 1);

        auto* if_stmt = dynamic_cast<const IfStmt*>(block->statements()[0].get());
        ASSERT_NE(if_stmt, nullptr);

        auto* break_stmt = dynamic_cast<const BreakStmt*>(if_stmt->then_branch());
        ASSERT_NE(break_stmt, nullptr);
    }

    // 测试在循环内使用 continue
    {
        std::string source = R"(
            for (number i = 0; i < 10; i = i + 1) {
                if (i % 2 == 0) continue;
                print(i);
            }
        )";
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);

        auto stmt = parser.parse();
        ASSERT_NE(stmt, nullptr);

        auto* for_stmt = dynamic_cast<ForStmt*>(stmt.get());
        ASSERT_NE(for_stmt, nullptr);

        auto* block = dynamic_cast<const BlockStmt*>(for_stmt->body());
        ASSERT_NE(block, nullptr);
        ASSERT_EQ(block->statements().size(), 2);

        auto* if_stmt = dynamic_cast<const IfStmt*>(block->statements()[0].get());
        ASSERT_NE(if_stmt, nullptr);

        auto* continue_stmt = dynamic_cast<const ContinueStmt*>(if_stmt->then_branch());
        ASSERT_NE(continue_stmt, nullptr);
    }

    // 循环外使用 break：parser 层仅做语法解析，语法上接受并产出 BreakStmt。
    // “循环外 break/continue” 属于上下文/语义约束，由语义分析器检测并报错，
    // 相应的负面用例放在语义分析测试中，这里只验证 parser 不误报语法错误。
    {
        std::string source = "break;";
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);

        auto stmt = parser.parse();
        ASSERT_NE(stmt, nullptr);
        EXPECT_NE(dynamic_cast<BreakStmt*>(stmt.get()), nullptr);
    }

    // 循环外使用 continue：同上，parser 层语法接受，语义层负责报错。
    {
        std::string source = "continue;";
        Lexer lexer(source);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);

        auto stmt = parser.parse();
        ASSERT_NE(stmt, nullptr);
        EXPECT_NE(dynamic_cast<ContinueStmt*>(stmt.get()), nullptr);
    }
}

// parse_program 对含语法错误的源码采用错误恢复：记录错误、跳过出错语句，
// 仍返回已成功解析的部分 AST（不抛异常）。main.cpp 依赖 get_errors() 做门禁，
// 防止“报完 Parse error 仍运行正确那部分”。
TEST(ParserTest, ParseProgramRecordsErrorsAndReturnsPartialAst) {
    // 前两条合法，第三条 `object object = none;` 语法错误（object 是关键字，不能作变量名）
    std::string source = "number a = 42;\nprint(a);\nobject object = none;";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmts = parser.parse_program();

    // 记录了语法错误（不静默失败）
    EXPECT_FALSE(parser.get_errors().empty());
    // 但仍返回了前面成功解析的两条语句（部分 AST），而非抛异常或返回空
    EXPECT_EQ(stmts.size(), 2u);
}

// 合法程序：parse_program 不应产生任何错误（作为门禁无误报的对照）。
TEST(ParserTest, ParseProgramNoErrorsForValidSource) {
    std::string source = "number a = 42;\nprint(a);";
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto stmts = parser.parse_program();

    EXPECT_TRUE(parser.get_errors().empty());
    EXPECT_EQ(stmts.size(), 2u);
}

#ifdef _WIN32
void SetupWindowsConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
}
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    SetupWindowsConsole();
#elif defined(__linux__) || defined(__APPLE__)
    setlocale(LC_ALL, "");
#endif

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
