/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include <gtest/gtest.h>
#include "../semantic/symbol_table.h"

using namespace collie;

// 辅助函数：创建一个变量符号
Symbol create_variable(const std::string& name, TokenType type_token = TokenType::KW_NUMBER) {
    return Symbol{
        SymbolKind::VARIABLE,
        Token(type_token, "", 0, 0),  // 类型
        Token(TokenType::IDENTIFIER, name, 0, 0),  // 名称
        0,  // 作用域层级会在定义时设置
        false  // 初始状态未初始化
    };
}

// 辅助函数：创建一个函数符号
Symbol create_function(const std::string& name, const std::vector<Symbol>& params = {}) {
    return Symbol{
        SymbolKind::FUNCTION,
        Token(TokenType::KW_NUMBER, "", 0, 0),  // 返回类型
        Token(TokenType::IDENTIFIER, name, 0, 0),  // 名称
        0,  // 作用域层级会在定义时设置
        true,  // 函数定义时就认为是已初始化的
        params  // 参数列表
    };
}

// 基本作用域测试
TEST(SymbolTableTest, BasicScope) {
    SymbolTable table;

    // 测试全局作用域
    EXPECT_EQ(table.current_scope_level(), 0);

    // 添加一个变量
    auto x = create_variable("x");
    table.define(x);

    // 验证变量存在
    auto* symbol = table.resolve("x");
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->name.lexeme(), "x");
    EXPECT_EQ(symbol->kind, SymbolKind::VARIABLE);
}

// 嵌套作用域测试
TEST(SymbolTableTest, NestedScope) {
    SymbolTable table;

    // 全局作用域定义变量 x
    table.define(create_variable("x"));

    // 进入新作用域
    table.begin_scope();
    EXPECT_EQ(table.current_scope_level(), 1);

    // 在新作用域定义同名变量
    table.define(create_variable("x"));

    // 验证当前作用域的变量遮蔽了外层作用域的变量
    auto* inner_x = table.resolve("x");
    ASSERT_NE(inner_x, nullptr);
    EXPECT_EQ(inner_x->scope_level, 1);

    // 退出作用域
    table.end_scope();

    // 验证外层作用域的变量仍然存在
    auto* outer_x = table.resolve("x");
    ASSERT_NE(outer_x, nullptr);
    EXPECT_EQ(outer_x->scope_level, 0);
}

// 函数作用域测试
TEST(SymbolTableTest, FunctionScope) {
    SymbolTable table;

    // 定义函数
    std::vector<Symbol> params = {
        Symbol{SymbolKind::PARAMETER,
               Token(TokenType::KW_NUMBER, "", 0, 0),
               Token(TokenType::IDENTIFIER, "a", 0, 0),
               0, true}
    };
    table.define(create_function("func", params));

    // 验证函数定义
    auto* func = table.resolve("func");
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->kind, SymbolKind::FUNCTION);
    EXPECT_EQ(func->parameters.size(), 1);
    EXPECT_EQ(func->parameters[0].name.lexeme(), "a");
}

// 变量初始化状态测试
TEST(SymbolTableTest, VariableInitialization) {
    SymbolTable table;

    // 定义未初始化的变量
    auto x = create_variable("x");
    EXPECT_FALSE(x.is_initialized);
    table.define(x);

    // 查找并验证状态
    auto* symbol = table.resolve("x");
    ASSERT_NE(symbol, nullptr);
    EXPECT_FALSE(symbol->is_initialized);

    // 修改初始化状态
    symbol->is_initialized = true;
    EXPECT_TRUE(table.resolve("x")->is_initialized);
}

// 重复定义测试
TEST(SymbolTableTest, DuplicateDefinition) {
    SymbolTable table;

    // 在同一作用域中重复定义变量
    table.define(create_variable("x"));
    EXPECT_TRUE(table.is_defined_in_current_scope("x"));

    // 再次定义同名变量
    table.define(create_variable("x"));

    // 验证只有一个定义存在
    auto* symbol = table.resolve("x");
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->scope_level, 0);
}
