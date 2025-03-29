# parser 模块的 CMake 配置

# 设置源文件
set(PARSER_SOURCES
    ast.cpp
    parser.cpp
)

# 构建 parser 静态库
add_library(parser STATIC ${PARSER_SOURCES})

# 设置包含目录
target_include_directories(parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置依赖关系
target_link_libraries(parser
    PUBLIC
        lexer
        utils
)

# 设置编译选项
if(MSVC)
    target_compile_options(parser PRIVATE "/utf-8" "/FS" "/MP")
endif()

# 设置库的属性
set_target_properties(parser PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
