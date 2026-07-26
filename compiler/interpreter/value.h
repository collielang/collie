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
#include <memory>
#include <sstream>
#include <vector>

namespace collie {

// 前置声明（避免包含完整 ast.h）
class FunctionStmt;

/**
 * @brief 解释器运行期的值
 *
 * v1 支持六种值：none（空）、bool、number、string、function、array。
 * 数组为引用语义（shared_ptr 共享底层存储），赋值/传参共享同一数组。
 * TODO(interpreter): Collie 语言区分 integer/decimal 等数值子类型，
 *   目前解释器统一用 double 承载 number，暂不区分整型/浮点型的溢出与精度语义。
 */
class Value {
public:
    enum class Kind { None, Bool, Number, String, Function, Array };

    using ArrayStorage = std::vector<Value>;

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
    static Value array(ArrayStorage elements) {
        Value v;
        v.kind_ = Kind::Array;
        v.arr_ = std::make_shared<ArrayStorage>(std::move(elements));
        return v;
    }

    Kind kind() const { return kind_; }
    bool is_none() const { return kind_ == Kind::None; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_number() const { return kind_ == Kind::Number; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_function() const { return kind_ == Kind::Function; }
    bool is_array() const { return kind_ == Kind::Array; }

    bool as_bool() const { return bool_; }
    double as_number() const { return num_; }
    const std::string& as_string() const { return str_; }
    const FunctionStmt* as_function() const { return fn_; }
    ArrayStorage& as_array() { return *arr_; }
    const ArrayStorage& as_array() const { return *arr_; }

    /**
     * @brief 真值判断
     * none -> false；bool -> 原值；number -> 非零为真；string -> 非空为真；
     * array -> 非空为真。
     */
    bool is_truthy() const {
        switch (kind_) {
            case Kind::None:     return false;
            case Kind::Bool:     return bool_;
            case Kind::Number:   return num_ != 0.0;
            case Kind::String:   return !str_.empty();
            case Kind::Function: return true;  // 函数值始终为真
            case Kind::Array:    return arr_ && !arr_->empty();
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
                // 特殊数值按文档格式输出（见 04-numeric.md）：
                // NaN / +Infinity / -Infinity
                if (std::isnan(num_)) return "NaN";
                if (std::isinf(num_)) return num_ > 0 ? "+Infinity" : "-Infinity";
                if (num_ == std::floor(num_) && std::fabs(num_) < 1e15) {
                    return std::to_string(static_cast<long long>(num_));
                }
                std::ostringstream oss;
                oss << num_;
                return oss.str();
            }
            case Kind::Array: {
                // 元素递归转字符串，形如 [1, 2, 3]
                std::string out = "[";
                bool first = true;
                for (const Value& element : *arr_) {
                    if (!first) out += ", ";
                    out += element.to_string();
                    first = false;
                }
                out += "]";
                return out;
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
            case Kind::Array:    return "array";
        }
        return "unknown";
    }

private:
    Kind kind_;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    const FunctionStmt* fn_ = nullptr;
    std::shared_ptr<ArrayStorage> arr_;  ///< 数组存储（引用语义，赋值共享）
};

} // namespace collie

#endif // COLLIE_INTERPRETER_VALUE_H
