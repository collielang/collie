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

# 引入 Google Test 框架
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# 启用测试
enable_testing()

# 添加子目录
add_subdirectory(utils)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(semantic)
add_subdirectory(ir)
add_subdirectory(tests)

# 构建主程序
add_executable(collie
    main.cpp
)

# 链接库
target_link_libraries(collie
    PRIVATE
        utils
        lexer
        parser
        semantic
        ir
)

# 设置 Visual Studio 启动项目为主程序
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT collie)
