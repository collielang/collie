/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include <gtest/gtest.h>
#include <memory>
#include "../parser/parser.h"

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
        std::string callee = result_;

        // 添加参数列表
        result_ = callee + "(";
        bool first = true;
        for (const auto& arg : expr.arguments()) {
            if (!first) result_ += ", ";
            arg->accept(*this);
            first = false;
        }
        result_ += ")";
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
            block += "  " + last_result_ + "\n";
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

    std::string result() const { return result_; }

private:
    std::string result_;     // 最终结果
    std::string last_result_; // 最近一次访问的结果
};

// 基本表达式测试
TEST(ParserTest, BasicExpressions) {
    std::string source = "42 + x * 3;";
    Lexer lexer(source);
    Parser parser(lexer);

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
    Parser parser(lexer);

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
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "{\n  number x = 42;\n  (x+1);\n}");
}

// 错误恢复测试
TEST(ParserTest, ErrorRecovery) {
    // number x = ;  // 错误：缺少初始化表达式
    // number y = 42;  // 这条语句应该能正确解析
    std::string source = "number x = ; number y = 42;";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);  // 应该返回第二条语句

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "number y = 42;");
}

// 复杂表达式测试
TEST(ParserTest, ComplexExpressions) {
    std::string source = "x = 2 * (3 + 4);";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "(x=(2*(3+4)));");
}

// 添加 if 语句测试
TEST(ParserTest, IfStatement) {
    std::string source = "if (x > 0) { number y = 42; } else y = 0;";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "if ((x>0)) {\n  number y = 42;\n} else y = 0;");
}

// 添加 while 语句测试
TEST(ParserTest, WhileStatement) {
    std::string source = "while (x > 0) { x = x - 1; }";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "while ((x>0)) {\n  x = (x-1);\n}");  // 修改期望输出
}

// 添加嵌套循环测试
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
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "while ((x>0)) {\n  while ((y>0)) {\n    y = (y-1);\n  }\n  x = (x-1);\n}");  // 修改期望输出
}

// 添加 for 语句测试
TEST(ParserTest, ForStatement) {
    std::string source = "for (number i = 0; i < 10; i = i + 1) { x = x + i; }";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "for (number i = 0; (i<10); i = (i+1)) {\n  x = (x+i);\n}");
}

// 添加空条件 for 语句测试
TEST(ParserTest, EmptyForStatement) {
    std::string source = "for (;;) { x = x + 1; }";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "for (; ; ) {\n  x = (x+1);\n}");
}

// 添加函数声明测试
TEST(ParserTest, FunctionDeclaration) {
    std::string source = "number add(number x, number y) { return x + y; }";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(),
        "number add(number x, number y) {\n  return (x+y);\n}");
}

// 添加函数调用测试
TEST(ParserTest, FunctionCall) {
    std::string source = "add(1, 2 * 3);";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "add(1, (2*3));");
}

// 添加嵌套函数调用测试
TEST(ParserTest, NestedFunctionCall) {
    std::string source = "print(add(1, 2), mul(3, 4));";
    Lexer lexer(source);
    Parser parser(lexer);

    auto stmt = parser.parse();
    ASSERT_NE(stmt, nullptr);

    TestStmtVisitor visitor;
    stmt->accept(visitor);
    EXPECT_EQ(visitor.result(), "print(add(1, 2), mul(3, 4));");
}