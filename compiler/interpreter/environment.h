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

namespace collie {

/**
 * @brief 变量作用域链
 *
 * 用作用域栈实现变量的声明、读取与赋值。内层作用域可遮蔽外层的同名变量；
 * 读取/赋值按从内到外的顺序查找。
 */
class Environment {
public:
    Environment() { push_scope(); }  // 全局作用域

    void push_scope() { scopes_.emplace_back(); }
    void pop_scope() {
        if (scopes_.size() > 1) scopes_.pop_back();
    }

    /// @brief 在当前作用域声明变量（允许遮蔽外层同名变量）
    void define(const std::string& name, const Value& value) {
        scopes_.back()[name] = value;
    }

    /// @brief 从内到外查找变量，找到返回其指针，否则返回 nullptr
    Value* get(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

    /// @brief 赋值到最近作用域中已声明的同名变量，成功返回 true
    bool assign(const std::string& name, const Value& value) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                found->second = value;
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::unordered_map<std::string, Value>> scopes_;
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
