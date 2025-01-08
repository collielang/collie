/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-07
 * @Description: 编译器版本信息工具类实现
 */

#include "version_info.h"
#include <sstream>

namespace collie {
namespace utils {

std::string get_version_info() {
    std::stringstream ss;
    ss << "Collie Programming Language Compiler v0.1.0" << std::endl;
    ss << "Copyright (c) 2025 Zhang Bokai. All rights reserved." << std::endl;
    return ss.str();
}

std::string get_environment_info() {
    std::stringstream ss;

    // 编译器版本信息
    #ifdef _MSC_VER
        ss << "MSVC Version: " << _MSC_VER << std::endl;
    #endif
    #ifdef __GNUC__
        ss << "GCC Version: " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__ << std::endl;
    #endif
    #ifdef __clang__
        ss << "Clang Version: " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__ << std::endl;
    #endif

    // 操作系统信息
    ss << "OS: ";
    #ifdef _WIN32
        ss << "Windows";
    #elif __linux__
        ss << "Linux";
    #elif __APPLE__
        ss << "macOS";
    #else
        ss << "Unknown";
    #endif
    ss << std::endl;

    // 架构信息
    ss << "Architecture: ";
    #ifdef _WIN64
        ss << "x64";
    #elif _WIN32
        ss << "x86";
    #elif __x86_64__
        ss << "x64";
    #elif __i386__
        ss << "x86";
    #elif __arm__
        ss << "ARM";
    #elif __aarch64__
        ss << "ARM64";
    #else
        ss << "Unknown";
    #endif
    ss << std::endl;

    return ss.str();
}

} // namespace utils
} // namespace collie