# Collie 编译器

Collie 编程语言的官方编译器 / 解释器实现。已打通「前端 → 树遍历解释器」与「前端 → LLVM IR → 本地二进制」两条流水线：`.collie` 源文件既可解释执行，也可编译为本地可执行程序。

> 详细的开发进度、里程碑计划与变更日志见 [PROGRESS.md](PROGRESS.md)。

## 实现语言

C++17。主要考虑因素：
- 优秀的性能表现，适合编译器/解释器开发
- 灵活的内存管理（智能指针 + RAII）
- 良好的跨平台支持（Windows MSVC / Linux gcc/clang）
- 可直接使用 LLVM 官方 C++ API 与预编译包（codegen 后端）

## 编译器架构

```
源码 (.collie) → Lexer → Parser → Semantic
                              ├─→ Interpreter（树遍历）→ 输出
                              └─→ CodeGenerator → LLVM IR (.ll) → clang → 本地二进制
```

| 阶段 | 状态 | 说明 |
|------|------|------|
| 词法分析 Lexer | ✅ 较成熟 | UTF-8/UTF-16，注释，多类字面量，关键字 |
| 语法分析 Parser | ✅ 基本可用 | 表达式、变量/函数声明、if/while/for/block/return/break/continue |
| 语义分析 Semantic | ✅ 相对完整 | 类型检查、隐式转换、函数重载打分、作用域、错误恢复 |
| 树遍历解释器 | ✅ 基本可用 | 字面量/算术/比较/逻辑、变量、控制流、用户函数（含递归）、内建 print |
| LLVM 后端 Codegen | ✅ 进行中（M6） | AST → LLVM IR，已覆盖 S1–S76（算术/控制流/函数/数组/class 与继承/tuple/tribool/位运算等）；不支持的面**拒编而非错编**，已知缺口 CG1–CG4、CG6–CG7 见 [codegen/README.md](codegen/README.md) 第七节 |
| 优化器 Optimizer | ⬜ 未实现 | 尚未接入 LLVM Pass 管线 |

## 项目结构

```
compiler/
├── lexer/           # 词法分析器
├── parser/          # 语法分析器 + AST 定义
├── semantic/        # 语义分析器（类型检查、作用域、重载）
├── interpreter/     # 树遍历解释器（Value/Environment/Interpreter）
├── codegen/         # LLVM 代码生成（CodeGenerator + colliec 驱动）
│   ├── runtime/     # collie_rt.c 运行时垫片（纯 C，供 clang 链接编译产物）
│   └── tests/       # 差分测试：.collie 用例 + run_diff_test.cmake 比对脚本
├── utils/           # 通用工具（token_utils, version_info）
├── tests/           # GoogleTest 单元测试 + 端到端测试
│   └── fixtures/    # CLI 门禁用的 .collie 测试文件
├── examples/        # 示例程序（simple-code.collie, helloworld.collie）
├── .deps/           # GoogleTest 源码缓存（git-ignored，离线友好）
├── main.cpp         # CLI 入口
├── CMakeLists.txt   # 顶层 CMake 配置
└── PROGRESS.md      # 开发进度文档（Living Document）
```

## 构建

要求：CMake 3.14+，支持 C++17 的编译器（MSVC 19+、gcc 9+、clang 10+）。

```bash
# 配置（首次会自动拉取 GoogleTest 到 .deps/，之后离线可用）
cmake -S compiler -B compiler/build -DCMAKE_BUILD_TYPE=Release

# 构建主程序与全部测试
cmake --build compiler/build --config Release

# 运行测试
ctest --test-dir compiler/build -C Release --output-on-failure
```

可选开关：
- `-DCOLLIE_BUILD_TESTS=OFF`：跳过测试构建（无需 GoogleTest）
- `-DCOLLIE_DEPS_DIR=<path>`：指向全局共享的 `.deps` 缓存
- `-DCOLLIE_ENABLE_LLVM=ON -DLLVM_DIR=<llvm解压路径>/lib/cmake/llvm`：启用 codegen 后端（默认关闭）。
  codegen 目标以 `EXCLUDE_FROM_ALL` 接入，且 LLVM 官方预编译包仅提供 Release + `/MT` 库，**须单独以 Release 构建**：

  ```bash
  cmake --build compiler/build --config Release --target colliec
  ```

## 运行

```bash
# 解释执行 .collie 源文件
./compiler/build/Release/collie examples/helloworld.collie

# 带诊断输出（-v 显示词法/语法/语义详细信息）
./compiler/build/Release/collie -v examples/simple-code.collie

# 编译为本地二进制（需启用 LLVM 构建出 colliec）
# 用法：colliec [--emit-llvm] [-o <output>] <source.collie>
./compiler/build/Release/colliec examples/helloworld.collie              # 产出 helloworld.exe
./compiler/build/Release/colliec --emit-llvm examples/helloworld.collie  # 只产出 helloworld.ll
```

## CI

GitHub Actions 自动运行（Windows MSVC + Linux gcc）：
- 构建 `collie` + 全部测试目标
- 单元测试门禁：`lexer_tests`、`parser_tests`、`semantic_tests`、`interpreter_tests`
- CLI 端到端门禁：`cli_valid_program`、`cli_syntax_error_gate`

codegen **不在 CI 内**：默认 `COLLIE_ENABLE_LLVM=OFF`，且依赖本地 LLVM 预编译包。本地启用后可跑差分门禁——
同一 `.collie` 源的「解释器输出」与「编译产物输出」**逐字节比对**，共 79 个用例（s1、s3–s80）：

```bash
cmake --build compiler/build --config Release --target collie colliec
ctest --test-dir compiler/build -C Release -R codegen_diff --output-on-failure
```

配置见 [`.github/workflows/ci-compiler.yml`](../.github/workflows/ci-compiler.yml)。
