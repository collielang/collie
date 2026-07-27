# interpreter 模块的 CMake 配置

# 设置源文件
set(INTERPRETER_SOURCES
    interpreter.cpp
    big_int.cpp
)

# 构建 interpreter 静态库
add_library(interpreter STATIC ${INTERPRETER_SOURCES})

# 设置包含目录
target_include_directories(interpreter
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置依赖关系：解释器遍历 AST，依赖 parser（AST 定义）与 lexer（Token）
target_link_libraries(interpreter
    PUBLIC
        parser
        lexer
        utils
)

# 设置编译选项
if(MSVC)
    target_compile_options(interpreter PRIVATE "/utf-8" "/FS" "/MP")
endif()

# 设置库的属性
set_target_properties(interpreter PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
