/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#ifndef COLLIE_TOKEN_UTILS_H
#define COLLIE_TOKEN_UTILS_H

#include <string>
#include "../lexer/token.h"

namespace collie {

/**
 * @brief 将TokenType转换为字符串
 * @param type TokenType枚举值
 * @return 对应的字符串表示
 */
std::string token_type_to_string(TokenType type);

} // namespace collie

#endif // COLLIE_TOKEN_UTILS_H