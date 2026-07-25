/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2026-07-25
 * @Description: 树遍历解释器的运行期值类型
 */
#ifndef COLLIE_INTERPRETER_VALUE_H
#define COLLIE_INTERPRETER_VALUE_H

#include <string>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>

namespace collie {

// 前置声明（避免包含完整 ast.h）
class FunctionStmt;

/**
 * @brief 解释器运行期的值
 *
 * v1 支持五种值：none（空）、bool、number、string、function。
 * TODO(interpreter): Collie 语言区分 integer/decimal 等数值子类型，
 *   目前解释器统一用 double 承载 number，暂不区分整型/浮点型的溢出与精度语义。
 */
class Value {
public:
    enum class Kind { None, Bool, Number, String, Function };

    Value() : kind_(Kind::None) {}

    static Value none() { return Value(); }
    static Value boolean(bool b) {
        Value v; v.kind_ = Kind::Bool; v.bool_ = b; return v;
    }
    static Value number(double n) {
        Value v; v.kind_ = Kind::Number; v.num_ = n; return v;
    }
    static Value str(std::string s) {
        Value v; v.kind_ = Kind::String; v.str_ = std::move(s); return v;
    }
    static Value function(const FunctionStmt* fn) {
        Value v; v.kind_ = Kind::Function; v.fn_ = fn; return v;
    }

    Kind kind() const { return kind_; }
    bool is_none() const { return kind_ == Kind::None; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_number() const { return kind_ == Kind::Number; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_function() const { return kind_ == Kind::Function; }

    bool as_bool() const { return bool_; }
    double as_number() const { return num_; }
    const std::string& as_string() const { return str_; }
    const FunctionStmt* as_function() const { return fn_; }

    /**
     * @brief 真值判断
     * none -> false；bool -> 原值；number -> 非零为真；string -> 非空为真。
     */
    bool is_truthy() const {
        switch (kind_) {
            case Kind::None:     return false;
            case Kind::Bool:     return bool_;
            case Kind::Number:   return num_ != 0.0;
            case Kind::String:   return !str_.empty();
            case Kind::Function: return true;  // 函数值始终为真
        }
        return false;
    }

    /**
     * @brief 转为用于输出/拼接的字符串表示
     * 整数值不显示小数点；其余数字用默认浮点格式。
     */
    std::string to_string() const {
        switch (kind_) {
            case Kind::None:     return "none";
            case Kind::Bool:     return bool_ ? "true" : "false";
            case Kind::String:   return str_;
            case Kind::Function: return "<function>";
            case Kind::Number: {
                if (std::isfinite(num_) && num_ == std::floor(num_) &&
                    std::fabs(num_) < 1e15) {
                    return std::to_string(static_cast<long long>(num_));
                }
                std::ostringstream oss;
                oss << num_;
                return oss.str();
            }
        }
        return "";
    }

    const char* kind_name() const {
        switch (kind_) {
            case Kind::None:     return "none";
            case Kind::Bool:     return "bool";
            case Kind::Number:   return "number";
            case Kind::String:   return "string";
            case Kind::Function: return "function";
        }
        return "unknown";
    }

private:
    Kind kind_;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    const FunctionStmt* fn_ = nullptr;
};

} // namespace collie

#endif // COLLIE_INTERPRETER_VALUE_H
