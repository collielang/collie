# semantic 模块的 CMake 配置

# 设置源文件
set(SEMANTIC_SOURCES
    symbol_table.cpp
    semantic_analyzer.cpp
    semantic_common.cpp
)

# 构建 semantic 静态库
add_library(semantic STATIC ${SEMANTIC_SOURCES})

# 设置包含目录
target_include_directories(semantic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置依赖关系
target_link_libraries(semantic
    PUBLIC
        parser
        lexer
        utils
)

# 设置编译选项
if(MSVC)
    target_compile_options(semantic PRIVATE "/utf-8" "/FS" "/MP")
endif()

# 设置库的属性
set_target_properties(semantic PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
