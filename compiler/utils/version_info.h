/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-07
 * @Description: 编译器版本信息工具类
 */

#ifndef COLLIE_VERSION_INFO_H
#define COLLIE_VERSION_INFO_H

#include <string>

namespace collie {
namespace utils {

/**
 * @brief 获取编译器版本信息
 * @return 版本信息字符串
 */
std::string get_version_info();

/**
 * @brief 获取编译器环境信息
 * @return 环境信息字符串
 */
std::string get_environment_info();

} // namespace utils
} // namespace collie

#endif // COLLIE_VERSION_INFO_H