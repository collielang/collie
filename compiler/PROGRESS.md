# Collie 编译器 · 开发进度与路线（Living Document）

> 这是一份**持续更新**的工程进度文档，记录当前状态、关键决策、阶段计划、风险与待确认的语言设计问题。
>
> **更新约定**：每完成或修复一块工作，就在对应里程碑打勾，并在文末「变更日志」追加一条（与 git 提交一一对应）。

最后更新：2026-07-24

---

## 一、当前状态快照

整条编译流水线目前**止步于语义分析**，尚无法真正执行任何 Collie 程序（没有解释器，也没有代码生成）。

| 阶段 | 状态 | 说明 |
|------|------|------|
| 词法分析 Lexer | ✅ 较成熟 | UTF-8/UTF-16、注释、多类字面量，token 种类丰富 |
| 语法分析 Parser | ✅ 基本可用 | 表达式、变量/函数声明、if/while/for/block/return/break/continue |
| 语义分析 Semantic | ✅ 相对完整 | 类型检查、隐式转换、函数重载打分、作用域、panic-mode 错误恢复 |
| 中间代码 IR | ⛔ 已下线 | 旧自研 IR 实现质量不佳，正式移除，未来基于 LLVM 重做 |
| 优化器 Optimizer | ⬜ 未实现 | — |
| 目标代码 Codegen | ⬜ 未实现 | 计划 LLVM 后端 |
| **解释器 Interpreter** | ⬜ 未实现 | **近期首要目标**：树遍历解释器跑通 helloworld |

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
| D8 | 源文件后缀：主 `.collie`，别名 `.col`（**待作者确认**） | 与现有示例 `simple-code.collie` 一致，同时提供短后缀 |

---

## 三、里程碑与阶段计划

### M0 · 修复构建 & 模块解耦（进行中）
- [ ] 修复 `tests/CMakeLists.txt`：移除对已删 `ir` 库的链接、移除已删的 `symbol_table_test.cpp`
- [ ] 测试解耦：每个 test 目标只链接被测模块（lexer_tests→lexer，parser_tests→parser，semantic_tests→semantic）
- [ ] 增加 `option(BUILD_TESTING ...)`，未开启时主编译器不依赖测试框架
- [ ] googletest 改为离线友好（`find_package` 优先 + 可 vendored）
- [ ] 更新 `compiler/README.md` 中过时的进度描述（当前仍写 IR 已完成）

### M1 · 跨平台 & 编码统一
- [ ] `main.cpp` 改为二进制读取源文件、全程 UTF-8 `std::string`
- [ ] 移除 `std::codecvt` / `std::wstring_convert` 用法
- [ ] `#include <Windows.h>` 等平台代码用 `#ifdef _WIN32` 隔离
- [ ] 在 gcc/clang（Linux）下验证可编译

### M2 · 语义错误上报
- [ ] `main.cpp` 在语义分析后检查 `analyzer.has_errors()` 并打印 `get_errors()`
- [ ] 有语义错误时以非零退出码结束，不再打印 "Compilation successful!"

### M3 · 工程化（CI）
- [ ] GitHub Actions：Windows + Linux 矩阵，cmake configure → build → ctest

### M4 · 树遍历解释器（路线 A）→ 跑通 helloworld
- [ ] 定义 `helloworld.collie` 的最小语义（print 的形式、字符串字面量）
- [ ] 新增 `interpreter` 模块（复用 AST 的 Visitor）
- [ ] 支持字面量/算术/变量/print，输出 "Hello, World!"
- [ ] 逐步补齐 if/while/for/函数调用
- [ ] 为解释器建立端到端测试（.collie 源 → 期望输出）

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
| 语义错误被静默吞掉（main 不检查 errors） | 🟡 中 | M2 修复 |
| `std::codecvt` 已弃用 | 🟡 中 | M1 随 UTF-8 管线一并移除 |
| 设计愿景 > 已实现，文档多为占位 | 🟡 中 | M5 以实现为准逐步沉淀规范 |
| 依赖在线拉取 GoogleTest | 🟢 低 | M0 改离线友好，评估 doctest |
| 无 CI / 无端到端测试 | 🟢 低 | M3 加 CI，M4 加端到端测试 |

---

## 六、待与作者确认的语言设计问题

> 实现过程中遇到语法歧义会在此登记，逐条与作者确认后更新。

- [ ] 源文件后缀：`.collie` 为主、`.col` 为别名，是否可行？
- [ ] helloworld 的 `print`：是内建函数（`print("...")`）还是语句？字符串字面量的转义规则？
- [ ] `tribool`（三态布尔）与 `==?` 运算符的确切语义？
- [ ] tuple 成员访问语法（如 `.0` / `.1`）在词法层如何界定？
- [ ] `class` 是否纳入近期实现范围，还是先搁置？

---

## 七、变更日志

> 与 git 提交一一对应，最新在上。

- （待记录）
