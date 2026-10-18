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
            s118_merge_landing_slots s118_merge_nested_consume
            s119_match_dyn_scalar_bits s119_match_dyn_container_bits
            s119_match_dyn_both_sides s119_match_dyn_switch
            s119_match_dyn_signature_field s119_match_dyn_control_flow
            s120_tuple_to_object_slot s120_tuple_object_field
            s120_dyn_tuple_nested_elem s120_tuple_arms_merge
            s120_tuple_elem_dyn_domain_write s120_dyn_tuple_member_access
            s120_dyn_tuple_compare
            s121_fn_to_object_signature s121_fn_array_bits
            s121_fn_tuple_element s121_fn_dyn_domain_elem
            s121_fn_arms_merge s121_fn_match_candidate
            s121_fn_consume_control_flow
            s122_tuple_dyn_index_arr_elem s122_tuple_dyn_index_object_elem
            s122_tuple_dyn_index_nested_tuple
            s122_tuple_dyn_index_mixed_carriers
            s122_tuple_dyn_index_signature_field
            s122_tuple_dyn_index_nested_forms
            s123_object_uninit_scalar_carriers
            s123_object_uninit_container_carriers
            s123_object_uninit_instance_bits
            s123_object_uninit_reassign_cycle
            s123_object_uninit_control_flow
            s124_object_slot_carrier_cycle
            s124_object_slot_instance_bits
            s124_object_slot_container_carriers
            s124_object_slot_match_compare
            s124_object_slot_control_flow
            s125_none_bit_slot_init s125_none_bit_slot_assign
            s125_none_bit_uninit_slot s125_none_bit_tuple_element
            s125_none_bit_signature_field s125_none_bit_array_element
            s125_none_bit_compare_flow
            s126_field_mismatch_object_slot
            s126_field_mismatch_typed_slots
            s126_field_mismatch_container_bits
            s126_field_mismatch_signature
            s126_field_mismatch_control_flow
            s127_match_static_kind_exclusion
            s127_match_tribool_kinds
            s127_match_none_slot_candidates
            s127_match_dynamic_tuple_targets
            s127_match_container_instance_kinds
            s127_match_numeric_kind_mix
            s127_match_control_flow_nesting
            s128_field_mismatch_assign_basic
            s128_field_mismatch_assign_kinds
            s128_field_mismatch_assign_dyn_src
            s128_field_mismatch_assign_receivers
            s128_field_mismatch_assign_tree
            s128_field_mismatch_assign_consume
            s128_field_mismatch_assign_flow
            s128_field_mismatch_assign_obj_arm
            s129_tuple_field_basic
            s129_tuple_field_kinds
            s129_tuple_field_nested
            s129_tuple_field_dyn_source
            s129_tuple_field_receivers
            s129_tuple_field_tree
            s129_tuple_field_consume
            s129_tuple_field_flow
            s130_tuple_var_dyn_basic
            s130_tuple_var_dyn_kinds
            s130_tuple_var_dyn_sources
            s130_tuple_var_dyn_sig
            s130_tuple_var_dyn_reassign
            s130_tuple_var_dyn_fields
            s130_tuple_var_dyn_consume
            s130_tuple_var_dyn_flow
            s131_tuple_uninit_dyn_first
            s131_tuple_uninit_shape
            s131_tuple_uninit_branch
            s131_tuple_uninit_loop
            s131_tuple_uninit_global
            s131_tuple_uninit_sig
            s131_tuple_uninit_consume
            s131_tuple_uninit_kinds
            s132_shadow_basic
            s132_shadow_init_chain
            s132_shadow_write
            s132_shadow_kinds
            s132_shadow_ctor
            s132_shadow_consume
            s132_shadow_sig
            s132_shadow_flow
            s133_dispatch_diff_return
            s133_dispatch_diff_params
            s133_dispatch_receiver_reassign
            s133_dispatch_override_diff
            s133_dispatch_inherited_diff
            s133_dispatch_ret_kinds
            s133_dispatch_void_arm
            s133_dispatch_class_param
            s133_dispatch_dyn_domain_receiver
            s133_dispatch_flow
            s134_shadow_type_mismatch
            s134_shadow_field_class
            s134_shadow_bit_max
            s134_shadow_ctor_side_effects
            s134_shadow_tuple_carrier
            s134_shadow_array_carrier
            s134_shadow_object_carrier
            s134_shadow_cross_tree_mixed
            s134_shadow_dyn_consume
            s134_shadow_chained_and_tribool
            s134_shadow_class_carrier_alias
            s135_uninit_field_all_types
            s135_uninit_field_dynamic_members
            s135_uninit_field_ctor_partial
            s135_uninit_field_observe_before_assign
            s135_shadow_uninit_no_ctor
            s135_uninit_field_inherit_upcast
            s135_uninit_field_dyn_consume
            s136_uninit_object_conditional_assign
            s136_uninit_object_none_observable
            s136_uninit_object_region_paths
            s136_uninit_object_capture_functions
            s136_uninit_object_slot_consumers)
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
    foreach(_rej s91_reject_closure_tuple_capture s91_reject_closure_method_capture s91_reject_closure_shadowed_capture
            s97_reject_dyn_to_homog_array_slot
            s115_reject_dyn_array_slot_downgrade_in_loop
            s117_reject_arr_elem_to_homog_domain
            s120_reject_tuple_elem_homog_array s120_reject_dyn_tuple_prop_assign
            s136_reject_num_uninit_conditional_assign
            s136_reject_ptr_uninit_conditional_assign
            s125_reject_none_bit_homog_array
            s129_reject_dyn_to_tuple_reassign
            s131_reject_tuple_uninit_conditional_assign
            s131_reject_tuple_uninit_function_assign
            s133_reject_arity_across_arms)
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
