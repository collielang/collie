cmake_minimum_required(VERSION 3.10)
project(collie_compiler)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Visual Studio 特定配置
if(MSVC)
    # 设置为 MT 运行时库
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # 启用 UTF-8 编码支持
    add_compile_options("/utf-8")
    # 禁用一些警告
    add_compile_options("/wd4251" "/wd4275")
    # 添加这一行来禁用 codecvt 相关警告
    add_compile_definitions(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
endif()

# 添加Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
# 设置gtest选项
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# 编译器源文件
set(COMPILER_SOURCES
    lexer/token.cpp
    lexer/lexer.cpp
    parser/ast.cpp
    parser/parser.cpp
)

# 创建编译器库
add_library(collie_compiler ${COMPILER_SOURCES})
target_include_directories(collie_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 测试可执行文件
enable_testing()
add_executable(lexer_tests tests/lexer_test.cpp)
target_link_libraries(lexer_tests
    collie_compiler
    gtest
    gtest_main
)

# 设置为启动项目
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT lexer_tests)

# 添加测试
add_test(NAME lexer_tests COMMAND lexer_tests)

# 在现有的测试部分添加
add_executable(parser_tests
    tests/parser_test.cpp
)
target_link_libraries(parser_tests
    collie_compiler
    gtest
    gtest_main
)
add_test(NAME parser_tests COMMAND parser_tests)