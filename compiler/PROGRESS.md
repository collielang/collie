# Collie 编译器 · 开发进度与路线（Living Document）

> 这是一份**持续更新**的工程进度文档，记录当前状态、关键决策、阶段计划、风险与待确认的语言设计问题。
>
> **更新约定**：每完成或修复一块工作，就在对应里程碑打勾，并在文末「变更日志」追加一条（与 git 提交一一对应）。

最后更新：2026-07-25

---

## 一、当前状态快照

编译流水线已打通到**树遍历解释器**：`helloworld.collie` 可实际执行并输出 `Hello, World!`（尚无代码生成 / LLVM 后端）。

| 阶段 | 状态 | 说明 |
|------|------|------|
| 词法分析 Lexer | ✅ 较成熟 | UTF-8/UTF-16、注释、多类字面量，token 种类丰富 |
| 语法分析 Parser | ✅ 基本可用 | 表达式、变量/函数声明、if/while/for/block/return/break/continue |
| 语义分析 Semantic | ✅ 相对完整 | 类型检查、隐式转换、函数重载打分、作用域、panic-mode 错误恢复 |
| **解释器 Interpreter** | ✅ 最小可用 | **树遍历解释器**：字面量/算术/比较/逻辑、变量声明与读写、if/while/for、break/continue、内建 `print`，跑通 helloworld |
| 中间代码 IR | ⛔ 已下线 | 旧自研 IR 实现质量不佳，正式移除，未来基于 LLVM 重做 |
| 优化器 Optimizer | ⬜ 未实现 | — |
| 目标代码 Codegen | ⬜ 未实现 | 计划 LLVM 后端 |

已知的语法「半截特性」（token 有、语法/语义未闭环）：`switch`、`do-while`、`class`、`tribool`、`==?`、tuple 成员访问等。

---

## 二、关键决策记录（ADR）

| 编号 | 决策 | 理由 |
|------|------|------|
| D1 | **旧自研 IR 正式下线，不恢复**；未来基于 **LLVM** 重做后端 | 旧实现质量不佳且依赖尚不稳定的 AST/语义；反正最终要用 LLVM，恢复一套将被替换的 IR 是浪费。git 历史保留可随时参考 |
| D2 | **优先实现树遍历解释器（路线 A）**，先跑通 helloworld，再补 LLVM 后端（路线 B） | 快速拿到「能跑」的正反馈，并建立回归测试基线，反向验证 parser/semantic |
| D3 | 源码**统一按 UTF-8 字节处理**，移除 wide-char / `std::codecvt` 管线；平台相关代码用 `#ifdef` 隔离并集中 | 一举解决「跨平台」与「codecvt 已弃用」两个隐患 |
| D4 | **测试解耦**：每个 test 目标只链接被测模块；主编译器不依赖测试框架；测试可选（`BUILD_TESTING`） | 降低耦合，模块间仅通过头文件 + 静态库交互 |
| D5 | **GoogleTest 改为可选 + 离线友好**（优先 `find_package`，回退 `FetchContent`，支持本地/vendored 源）；评估迁移到 **doctest**（单头文件，可直接 vendored，零网络） | 解决国内网络拉取 googletest 的长期痛点 |
| D6 | 增加 **GitHub Actions CI**（Windows + Linux，configure/build/ctest） | 防止「删文件后构建配置未同步」这类回归 |
| D7 | **不与 Visual Studio 深绑定**，用 CMake 保持工具链中立（支持 Ninja + gcc/clang，兼容 VSCode/CLion） | 不限制开发工具，便于社区开源协作 |
| D8 | 源文件后缀：主 `.collie`，别名 `.col`（已确认） | 与现有示例 `simple-code.collie` 一致，同时提供短后缀 |

---

## 三、里程碑与阶段计划

### M0 · 修复构建 & 模块解耦（进行中）
- [x] 修复 `tests/CMakeLists.txt`：移除对已删 `ir` 库的链接、移除已删的 `symbol_table_test.cpp`
- [x] 测试解耦：每个 test 目标只链接被测模块（lexer_tests→lexer，parser_tests→parser，semantic_tests→semantic）
- [x] 增加 `option(COLLIE_BUILD_TESTS ...)`，关闭后主编译器不依赖测试框架、无需联网
- [x] googletest 改为离线友好：`find_package` 优先 → 持久缓存 `.deps/`（build 之外，删 build 不重拉，`--depth 1` 首次克隆一次）→ `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` 完全离线覆盖；可用 `-DCOLLIE_DEPS_DIR=<path>` 指向全局共享缓存
- [ ] 更新 `compiler/README.md` 中过时的进度描述（当前仍写 IR 已完成）

### M1 · 跨平台 & 编码统一
**M1a · `main.cpp` 编码/平台隔离（已完成）**
- [x] `main.cpp` 改为二进制读取源文件、全程 UTF-8 `std::string`（含跳过 UTF-8 BOM）
- [x] 移除 `main.cpp` 中的 `std::codecvt` / `std::wstring_convert` / `wifstream` 用法
- [x] `#include <Windows.h>` 用 `#ifdef _WIN32` 隔离（`SetConsoleOutputCP` 已在 `_WIN32` 内）
- [x] VS2026 实测编译通过；UTF-8 源文件（含中文注释）读取与词法输出正确

**M1b · 词法层 codecvt 移除（已完成）**
- [x] 新增平台无关的 `lexer/utf_convert.h`（手写 UTF-8↔UTF-16），替换 `token.h` / `lexer.cpp` 中的 `codecvt_utf8_utf16` 与 Windows 专属 API
- [x] 移除 `CMakeLists.txt` 中的 `_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING`（已无 codecvt 依赖）
- [x] 删除 `lexer.cpp` 中无调用方的 `utf16_to_utf8` 死代码
- [x] VS2026 实测：`collie.exe` 与 `lexer_tests` 均编译通过，无 codecvt 弃用警告
- [ ] 在 gcc/clang（Linux）下验证可编译（待有 Linux 环境时）

> 注：`lexeme_utf16()` 相关测试（`UTF16Strings` 等）**在 M1b 之前就已失败**，根因是下方 M2 记录的词法器 UTF-8 bug（传入的 lexeme 字节已损坏）。M1b 仅把 codecvt 的静默替换改为显式报错，**无回归**（前后均为 4 通过 / 7 失败）。

### M2 · 语义错误上报 & 前端 UTF-8 健壮性
- [x] `main.cpp` 在语义分析后检查 `analyzer.has_errors()` 并打印 `get_errors()`（逐条 `Line X, Column Y: msg`）
- [x] 有语义错误时以非零退出码结束，不再打印 "Compilation successful!"
- [x] 修复语义分析对未声明函数调用（如 `print(a)`）死循环

**前端死循环修复（parser + semantic，已修复：两处独立死循环不再卡死）**
- [x] `parser.cpp`：`parse_program` / `parse_block_statement` 驱动循环加**进度守卫**（记录游标前值，若本轮未推进则强制 `advance()`）——修复 `synchronize()` 停在无消费规则的边界关键字（如 `class Foo {}` 的 `KW_CLASS`）导致的死循环（原会输出 >20MB）
- [x] `semantic_analyzer.cpp`：`synchronize()` 在 `tokens_` 为空（主程序路径未调用 `set_tokens`）或已到序列末尾时直接 `exit_panic_mode` 并返回；`advance_token()` 增加 `!tokens_.empty()` 守卫防 `size()-1` 下溢——修复 `print(a)` 触发未定义函数错误后的死循环
- [x] `tests/parser_test.cpp`：补 `#include <windows.h>` / `<io.h>`（原仅 `<fcntl.h>`，`SetConsoleOutputCP`/`_setmode` 在 Windows 无法编译），使 `parser_tests` 可在 Windows 构建

> 复现与验证：`number a=1; print(a);`（语义挂起）与 `class Foo {}`（解析器挂起，输出 >20MB）修复后均正常退出。
> 回归验证：将 `parser.cpp` 回退到 HEAD 对比，`parser_tests` 前后**同为 5 通过 / 9 失败**，证明本次改动零回归；这 9 个失败是**预存**的解析器逻辑 bug（赋值/函数调用参数解析等），因 `parser_test.cpp` 此前在 Windows 无法编译而从未被执行到，待后续单独处理。
> `semantic_tests` 目标因**预存**损坏文件 `semantic_test.cpp`（使用过时 API：`Parser(lexer)`、`analyze(stmt.get())`）无法编译，语义恢复测试暂无法运行，待后续单独修复。

**词法器 UTF-8 bug（已修复：`lexer_tests` 从 4 通过 / 7 失败 → 11 全绿）**
- [x] `next_token` 改为 peek 分类不预消费，各 `scan_*` 自行消费定界符；非 ASCII（UTF-8 首字节 `>= 0x80`）派发到 `scan_identifier`
- [x] `scan_identifier` 改为逐 UTF-8 码点循环（`nextUtf8Char` + `isIdentifierChar`），支持中文标识符（`ChineseIdentifiers`/`UTF16Characters`/`StringLiterals`）
- [x] `scan_string` 不再重复消费首引号，单行分支对 `>= 0x80` 字节按整码点校验保留（修复 `Utf8Characters`/`UTF16Strings`），非法序列抛 `LexError`（`InvalidUtf8`）
- [x] `scan_string` 多行分支重写 dedent（以首行缩进为基准逐行 strip，缩进不足报错），`MultilineStrings` 通过
- [x] `scan_character` 改为 UTF-8 感知：ASCII → `LITERAL_CHAR`，多字节 → `LITERAL_CHARACTER`

### M3 · 工程化（CI）
- [x] GitHub Actions（`.github/workflows/ci-compiler.yml`）：Windows(MSVC) + Linux(gcc) 矩阵，cmake configure → build → ctest
- [x] 构建 `collie` + 绿色测试集并跑 `ctest`（`lexer_tests` + `interpreter_tests`；绕开预存红/不可编译的 `parser_tests`/`semantic_tests`，待 t9/t10 修复后纳入）
- [x] 缓存 `compiler/.deps`（GoogleTest 持久源码），避免每次联网克隆
- [ ] t9/t10 修复后，将 `parser_tests`/`semantic_tests` 纳入 CI 测试步骤

### M4 · 树遍历解释器（路线 A）→ 跑通 helloworld
- [x] 定义 `helloworld.collie` 的最小语义（`print` 为内建函数、字符串字面量转义沿用词法器已解码结果）
- [x] 新增 `interpreter` 模块（复用 AST 的 Visitor）：`Value`（运行期值）/`Environment`（作用域链）/`Interpreter`
- [x] 支持字面量/算术/变量/内建 `print`，输出 "Hello, World!"
- [x] 补齐 if/while/for、比较/逻辑运算（短路求值）、break/continue
- [x] 语义层识别内建 `print`（任意实参、返回 none），使流水线不再拦截
- [x] 为解释器建立端到端测试 `tests/interpreter_test.cpp`（.collie 源 → 期望输出，11 例全绿），并纳入 CI 绿色套件
- [ ] 用户自定义函数调用（当前抛 RuntimeError，见代码 TODO）
- [ ] 数字类型区分 integer/decimal（当前统一 `double`，见代码 TODO）

### M5 · 语言规范 & 语法闭环（持续）
- [ ] 沉淀一份「实际实现」为准的语言规范草稿
- [ ] parser 补齐已有 token 但缺失的语法（`switch`、`do-while`、`class` 等）

### M6 · LLVM 后端（路线 B，稳定后启动）
- [ ] 引入 LLVM，设计 AST/新 IR → LLVM IR 的降级
- [ ] 生成本地二进制，跑通 helloworld 的编译产物

---

## 四、模块解耦方案

现状：各模块（utils/lexer/parser/semantic）**已经是独立静态库**，依赖链为 `utils ← lexer ← parser ← semantic`，通过 `target_include_directories(PUBLIC)` 暴露头文件——这部分解耦已较合理。

主要耦合点在**测试**：`test_utils` 链接了全部库（含已删的 `ir`），且所有测试可执行文件都链接 `test_utils`，导致每个测试都传递依赖所有模块。

目标做法：
1. 每个测试目标只链接**被测模块**及其必要依赖；
2. `test_utils` 仅提供共享测试辅助，只链接测试框架；
3. 模块间**仅通过头文件 + 静态库**交互（保持现有 `PUBLIC` 用法要求）。

---

## 五、风险登记册

| 风险 | 级别 | 现状/对策 |
|------|------|-----------|
| IR 删除不干净导致 test 构建失败 | 🔴 高 | M0 修复（移除 `ir` 链接与已删源文件引用） |
| 跨平台是伪命题（无条件 `#include <Windows.h>`） | 🟠 中高 | M1 用 UTF-8 字节管线 + `#ifdef` 隔离 |
| 语义错误被静默吞掉（main 不检查 errors） | ✅ 已修复 | M2：`main.cpp` 检查 `has_errors()` 逐条上报并非零退出 |
| 语义分析对未声明函数调用（如 `print(a)`）疑似死循环卡住 | ✅ 已修复 | M2：parser 驱动循环加进度守卫、semantic `synchronize`/`advance_token` 防空防下溢 |
| `std::codecvt` 已弃用 | 🟡 中 | M1 随 UTF-8 管线一并移除 |
| 设计愿景 > 已实现，文档多为占位 | 🟡 中 | M5 以实现为准逐步沉淀规范 |
| 依赖在线拉取 GoogleTest | 🟢 低 | M0 改离线友好，评估 doctest |
| 无 CI / 无端到端测试 | 🟢 低 | ✅ CI 已加（M3，Windows+Linux 矩阵，跑 `lexer_tests`+`interpreter_tests`）；端到端测试已随 M4 解释器建立 |
| 数字类型统一用 `double`，未区分 integer/decimal | 🟡 中 | 解释器 `Value` 暂以 `double` 承载，代码记 TODO；类型系统闭环时再拆分 |

---

## 六、待与作者确认的语言设计问题

> 实现过程中遇到语法歧义会在此登记，逐条与作者确认后更新。

- [x] 源文件后缀：`.collie` 为主、`.col` 为别名 —— 已确认
- [x] helloworld 的 `print`：内建函数 `print(...)`（任意实参）；字符串转义 `\n \t \\ \"` 由词法器解码，解释器原样输出 —— 已确认
- [ ] `tribool`（三态布尔）与 `==?` 运算符的确切语义？
- [ ] tuple 成员访问语法（如 `.0` / `.1`）在词法层如何界定？
- [ ] `class` 是否纳入近期实现范围，还是先搁置？

---

## 七、变更日志

> 与 git 提交一一对应，最新在上。

- 2026-07-25 `feat(interpreter)`: 新增树遍历解释器模块（`Value`/`Environment`/`Interpreter`），支持字面量/算术/比较/逻辑/变量/if/while/for/break/continue 与内建 `print`；语义层识别内建 `print`、修复布尔字面量类型；清理 parser/semantic 调试打印；`main.cpp` 接入解释器并分离诊断输出（`-v`）；新增 `helloworld.collie` 示例与 `interpreter_tests`（11 例全绿）并纳入 CI（M4）
- 2026-07-25 `ci(compiler)`: 新增 GitHub Actions 工作流（Windows+Linux 矩阵，configure/build/ctest，首版跑 `lexer_tests`，缓存 `.deps`）（M3）
- 2026-07-25 `feat(main)`: 语义分析后检查 `has_errors()`，逐条打印错误并以非零码退出，不再静默报“Compilation successful!”（M2）
- 2026-07-25 `fix(parser,semantic)`: 修复前端两处死循环（parser 驱动循环进度守卫、semantic `synchronize`/`advance_token` 防空/防下溢），补 `parser_test.cpp` 的 Windows 头文件（M2）
- 2026-07-25 `fix(lexer)`: 修复 UTF-8 标识符/字符/字符串扫描（peek 分类、码点循环、UTF-8 校验、多行 dedent），`lexer_tests` 11 全绿（M2）
- 2026-07-25 `build(compiler)`: GoogleTest 源码缓存到 build 之外的持久 `.deps/`，删 build 后重配不再联网（离线优化）
- 2026-07-25 `refactor(lexer)`: 新增 `utf_convert.h` 手写 UTF-8↔UTF-16，移除 `token.h`/`lexer.cpp` 的 codecvt 与 Windows 专属 API（M1b）
- 2026-07-25 `refactor(compiler)`: `main.cpp` 改为二进制 UTF-8 字节读取，`#ifdef _WIN32` 隔离平台代码（M1a）

- 2026-07-24 `build(cmake)`: 退役旧自研 IR（删除 `ir/` 及相关测试），修复并解耦测试构建，GoogleTest 改为可选 + 离线友好
- 2026-07-24 `docs`: 新增本进度文档与 prompt 归档
