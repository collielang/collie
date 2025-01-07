/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 符号表的定义，用于管理变量和函数的作用域及类型信息
 */
#ifndef COLLIE_SYMBOL_TABLE_H
#define COLLIE_SYMBOL_TABLE_H

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include "../lexer/token.h"

namespace collie {

// 符号类型
enum class SymbolKind {
    VARIABLE,    // 变量
    FUNCTION,    // 函数
    PARAMETER    // 函数参数
};

// 符号信息
struct Symbol {
    SymbolKind kind;          // 符号类型
    Token type;               // 数据类型
    Token name;               // 符号名称
    size_t scope_level;       // 作用域层级
    bool is_initialized;      // 是否已初始化
    bool is_const;           // 是否是常量
    bool is_modified;        // 是否被修改过

    // 函数特有信息
    std::vector<Symbol> parameters;  // 函数参数列表

    // 添加默认构造函数
    Symbol() : kind(SymbolKind::VARIABLE), scope_level(0),
              is_initialized(false), is_const(false), is_modified(false) {}

    // 添加完整的构造函数
    Symbol(SymbolKind k, Token t, Token n, size_t level, bool init,
           bool is_const = false, const std::vector<Symbol>& params = {})
        : kind(k), type(t), name(n), scope_level(level),
          is_initialized(init), is_const(is_const), is_modified(false),
          parameters(params) {}
};

// 作用域
class Scope {
public:
    explicit Scope(size_t level) : level_(level) {}

    // 添加符号
    void define(const Symbol& symbol);

    // 查找符号
    Symbol* resolve(const std::string& name);

    // 判断符号是否已在当前作用域中定义
    bool is_defined(const std::string& name) const;

private:
    size_t level_;  // 作用域层级
    std::unordered_map<std::string, Symbol> symbols_;  // 符号表
};

// 符号表
class SymbolTable {
public:
    SymbolTable() { begin_scope(); }  // 创建全局作用域

    // 作用域管理
    void begin_scope();  // 进入新作用域
    void end_scope();    // 退出当前作用域

    // 符号管理
    void define(const Symbol& symbol);  // 定义新符号
    Symbol* resolve(const std::string& name);  // 查找符号
    bool is_defined_in_current_scope(const std::string& name) const;  // 检查当前作用域

    // 获取当前作用域层级
    size_t current_scope_level() const { return scopes_.size() - 1; }

private:
    std::vector<Scope> scopes_;  // 作用域栈
};

} // namespace collie

#endif // COLLIE_SYMBOL_TABLE_H