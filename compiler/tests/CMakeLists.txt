# tests 模块的 CMake 配置

# 测试工具库
add_library(test_utils STATIC
    test_utils.cpp
)

target_include_directories(test_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_utils
    PUBLIC
        ir
        semantic
        parser
        lexer
        utils
        GTest::gtest
)

# 词法分析器测试
add_executable(lexer_tests
    lexer_test.cpp
)

target_link_libraries(lexer_tests
    PRIVATE
        test_utils
        GTest::gtest_main
)

add_test(NAME lexer_tests COMMAND lexer_tests)

# 语法分析器测试
add_executable(parser_tests
    parser_test.cpp
)

target_link_libraries(parser_tests
    PRIVATE
        test_utils
        GTest::gtest_main
)

add_test(NAME parser_tests COMMAND parser_tests)

# 语义分析器测试
add_executable(semantic_tests
    semantic_analyzer_test.cpp
    semantic_error_test.cpp
    semantic_recovery_test.cpp
    semantic_test.cpp
    symbol_table_test.cpp
)

target_link_libraries(semantic_tests
    PRIVATE
        test_utils
        GTest::gtest_main
)

add_test(NAME semantic_tests COMMAND semantic_tests)

# IR 测试
add_executable(ir_tests
    ir_node_test.cpp
    ir_generator_test.cpp
    ir_optimizer_test_base.h
    ir_constant_folding_test.cpp
    ir_dead_code_elimination_test.cpp
    ir_block_merging_test.cpp
    ir_loop_optimizer_test.cpp
    ir_cse_test.cpp
    ir_loop_unrolling_test.cpp
    ir_loop_invariant_motion_test.cpp
    ir_strength_reduction_test.cpp
)

target_link_libraries(ir_tests
    PRIVATE
        test_utils
        GTest::gtest_main
)

add_test(NAME ir_tests COMMAND ir_tests)

# MSVC 特定配置
if(MSVC)
    target_compile_options(test_utils PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(lexer_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(parser_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(semantic_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(ir_tests PRIVATE "/utf-8" "/FS" "/MP")
endif()
