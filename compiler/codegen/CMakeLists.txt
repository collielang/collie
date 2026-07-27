# codegen 模块：AST → LLVM IR 降级（M6，进行中）
# 仅在顶层 -DCOLLIE_ENABLE_LLVM=ON 时进入（find_package(LLVM) 已在顶层完成）。
# 官方预编译 LLVM 包为 Release + /MT（静态 CRT）静态库：本模块目标须以
# Release 配置构建，且运行时库单独对齐为 /MT（工程其余目标保持 /MD 不变）。

# 按需映射 LLVM 组件为静态库列表（后续按需追加，如 native/target 组件）
llvm_map_components_to_libnames(COLLIE_LLVM_LIBS core support)

# 冒烟验证工具：用 IRBuilder 构造 hello world 模块，verify 后打印 IR。
# 用途：证明「头文件 + 静态库 + CMake config」全链路可用，为 t48c 降级实现铺路。
add_executable(llvm_smoke llvm_smoke.cpp)
target_include_directories(llvm_smoke SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(llvm_smoke PRIVATE ${LLVM_DEFINITIONS_LIST})
target_link_libraries(llvm_smoke PRIVATE ${COLLIE_LLVM_LIBS})

if(MSVC)
    # LLVM 官方包不带 RTTI，对齐关闭以避免 typeinfo 链接问题
    target_compile_options(llvm_smoke PRIVATE /GR-)
    # LLVM 官方包用静态 CRT（MT_StaticRelease），不对齐会报 LNK2038 RuntimeLibrary 不匹配。
    # 双保险：属性走 CMP0091 NEW 抽象；目录作用域再清洗旧缓存 flags 里的 /MD（仅影响本模块）
    set_property(TARGET llvm_smoke PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    foreach(_flag_var CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO
                      CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_DEBUG)
        string(REPLACE "/MD" "/MT" ${_flag_var} "${${_flag_var}}")
    endforeach()
endif()
