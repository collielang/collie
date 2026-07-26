/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2026-07-25
 * @Description: 解释器的变量作用域链
 */
#ifndef COLLIE_INTERPRETER_ENVIRONMENT_H
#define COLLIE_INTERPRETER_ENVIRONMENT_H

#include <string>
#include <unordered_map>
#include <vector>
#include "value.h"
#include "../lexer/token.h"

namespace collie {

/**
 * @brief 变量作用域链
 *
 * 用作用域栈实现变量的声明、读取与赋值。内层作用域可遮蔽外层的同名变量；
 * 读取/赋值按从内到外的顺序查找。支持 const 变量保护与声明类型记录。
 */
class Environment {
public:
    Environment() { push_scope(); }  // 全局作用域

    void push_scope() { scopes_.emplace_back(); }
    void pop_scope() {
        if (scopes_.size() > 1) scopes_.pop_back();
    }

    /// @brief 在当前作用域声明变量（允许遮蔽外层同名变量）
    /// declared_type 缺省 KW_OBJECT 表示动态类型（不做运行期校验）
    void define(const std::string& name, const Value& value, bool is_const = false,
                TokenType declared_type = TokenType::KW_OBJECT) {
        scopes_.back()[name] = Binding{value, is_const, declared_type};
    }

    /// @brief 从内到外查找变量，找到返回其值指针，否则返回 nullptr
    Value* get(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second.value;
        }
        return nullptr;
    }

    /// @brief 检查变量是否为 const（从内到外查找）
    bool is_const(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second.is_const;
        }
        return false;
    }

    /// @brief 查询变量的声明类型（从内到外查找；未声明返回 KW_OBJECT 动态放行）
    TokenType declared_type(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second.declared_type;
        }
        return TokenType::KW_OBJECT;
    }

    /// @brief 赋值到最近作用域中已声明的同名变量，成功返回 true
    bool assign(const std::string& name, const Value& value) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                found->second.value = value;
                return true;
            }
        }
        return false;
    }

private:
    struct Binding {
        Value value;
        bool is_const = false;
        TokenType declared_type = TokenType::KW_OBJECT;
    };
    std::vector<std::unordered_map<std::string, Binding>> scopes_;
};

/**
 * @brief RAII 作用域守卫：构造时进入新作用域，析构时退出。
 */
class ScopeGuard {
public:
    explicit ScopeGuard(Environment& env) : env_(env) { env_.push_scope(); }
    ~ScopeGuard() { env_.pop_scope(); }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    Environment& env_;
};

} // namespace collie

#endif // COLLIE_INTERPRETER_ENVIRONMENT_H
