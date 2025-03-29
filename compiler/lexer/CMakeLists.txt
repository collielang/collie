# lexer 模块的 CMake 配置

# 设置源文件
set(LEXER_SOURCES
    token.cpp
    lexer.cpp
)

# 构建 lexer 静态库
add_library(lexer STATIC ${LEXER_SOURCES})

# 设置包含目录
target_include_directories(lexer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置依赖关系
target_link_libraries(lexer
    PUBLIC
        utils
)

# 设置编译选项
if(MSVC)
    target_compile_options(lexer PRIVATE "/utf-8" "/FS" "/MP")
endif()

# 设置库的属性
set_target_properties(lexer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
