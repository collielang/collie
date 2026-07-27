# 设置 CMake 最低版本要求和项目名称
cmake_minimum_required(VERSION 3.10)
# MSVC 运行时库由 CMAKE_MSVC_RUNTIME_LIBRARY 变量/目标属性控制（CMP0091 NEW）：
# 否则下方的全局 /MD 设置与 codegen 模块对齐 LLVM 静态 CRT 的属性覆盖都不生效
cmake_policy(SET CMP0091 NEW)
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
    # 优先使用系统/本地已安装的 GoogleTest，找不到再获取
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        # gtest 使用动态运行时库（与本项目保持一致）
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        # 依赖缓存目录：位于构建目录之外，删除 build/ 后不会丢失，无需重新联网下载。
        # 可用 -DCOLLIE_DEPS_DIR=<path> 覆盖（例如指向用户级全局缓存，在多个克隆间共享）。
        set(COLLIE_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH
            "Persistent directory for third-party sources")
        set(_gtest_src "${COLLIE_DEPS_DIR}/googletest-release-1.12.1")

        if(FETCHCONTENT_SOURCE_DIR_GOOGLETEST)
            # 完全离线：显式指向已有的 googletest 源码目录
            set(_gtest_src "${FETCHCONTENT_SOURCE_DIR_GOOGLETEST}")
        elseif(NOT EXISTS "${_gtest_src}/CMakeLists.txt")
            # 首次构建：浅克隆一次到持久缓存（之后删 build/ 不再重拉）
            find_package(Git REQUIRED)
            message(STATUS "Fetching GoogleTest (once) into ${_gtest_src}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" clone --depth 1 --branch release-1.12.1
                        https://github.com/google/googletest.git "${_gtest_src}"
                RESULT_VARIABLE _gtest_clone_rc
            )
            if(NOT _gtest_clone_rc EQUAL 0)
                message(FATAL_ERROR
                    "Failed to clone GoogleTest. 可先手动将源码放到 ${_gtest_src}，"
                    "或安装系统 GTest 后重试，或 -DCOLLIE_BUILD_TESTS=OFF 跳过测试。")
            endif()
        endif()

        # 使用本地源码构建，编译产物仍落在 build/ 内（可随 build/ 一起清理）
        include(FetchContent)
        FetchContent_Declare(googletest SOURCE_DIR "${_gtest_src}")
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
endif()

# 添加子目录
add_subdirectory(utils)
add_subdirectory(lexer)
add_subdirectory(parser)
add_subdirectory(semantic)
add_subdirectory(interpreter)

# LLVM 后端（M6，可选，默认关闭，不影响现有测试门禁与 CI）：
#   cmake .. -DCOLLIE_ENABLE_LLVM=ON -DLLVM_DIR=<llvm解压路径>/lib/cmake/llvm
# 注意：官方预编译包仅提供 Release + /MT（静态 CRT）库，codegen 相关目标须以 Release 配置构建
# 并在模块内单独对齐 /MT；子目录以 EXCLUDE_FROM_ALL 加入，避免 Debug 全量构建时误链 Release LLVM 库。
option(COLLIE_ENABLE_LLVM "Build the LLVM codegen backend" OFF)
if(COLLIE_ENABLE_LLVM)
    # LLVMConfig.cmake 会把 CMAKE_MSVC_RUNTIME_LIBRARY 改写为 MultiThreaded(/MT)，
    # 污染其后定义的所有目标（如 collie）：find_package 前后保存/恢复本工程设置
    set(_collie_msvc_runtime "${CMAKE_MSVC_RUNTIME_LIBRARY}")
    find_package(LLVM REQUIRED CONFIG)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "${_collie_msvc_runtime}")
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")
    separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
    add_subdirectory(codegen EXCLUDE_FROM_ALL)
endif()

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
        interpreter
)

# 设置 Visual Studio 启动项目为主程序
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT collie)
