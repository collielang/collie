/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#ifndef COLLIE_TEST_UTILS_H
#define COLLIE_TEST_UTILS_H

#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace collie {
namespace test {

/**
 * @brief 获取当前进程的内存使用量
 * @return 当前使用的内存字节数
 */
size_t getCurrentMemoryUsage();

} // namespace test
} // namespace collie

#endif // COLLIE_TEST_UTILS_H