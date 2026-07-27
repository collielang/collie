# 差分测试脚本（t50）：colliec 编译产物输出 vs collie 解释器输出，须逐字节一致
# 用法：cmake -DCOLLIEC=<colliec 路径> -DCOLLIE=<collie 路径> -DSOURCE=<用例.collie>
#            -DWORK_DIR=<临时目录> -P run_diff_test.cmake
# 由 codegen/CMakeLists.txt 以 CONFIGURATIONS Release 注册进 ctest。

foreach(_required COLLIEC COLLIE SOURCE WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "missing -D${_required}=...")
    endif()
endforeach()

get_filename_component(_case_name "${SOURCE}" NAME_WE)
file(MAKE_DIRECTORY "${WORK_DIR}")
set(_exe "${WORK_DIR}/${_case_name}.exe")

# 1) colliec 编译为本地二进制
execute_process(
    COMMAND "${COLLIEC}" "${SOURCE}" -o "${_exe}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "colliec failed on ${_case_name} (rc=${_rc}):\n${_out}${_err}")
endif()

# 2) 运行编译产物，捕获输出
execute_process(
    COMMAND "${_exe}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _native_out
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "native binary failed on ${_case_name} (rc=${_rc}):\n${_err}")
endif()

# 3) 解释器执行同一源文件，捕获输出
execute_process(
    COMMAND "${COLLIE}" "${SOURCE}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _interp_out
    ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "interpreter failed on ${_case_name} (rc=${_rc}):\n${_err}")
endif()

# 4) 逐字节比对（两侧均为 Windows 文本模式 \r\n，天然对齐）
if(NOT _native_out STREQUAL _interp_out)
    message(FATAL_ERROR "differential mismatch on ${_case_name}:\n"
                        "--- native ---\n${_native_out}"
                        "--- interpreter ---\n${_interp_out}")
endif()
message(STATUS "diff ok: ${_case_name}")
