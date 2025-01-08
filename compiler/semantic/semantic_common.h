/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语义分析器的公共定义
 */
#ifndef COLLIE_SEMANTIC_COMMON_H
#define COLLIE_SEMANTIC_COMMON_H

#include <string>
#include <vector>
#include <stdexcept>
#include "../lexer/token.h"

namespace collie {

/**
 * @brief 语义错误异常类
 */
class SemanticError : public std::runtime_error {
public:
    SemanticError(const std::string& message, size_t line, size_t column)
        : std::runtime_error(message), line_(line), column_(column) {
        error_message_ = "Line " + std::to_string(line) +
                        ", Column " + std::to_string(column) +
                        ": " + message;
    }

    const char* what() const noexcept override {
        return error_message_.c_str();
    }

    size_t line() const { return line_; }
    size_t column() const { return column_; }

private:
    size_t line_;
    size_t column_;
    std::string error_message_;
};

// 错误恢复相关常量
constexpr size_t MAX_ERRORS = 100;         // 最大错误数量
constexpr size_t MAX_NESTING_DEPTH = 256;  // 最大嵌套深度

// 类型转换优先级
enum class ConversionRank {
    EXACT_MATCH = 2,     // 精确匹配
    PROMOTION = 1,       // 类型提升
    CONVERSION = 0,      // 需要转换
    NO_CONVERSION = -1   // 不能转换
};

// 类型检查结果
struct TypeCheckResult {
    bool success;
    TokenType type;
    std::string error_message;
};

// 函数签名
struct FunctionSignature {
    std::string name;
    std::vector<TokenType> parameter_types;
    TokenType return_type;

    bool operator==(const FunctionSignature& other) const;
    std::string to_string() const;
};

} // namespace collie

#endif // COLLIE_SEMANTIC_COMMON_H