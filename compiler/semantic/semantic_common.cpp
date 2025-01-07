/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 * @Description: 语义分析器公共定义的实现
 */
#include "semantic_common.h"
#include <sstream>

namespace collie {

bool FunctionSignature::operator==(const FunctionSignature& other) const {
    if (name != other.name || return_type != other.return_type) {
        return false;
    }

    if (parameter_types.size() != other.parameter_types.size()) {
        return false;
    }

    for (size_t i = 0; i < parameter_types.size(); ++i) {
        if (parameter_types[i] != other.parameter_types[i]) {
            return false;
        }
    }

    return true;
}

std::string FunctionSignature::to_string() const {
    std::ostringstream oss;
    oss << name << "(";

    for (size_t i = 0; i < parameter_types.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        // TODO: 添加类型到字符串的转换
        oss << static_cast<int>(parameter_types[i]);
    }

    oss << ") -> " << static_cast<int>(return_type);
    return oss.str();
}

} // namespace collie