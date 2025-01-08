# utils 模块的 CMake 配置

# 设置源文件
set(UTILS_SOURCES
    token_utils.cpp
    version_info.cpp
)

# 构建 utils 静态库
add_library(utils STATIC ${UTILS_SOURCES})

# 设置包含目录
target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}  # 当前目录
        ${CMAKE_CURRENT_BINARY_DIR}  # 生成的文件目录
)

# 设置编译选项
if(MSVC)
    target_compile_options(utils PRIVATE "/utf-8" "/FS" "/MP")
endif()

# 设置库的属性
set_target_properties(utils PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
