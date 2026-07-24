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
        lexer
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
        parser
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
)

target_link_libraries(semantic_tests
    PRIVATE
        semantic
        test_utils
        GTest::gtest_main
)

add_test(NAME semantic_tests COMMAND semantic_tests)

# 解释器端到端测试
add_executable(interpreter_tests
    interpreter_test.cpp
)

target_link_libraries(interpreter_tests
    PRIVATE
        interpreter
        semantic
        parser
        lexer
        utils
        GTest::gtest_main
)

add_test(NAME interpreter_tests COMMAND interpreter_tests)

# CLI 端到端门禁测试：直接运行 collie 可执行文件，覆盖 main.cpp 对
# parser.get_errors() 的语法错误门禁（gtest 无法覆盖 main.cpp 自身逻辑）。
# 合法程序：正常执行并输出 gate-ok。
add_test(NAME cli_valid_program
    COMMAND collie ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/valid_hello.collie)
set_tests_properties(cli_valid_program PROPERTIES
    PASS_REGULAR_EXPRESSION "gate-ok")

# 含语法错误：必须报告 syntax error，且【不】执行合法语句（不得输出 42）。
# 设置 PASS/FAIL 正则后 CTest 忽略退出码，仅按输出判定。
add_test(NAME cli_syntax_error_gate
    COMMAND collie ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/syntax_error.collie)
set_tests_properties(cli_syntax_error_gate PROPERTIES
    PASS_REGULAR_EXPRESSION "syntax error"
    FAIL_REGULAR_EXPRESSION "42")

# MSVC 特定配置
if(MSVC)
    target_compile_options(test_utils PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(lexer_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(parser_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(semantic_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(interpreter_tests PRIVATE "/utf-8" "/FS" "/MP")
endif()
