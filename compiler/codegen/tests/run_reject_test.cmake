# 拒编门禁脚本（t136）：保守拒编面的回归锁
# 断言：colliec 退出码为 1、stderr 含 "not yet supported"、stdout 为空
#（拒编不错编——被拒构造解释器可运行或两端同错，产物绝不能静默生成）
# 用法：cmake -DCOLLIEC=<colliec 路径> -DSOURCE=<用例.collie> -DWORK_DIR=<临时目录>
#       -P run_reject_test.cmake
# 由 tests/CMakeLists.txt 以 CONFIGURATIONS Release 注册进 ctest（codegen_reject_*）。

foreach(_required COLLIEC SOURCE WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "missing -D${_required}=...")
    endif()
endforeach()

get_filename_component(_case_name "${SOURCE}" NAME_WE)
file(MAKE_DIRECTORY "${WORK_DIR}")
set(_exe "${WORK_DIR}/${_case_name}.exe")
file(REMOVE "${_exe}")

execute_process(
    COMMAND "${COLLIEC}" "${SOURCE}" -o "${_exe}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

if(NOT _rc EQUAL 1)
    message(FATAL_ERROR "expected reject (rc=1) on ${_case_name}, got rc=${_rc}:\n${_out}${_err}")
endif()
if(NOT _err MATCHES "not yet supported")
    message(FATAL_ERROR "reject message missing 'not yet supported' on ${_case_name}:\n${_err}")
endif()
if(NOT _out STREQUAL "")
    message(FATAL_ERROR "reject must produce no stdout on ${_case_name}:\n${_out}")
endif()
if(EXISTS "${_exe}")
    message(FATAL_ERROR "rejected compile must not leave an executable on ${_case_name}")
endif()
message(STATUS "reject ok: ${_case_name}")
