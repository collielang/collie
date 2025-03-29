/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#ifndef COLLIE_TEST_UTILS_H
#define COLLIE_TEST_UTILS_H

#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "../parser/ast.h"
#include "../parser/parser.h"
#include "../lexer/lexer.h"
#include "../lexer/token.h"

namespace collie {
namespace test {

/**
 * @brief 获取当前进程的内存使用量
 * @return 当前使用的内存字节数
 */
size_t getCurrentMemoryUsage();

/**
 * @brief 解析源代码并返回AST和tokens
 * @param source 源代码字符串
 * @return 包含AST和tokens的pair
 */
std::pair<std::vector<std::unique_ptr<Stmt>>, std::vector<Token>>
parse_and_get_tokens(const std::string& source);

} // namespace test
} // namespace collie

#endif // COLLIE_TEST_UTILS_H
