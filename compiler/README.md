# Collie 编译器

Collie 编程语言的官方编译器 / 解释器实现。当前已打通前端到树遍历解释器的完整流水线，可实际执行 `.collie` 源文件。

> 详细的开发进度、里程碑计划与变更日志见 [PROGRESS.md](PROGRESS.md)。

## 实现语言

C++17。主要考虑因素：
- 优秀的性能表现，适合编译器/解释器开发
- 灵活的内存管理（智能指针 + RAII）
- 良好的跨平台支持（Windows MSVC / Linux gcc/clang）

## 编译器架构

```
源码 (.collie) → Lexer → Parser → Semantic → Interpreter → 输出
```

| 阶段 | 状态 | 说明 |
|------|------|------|
| 词法分析 Lexer | ✅ 较成熟 | UTF-8/UTF-16，注释，多类字面量，关键字 |
| 语法分析 Parser | ✅ 基本可用 | 表达式、变量/函数声明、if/while/for/block/return/break/continue |
| 语义分析 Semantic | ✅ 相对完整 | 类型检查、隐式转换、函数重载打分、作用域、错误恢复 |
| 树遍历解释器 | ✅ 基本可用 | 字面量/算术/比较/逻辑、变量、控制流、用户函数（含递归）、内建 print |
| LLVM 后端 | ⬜ 未启动 | 旧自研 IR 已退役，未来基于 LLVM 实现代码生成 |

## 项目结构

```
compiler/
├── lexer/           # 词法分析器
├── parser/          # 语法分析器 + AST 定义
├── semantic/        # 语义分析器（类型检查、作用域、重载）
├── interpreter/     # 树遍历解释器（Value/Environment/Interpreter）
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

## 运行

```bash
# 解释执行 .collie 源文件
./compiler/build/Release/collie examples/helloworld.collie

# 带诊断输出（-v 显示词法/语法/语义详细信息）
./compiler/build/Release/collie -v examples/simple-code.collie
```

## CI

GitHub Actions 自动运行（Windows MSVC + Linux gcc）：
- 构建 `collie` + 全部测试目标
- 单元测试门禁：`lexer_tests`、`parser_tests`、`semantic_tests`、`interpreter_tests`
- CLI 端到端门禁：`cli_valid_program`、`cli_syntax_error_gate`

配置见 [`.github/workflows/ci-compiler.yml`](../.github/workflows/ci-compiler.yml)。
