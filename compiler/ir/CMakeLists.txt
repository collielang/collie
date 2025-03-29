# ir 模块的 CMake 配置

# 设置源文件
set(IR_SOURCES
    ir_node.cpp
    ir_generator.cpp
    ir_optimizer.cpp
)

# 构建 ir 静态库
add_library(ir STATIC ${IR_SOURCES})

# 设置包含目录
target_include_directories(ir
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置依赖关系
target_link_libraries(ir
    PUBLIC
        semantic
        parser
        lexer
        utils
)

# 设置编译选项
if(MSVC)
    target_compile_options(ir PRIVATE "/utf-8" "/FS" "/MP")
endif()

# 设置库的属性
set_target_properties(ir PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
