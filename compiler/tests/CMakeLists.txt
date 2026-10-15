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

# codegen 差分测试（t50，Release 专属）：colliec 编译产物 vs collie 解释器输出
# 逐字节比对；用例在 codegen/tests/diff_cases/。
# 注册在本目录而非 codegen/：codegen 子目录以 EXCLUDE_FROM_ALL 接入，
# 其 add_test 不会进 CTestTestfile（CMake 既定行为）。
# 仅 Release 配置生效（codegen 目标与 LLVM /MT 库仅 Release 可链）：
#   先 Release 构建 collie + colliec，再 ctest -C Release -R codegen_diff
# Debug 门禁（ctest -C Debug）不受影响。
if(COLLIE_ENABLE_LLVM)
    set(_codegen_dir ${CMAKE_CURRENT_SOURCE_DIR}/../codegen)
    foreach(_case s1_hello s3_control_flow s4_loops s5_functions s6_print_format s7_string_concat
            s8_string_compare s9_string_index s10_string_methods s11_int_edge s12_array s13_class
            s14_inherit s15_number s16_tonumber s17_multimatch s18_tribool s19_switch s20_number_methods
            s21_tuple s22_bits s23_array_signature s24_array_field s25_object_field s26_globals
            s27_number_field s28_tuple_eq s29_tuple_global s30_print_order s31_match_tuple
            s32_array_eq s33_decimal_mod s34_none_value s35_object_eq s36_tuple_index s37_tuple_get
            s38_nested_array s39_upcast s40_bitfield s41_dynarray s42_nestedwide s43_numarr s44_nestedfn
            s45_uninit s46_mergecls s47_mergearr s48_mergetup s49_uninitmore s50_bytewordret
            s51_bytewordparam s52_bytewordmethod s53_objectarray s54_uninitvar s55_upcast_members
            s56_downcast_members s57_method_nested_fn s58_substring_frac s59_array_retype s60_tuple_hetnum
            s61_tuple_getnum s62_class_uninit_field s63_uninit_branches s64_num_narrow s65_uninit_tuple
            s66_dowhile_definite s67_tuple_ref_index s68_instance_array_cast s69_tuple_instance_cast
            s70_tuple_reshape s71_index_assign_cast s72_mixed_array_literal s73_object_decl
            s74_uninit_object s75_number_index s76_method_overload s77_block_function
            s78_ctor_definite_assign s79_ctor_flow_definite s80_block_class s81_function_class
            s82_method_class s83_block_scoped_class s84_topfn_overload
            s85_object_param s86_tribool_array s87_cross_tree_array
            s88_hetero_tuple_dyn s89_hetero_scalar_array s90_dynamic_tuple s91_closure_capture s92_dynamic_object_field
            s93_function_value s94_hetero_obj_array s95_dyn_elem_member
            s96_object_dyn_slot s97_dyn_to_slot s98_dyn_arith s99_dyn_compare s100_dyn_index
            s101_dyn_domain_read s102_dyn_length s103_dyn_nested_index s104_dyn_string_methods
            s105_dyn_to_number s105_dyn_same_name_method
            s106_dyn_number_methods s106_dyn_same_name_number_method
            s107_dyn_tribool_methods s107_dyn_same_name_tribool_method
            s108_dyn_to_class_slots s108_dyn_to_class_inherit
            s108_dyn_to_class_decl s108_dyn_to_class_arg s108_dyn_to_class_return
            s109_numeric_kind_literal s109_all_num_literal s109_tribool_bool_mix
            s109_tribool_num_write s109_arr_eq_cross_kind
            s109_dyn_tostring_same_name_method s109_dyn_array_slots
            s110_obj_nested_array_mix s110_object_slot_array_init
            s110_dyn_array_bit_write s110_dyn_array_eq_deep
            s110_dyn_array_closure_loop
            s111_obj_param_scalar_carry s111_obj_param_length s111_obj_param_index
            s111_obj_param_array_carry s111_obj_param_instance_carry
            s111_obj_param_reassign_capture s111_obj_method_signature
            s112_object_field_scalar_init s112_object_field_array_bit
            s112_object_field_instance_bit s112_object_field_inherit_tree
            s112_object_field_reassign_capture s112_object_field_methods
            s113_dyn_tuple_named_field s113_dyn_tuple_field_kinds
            s113_dyn_tuple_field_array_bit s113_dyn_tuple_field_param_return
            s113_dyn_tuple_field_loop_consume s113_dyn_tuple_field_class_capture
            s114_dyn_tuple_static_array_elem s114_dyn_tuple_instance_elem
            s114_dyn_tuple_element_carry_out s114_dyn_tuple_merge_loop
            s114_dyn_tuple_incompatible_leaf s114_dyn_tuple_mixed_kind_elems
            s115_array_slot_decl s115_array_slot_kinds s115_array_slot_reassign
            s115_array_slot_signature s115_array_slot_field_capture
            s115_array_slot_domain
            s116_dyn_arm_merge_basic s116_dyn_arm_merge_kinds
            s116_dyn_arm_merge_obj s116_match_arm_merge
            s116_elem_slot_dyn_write s116_elem_slot_roundtrip
            s117_dyn_domain_arr_elem_write s117_dyn_domain_obj_elem_write
            s117_dyn_domain_elem_carry_out s117_dyn_domain_elem_control_flow
            s117_dyn_domain_elem_mixed_consume s117_dyn_domain_elem_alias
            s118_merge_cross_tree_obj s118_merge_obj_scalar_arr
            s118_match_cross_tree s118_merge_precision_kept
            s118_merge_landing_slots s118_merge_nested_consume)
        add_test(NAME codegen_diff_${_case}
            COMMAND ${CMAKE_COMMAND}
                -DCOLLIEC=$<TARGET_FILE:colliec>
                -DCOLLIE=$<TARGET_FILE:collie>
                -DSOURCE=${_codegen_dir}/tests/diff_cases/${_case}.collie
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/codegen_diff_work
                -P ${_codegen_dir}/tests/run_diff_test.cmake
            CONFIGURATIONS Release)
    endforeach()

    # 拒编门禁（t136 引入）：拒编面用例须 colliec rc=1 且 stderr 含
    # "not yet supported"（保守拒编不错编面的回归锁，禁止降差分用例迁就测试）
    foreach(_rej s91_reject_closure_tuple_capture s91_reject_closure_method_capture s91_reject_closure_shadowed_capture s92_reject_dynamic_object_field_type_mismatch
            s93_reject_function_as_argument s93_reject_function_in_array s93_reject_function_return_value
            s96_reject_obj_slot_scalar_reassign
            s97_reject_dyn_to_homog_array_slot
            s111_reject_obj_param_tuple s112_reject_object_field_tuple
            s114_reject_dyn_tuple_nested_elem s114_reject_dyn_tuple_function_elem
            s115_reject_dyn_array_slot_downgrade_in_loop
            s116_reject_dyn_arm_tuple s116_reject_match_arm_tuple
            s117_reject_tuple_elem_dyn_domain
            s117_reject_function_elem_dyn_domain
            s117_reject_arr_elem_to_homog_domain
            s118_reject_tern_arm_tuple_obj s118_reject_match_arm_tuple_obj
            s118_reject_tern_arm_function_obj s118_reject_match_arm_function_obj
            s118_reject_match_target_dyn)
        add_test(NAME codegen_reject_${_rej}
            COMMAND ${CMAKE_COMMAND}
                -DCOLLIEC=$<TARGET_FILE:colliec>
                -DSOURCE=${_codegen_dir}/tests/reject_cases/${_rej}.collie
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/codegen_reject_work
                -P ${_codegen_dir}/tests/run_reject_test.cmake
            CONFIGURATIONS Release)
    endforeach()
endif()

# MSVC 特定配置
if(MSVC)
    target_compile_options(test_utils PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(lexer_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(parser_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(semantic_tests PRIVATE "/utf-8" "/FS" "/MP")
    target_compile_options(interpreter_tests PRIVATE "/utf-8" "/FS" "/MP")
endif()
