# IR 模块的 CMake 配置文件

# 添加库
add_library(ir
    ir_node.cpp
    ir_generator.cpp
)

# 设置包含目录
target_include_directories(ir
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# 设置链接库
target_link_libraries(ir
    PUBLIC
        utils
        parser
)