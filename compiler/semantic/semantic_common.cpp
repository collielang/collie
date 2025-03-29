/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "semantic_common.h"
#include <sstream>
#include "../utils/token_utils.h"

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
        oss << token_type_to_string(parameter_types[i]);
    }

    oss << ") -> " << token_type_to_string(return_type);
    return oss.str();
}

} // namespace collie
