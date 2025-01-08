# 设置 CMake 最低版本要求和项目名称
cmake_minimum_required(VERSION 3.10)
project(collie_compiler)

# 编译器设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 特定配置
if(MSVC)
    # 使用 MD 运行时库
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # 启用 UTF-8 编码支持
    add_compile_options("/utf-8")
    # 启用并行 PDB 写入
    add_compile_options("/FS")
    # 禁用特定警告
    add_compile_options("/wd4251" "/wd4275")
    # 禁用 codecvt 弃用警告
    add_compile_definitions(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
    # 增加堆栈大小
    add_link_options("/STACK:8388608")
    # 启用多处理器编译
    add_compile_options("/MP")
endif()

# 设置 gtest 使用动态运行时库
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# 在获取 googletest 之前设置最低 CMake 版本
set(CMAKE_MINIMUM_REQUIRED_VERSION 3.10)

# 引入 Google Test 框架
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# 编译器核心模块源文件
set(COMPILER_SOURCES
    lexer/token.cpp
    lexer/lexer.cpp
    parser/ast.cpp
    parser/parser.cpp
    utils/token_utils.cpp
)

# 构建编译器核心库
add_library(collie_compiler ${COMPILER_SOURCES})
target_include_directories(collie_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 启用测试
enable_testing()

# 词法分析器测试
add_executable(lexer_tests tests/lexer_test.cpp)
target_link_libraries(lexer_tests
    collie_compiler
    gtest
    gtest_main
)
add_test(NAME lexer_tests COMMAND lexer_tests)

# 语法分析器测试
add_executable(parser_tests tests/parser_test.cpp)
target_link_libraries(parser_tests
    collie_compiler
    gtest
    gtest_main
)
add_test(NAME parser_tests COMMAND parser_tests)

# 语义分析模块
add_library(semantic
    semantic/symbol_table.cpp
    semantic/semantic_analyzer.cpp
    semantic/semantic_common.cpp
)
target_link_libraries(semantic
    PUBLIC collie_compiler
)

# 测试工具库
add_library(test_utils STATIC
    tests/test_utils.cpp
)
target_link_libraries(test_utils
    PUBLIC collie_compiler
    PUBLIC semantic
)
target_include_directories(test_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# 语义分析器测试
add_executable(semantic_tests
    tests/semantic_analyzer_test.cpp
    tests/semantic_error_test.cpp
    tests/semantic_recovery_test.cpp
)

# 为 semantic_tests 添加 /FS 选项
if(MSVC)
    target_compile_options(semantic_tests PRIVATE "/FS")
    target_compile_options(semantic PRIVATE "/FS")
    target_compile_options(test_utils PRIVATE "/FS")
endif()

target_link_libraries(semantic_tests
    PRIVATE semantic
    PRIVATE test_utils
    PRIVATE GTest::gtest
    PRIVATE GTest::gtest_main
)

add_test(NAME semantic_tests COMMAND semantic_tests)

# 构建主程序
add_executable(collie main.cpp)
target_link_libraries(collie
    PRIVATE collie_compiler
    PRIVATE semantic
)

# 设置 Visual Studio 启动项目为主程序
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT collie)