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
#include <unordered_map>
#include <vector>

#include "big_int.h"

namespace collie {

// 前置声明（避免包含完整 ast.h）
class FunctionStmt;
class ClassStmt;
struct InstanceData;  // 定义在 Value 之后（字段表持有 Value）

/**
 * @brief 解释器运行期的值
 *
 * v1 支持八种值：none（空）、bool、tribool、number、string、function、array、instance。
 * 数组与类实例为引用语义（shared_ptr 共享底层存储），赋值/传参共享同一对象。
 * number 内部双表示（t42，经作者确认的 Python 式设计）：整数值用 BigInt
 * 任意精度承载（自动扩容，无溢出），小数值用 double（IEEE 754）；
 * 静态类型 number 是 integer/decimal 的超类型，两种表示都算 is_number()。
 * tribool 为三态布尔（t43，经作者确认）：true/false/unset，与 bool 独立。
 */
class Value {
public:
    enum class Kind { None, Bool, Tribool, Number, String, Function, Array, Instance };

    /// tribool 的三态；数值编码满足 Kleene 逻辑 AND=min、OR=max
    enum class Tri : uint8_t { False = 0, Unset = 1, True = 2 };

    using ArrayStorage = std::vector<Value>;

    Value() : kind_(Kind::None) {}

    static Value none() { return Value(); }
    static Value boolean(bool b) {
        Value v; v.kind_ = Kind::Bool; v.bool_ = b; return v;
    }
    static Value tribool(Tri t) {
        Value v; v.kind_ = Kind::Tribool; v.tri_ = t; return v;
    }
    static Value number(double n) {
        Value v; v.kind_ = Kind::Number; v.num_ = n; return v;
    }
    /// 整数值（BigInt 任意精度承载；打印/精确算术走整数路径）
    static Value integer(BigInt n) {
        Value v;
        v.kind_ = Kind::Number;
        v.num_is_int_ = true;
        v.big_ = std::move(n);
        return v;
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
    static Value instance(std::shared_ptr<InstanceData> data) {
        Value v; v.kind_ = Kind::Instance; v.inst_ = std::move(data); return v;
    }

    Kind kind() const { return kind_; }
    bool is_none() const { return kind_ == Kind::None; }
    bool is_bool() const { return kind_ == Kind::Bool; }
    bool is_tribool() const { return kind_ == Kind::Tribool; }
    bool is_number() const { return kind_ == Kind::Number; }
    /// number 且内部为整数表示（integer 类型值）
    bool is_integer_value() const { return kind_ == Kind::Number && num_is_int_; }
    /// number 且内部为小数表示（decimal 类型值）
    bool is_decimal_value() const { return kind_ == Kind::Number && !num_is_int_; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_function() const { return kind_ == Kind::Function; }
    bool is_array() const { return kind_ == Kind::Array; }
    bool is_instance() const { return kind_ == Kind::Instance; }

    bool as_bool() const { return bool_; }
    Tri as_tribool() const { return tri_; }
    /// 数值的 double 视图：整数表示时转 double（超大整数有精度损失，
    /// 仅供混合算术/比较等小数路径使用；精确路径用 as_integer）
    double as_number() const { return num_is_int_ ? big_.to_double() : num_; }
    /// 整数表示的 BigInt（仅当 is_integer_value() 时有效）
    const BigInt& as_integer() const { return big_; }
    const std::string& as_string() const { return str_; }
    const FunctionStmt* as_function() const { return fn_; }
    ArrayStorage& as_array() { return *arr_; }
    const ArrayStorage& as_array() const { return *arr_; }
    InstanceData& as_instance() { return *inst_; }
    const InstanceData& as_instance() const { return *inst_; }

    /**
     * @brief 真值判断
     * none -> false；bool -> 原值；tribool -> 仅 true 为真（unset 为假，见文档
     * 两分支三元 unset 走 false 分支）；number -> 非零为真；string -> 非空为真；
     * array -> 非空为真。
     */
    bool is_truthy() const {
        switch (kind_) {
            case Kind::None:     return false;
            case Kind::Bool:     return bool_;
            case Kind::Tribool:  return tri_ == Tri::True;
            case Kind::Number:   return num_is_int_ ? !big_.is_zero() : num_ != 0.0;
            case Kind::String:   return !str_.empty();
            case Kind::Function: return true;  // 函数值始终为真
            case Kind::Array:    return arr_ && !arr_->empty();
            case Kind::Instance: return true;  // 类实例始终为真
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
            case Kind::Tribool:
                return tri_ == Tri::True ? "true" : (tri_ == Tri::False ? "false" : "unset");
            case Kind::String:   return str_;
            case Kind::Function: return "<function>";
            case Kind::Number: {
                // 整数表示：BigInt 精确打印（任意位数不丢精度）
                if (num_is_int_) return big_.to_string();
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
            case Kind::Instance: return "<object>";
        }
        return "";
    }

    const char* kind_name() const {
        switch (kind_) {
            case Kind::None:     return "none";
            case Kind::Bool:     return "bool";
            case Kind::Tribool:  return "tribool";
            case Kind::Number:   return "number";
            case Kind::String:   return "string";
            case Kind::Function: return "function";
            case Kind::Array:    return "array";
            case Kind::Instance: return "object";
        }
        return "unknown";
    }

private:
    Kind kind_;
    bool bool_ = false;
    Tri tri_ = Tri::Unset;     ///< tribool 三态（缺省 unset，见 draft.md）
    bool num_is_int_ = false;  ///< number 的内部表示：true=BigInt 整数，false=double 小数
    double num_ = 0.0;
    BigInt big_;               ///< 整数表示（num_is_int_ 为 true 时有效）
    std::string str_;
    const FunctionStmt* fn_ = nullptr;
    std::shared_ptr<ArrayStorage> arr_;  ///< 数组存储（引用语义，赋值共享）
    std::shared_ptr<InstanceData> inst_; ///< 类实例存储（引用语义，赋值共享）
};

/**
 * @brief 类实例的底层存储：所属类的 AST 节点 + 字段表
 */
struct InstanceData {
    const ClassStmt* klass = nullptr;               ///< 所属类声明节点
    std::unordered_map<std::string, Value> fields;  ///< 字段名 -> 字段值
};

} // namespace collie

#endif // COLLIE_INTERPRETER_VALUE_H
