# utils 模块的 CMake 配置
set(UTILS_SOURCES
    token_utils.cpp
    version_info.cpp
)

# 构建 utils 库
add_library(utils STATIC ${UTILS_SOURCES})
target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
