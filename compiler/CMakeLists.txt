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
    # 增加堆栈大小
    add_link_options("/STACK:8388608")
    # 启用多处理器编译
    add_compile_options("/MP")
endif()

# 是否构建单元测试。关闭后主编译器无需测试框架，也无需联网拉取依赖：
#   cmake .. -DCOLLIE_BUILD_TESTS=OFF
option(COLLIE_BUILD_TESTS "Build unit tests" ON)

if(COLLIE_BUILD_TESTS)
    # 优先使用系统/本地已安装的 GoogleTest，找不到再联网拉取
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        # gtest 使用动态运行时库（与本项目保持一致）
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
        )
        # 离线场景：设置 FETCHCONTENT_SOURCE_DIR_GOOGLETEST 指向本地 googletest 源码目录即可
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
endif()

# 添加子目录
add_subdirectory(utils)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(semantic)
if(COLLIE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

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
)

# 设置 Visual Studio 启动项目为主程序
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT collie)
