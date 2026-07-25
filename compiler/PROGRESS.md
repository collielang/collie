# Collie 编译器 · 开发进度与路线（Living Document）

> 这是一份**持续更新**的工程进度文档，记录当前状态、关键决策、阶段计划、风险与待确认的语言设计问题。
>
> **更新约定**：每完成或修复一块工作，就在对应里程碑打勾，并在文末「变更日志」追加一条（与 git 提交一一对应）。

最后更新：2026-07-25（t22 完成）

---

## 一、当前状态快照

编译流水线已打通到**树遍历解释器**：`helloworld.collie` 可实际执行并输出 `Hello, World!`（尚无代码生成 / LLVM 后端）。

| 阶段 | 状态 | 说明 |
|------|------|------|
| 词法分析 Lexer | ✅ 较成熟 | UTF-8/UTF-16、注释、多类字面量，token 种类丰富 |
| 语法分析 Parser | ✅ 基本可用 | 表达式、变量/函数声明、if/while/for/do-while/switch/block/return/break/continue、复合赋值(`+=`/`-=`/`*=`/`/=`/`%=`)、三元运算符(`?:`)、数组字面量与索引(`[1,2,3]`/`a[i]`) |
| 语义分析 Semantic | ✅ 相对完整 | 类型检查、隐式转换、函数重载打分、作用域、panic-mode 错误恢复 |
| **解释器 Interpreter** | ✅ 基本可用 | **树遍历解释器**：字面量/算术/比较/逻辑、变量声明与读写（含 const 保护）、if/while/for/do-while/switch、break/continue、内建 `print`/`len`/`toString`/`toNumber`、**用户自定义函数（声明/调用/return/递归）**、**数组（字面量/索引读写/负索引/引用语义）** |
| 中间代码 IR | ⛔ 已下线 | 旧自研 IR 实现质量不佳，正式移除，未来基于 LLVM 重做 |
| 优化器 Optimizer | ⬜ 未实现 | — |
| 目标代码 Codegen | ⬜ 未实现 | 计划 LLVM 后端 |

已知的语法「半截特性」（token 有、语法/语义未闭环）：`class`、`tribool`、`==?`、tuple 成员访问等。

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
| D9 | **`break`/`continue` 出现在循环外**，由**语义层**检测并报错，parser 层仅做语法解析（语法上接受） | 「是否在循环内」是上下文/语义约束而非纯语法；职责分离，符合主流编译器（如 Clang 作为 sema 错误）做法。相应负面用例移交语义测试 |
| D10 | parser 当前的函数声明采用 **`function` 关键字过渡文法**（`function add(a Num, b Num) Num {}`），文档 `reference/grammer/function.md` 描述的 **C 风格多返回类型**（`public int, string getAge()`）为**目标设计**，暂不改文档 | 文档代表设计方向，parser 尚未实现到该程度；保留文档意图，在此登记待对齐 TODO，避免文档退回过渡实现 |

---

## 三、里程碑与阶段计划

### M0 · 修复构建 & 模块解耦（进行中）
- [x] 修复 `tests/CMakeLists.txt`：移除对已删 `ir` 库的链接、移除已删的 `symbol_table_test.cpp`
- [x] 测试解耦：每个 test 目标只链接被测模块（lexer_tests→lexer，parser_tests→parser，semantic_tests→semantic）
- [x] 增加 `option(COLLIE_BUILD_TESTS ...)`，关闭后主编译器不依赖测试框架、无需联网
- [x] googletest 改为离线友好：`find_package` 优先 → 持久缓存 `.deps/`（build 之外，删 build 不重拉，`--depth 1` 首次克隆一次）→ `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` 完全离线覆盖；可用 `-DCOLLIE_DEPS_DIR=<path>` 指向全局共享缓存
- [x] 更新 `compiler/README.md` 中过时的进度描述（当前仍写 IR 已完成）

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
- [x] `main.cpp` 在语法分析后检查 `parser.get_errors()`：`parse_program` 采用错误恢复（不抛异常、返回部分 AST），若有语法错误则非零退出，修复“报完 Parse error 仍运行正确那部分”的静默执行
- [x] 为该门禁补回归测试：`parser_test.cpp` 新增契约测试（`parse_program` 记录错误+返回部分 AST、合法源无误报）；新增 CLI 端到端 ctest（`cli_valid_program`/`cli_syntax_error_gate`，直接跑 `collie` 可执行文件验证“报错即停、不输出 42”）
- [x] 有语义错误时以非零退出码结束，不再打印 "Compilation successful!"
- [x] 修复语义分析对未声明函数调用（如 `print(a)`）死循环

**前端死循环修复（parser + semantic，已修复：两处独立死循环不再卡死）**
- [x] `parser.cpp`：`parse_program` / `parse_block_statement` 驱动循环加**进度守卫**（记录游标前值，若本轮未推进则强制 `advance()`）——修复 `synchronize()` 停在无消费规则的边界关键字（如 `class Foo {}` 的 `KW_CLASS`）导致的死循环（原会输出 >20MB）
- [x] `semantic_analyzer.cpp`：`synchronize()` 在 `tokens_` 为空（主程序路径未调用 `set_tokens`）或已到序列末尾时直接 `exit_panic_mode` 并返回；`advance_token()` 增加 `!tokens_.empty()` 守卫防 `size()-1` 下溢——修复 `print(a)` 触发未定义函数错误后的死循环
- [x] `tests/parser_test.cpp`：补 `#include <windows.h>` / `<io.h>`（原仅 `<fcntl.h>`，`SetConsoleOutputCP`/`_setmode` 在 Windows 无法编译），使 `parser_tests` 可在 Windows 构建

> 复现与验证：`number a=1; print(a);`（语义挂起）与 `class Foo {}`（解析器挂起，输出 >20MB）修复后均正常退出。
> 回归验证：将 `parser.cpp` 回退到 HEAD 对比，`parser_tests` 前后**同为 5 通过 / 9 失败**，证明本次改动零回归；这 9 个失败是**预存**问题，因 `parser_test.cpp` 此前在 Windows 无法编译而从未被执行到。

**parser_tests 9 个失败修复（已完成：5 通过 / 9 失败 → 14 全绿，t9）**
- [x] 诊断分类：其中仅 1 个为真 parser bug、1 个错误恢复缺陷，其余 7 个为测试自身/设计对齐问题（并非路线图假设的「9 个 parser bug」）
- [x] `parser.cpp` 真 bug：`(expr)` 分组 vs `(a, b)` 元组消歧——先解析首表达式，再依是否出现逗号区分分组（透明返回）/元组，修复 `(a+b)` 被当单元素元组导致的双括号（`ComplexExpressions`）
- [x] `parser.cpp` 错误恢复：`parse_type_declaration` 初始化表达式解析失败时直接 `return nullptr`，交由 `parse_program` 继续，去掉二次 `synchronize_until` 吞掉后续正确语句的缺陷（`ErrorRecovery`）；删除死代码 `synchronize_until`/`parse_tuple_expr`/`is_literal_token`
- [x] `parser_test.cpp` 测试修复：`visitCall` 追加而非覆盖 `result_`（`FunctionCall`/`NestedFunctionCall`）、`visitBlock` 用 `indent()` 支持嵌套缩进（`NestedWhileStatement`）、修正 `BlockStatement`/`EmptyForStatement` 陈旧期望字符串、`FunctionDeclaration` 改用 `function` 关键字文法（对齐 parser，见 D10）、`ErrorRecovery` 改用 `parse_program`（见 Q3）
- [x] `break`/`continue` 循环外：按 D9 决策，parser 测试改为验证语法接受（产出 `BreakStmt`/`ContinueStmt` 不误报），负面用例移交语义测试
> `semantic_tests` 目标此前因**预存**损坏文件 `semantic_test.cpp`（过时 API：`Parser(lexer)`、`analyze(stmt.get())`）无法编译；t10 已修复该文件使目标恢复编译（详见下）。

**semantic_tests 恢复编译 + 分诊（t10，已完成）**
- [x] 重写陈旧的 `semantic_test.cpp`（过时 API：`Parser(lexer)`、`parse()` 返回单条、`analyze(stmt.get())`）为当前 API（`test::parse_and_get_tokens` + `analyze(vector)` + `has_errors()`），并按 D9 将其定位为**语义层 break/continue 循环上下文检查**（循环内合法、循环外报错，4 例全绿）
- [x] `semantic_tests` 目标恢复编译（4 个源文件全部编译链接通过）
- [x] **分诊完成**：对另外 3 个旧测试文件（`semantic_analyzer_test.cpp`/`semantic_error_test.cpp`/`semantic_recovery_test.cpp`）逐用例单跑取得 ground truth（**12 通过 / 37 失败**），失效根因如下：
    - 陈旧断言范式：普遍用 `EXPECT_THROW(analyze(...), SemanticError)`，但 `analyze()` 现已不抛异常、改为记录错误（应改查 `has_errors()`/`get_errors()`）
    - 面向目标设计而非当前实现：假设了 parser 未实现的文法（C 风格函数、`number[]` 数组类型）与未实现的语义检查（未初始化变量、不可达代码、返回类型/隐式转换/跨作用域），parse 阶段即报错
    - 两处硬崩溃：错误数不及预期却越界访问 `errors[3]`（`0xc0000409`），中断整轮测试
- [x] **分诊处置（用户决策：分诊+禁用）**：为 37 个失败用例加 `DISABLED_` 前缀暂停运行 + 每文件加分诊说明注释，作为文档化待办（待对应文法/语义实现后逐步恢复）；保留 12 个反映当前已实现行为的绿色用例。`semantic_tests` 现全绿（16 通过：12 语义 + 4 break/continue；37 禁用；无崩溃），纳入 CI 门禁

**词法器 UTF-8 bug（已修复：`lexer_tests` 从 4 通过 / 7 失败 → 11 全绿）**
- [x] `next_token` 改为 peek 分类不预消费，各 `scan_*` 自行消费定界符；非 ASCII（UTF-8 首字节 `>= 0x80`）派发到 `scan_identifier`
- [x] `scan_identifier` 改为逐 UTF-8 码点循环（`nextUtf8Char` + `isIdentifierChar`），支持中文标识符（`ChineseIdentifiers`/`UTF16Characters`/`StringLiterals`）
- [x] `scan_string` 不再重复消费首引号，单行分支对 `>= 0x80` 字节按整码点校验保留（修复 `Utf8Characters`/`UTF16Strings`），非法序列抛 `LexError`（`InvalidUtf8`）
- [x] `scan_string` 多行分支重写 dedent（以首行缩进为基准逐行 strip，缩进不足报错），`MultilineStrings` 通过
- [x] `scan_character` 改为 UTF-8 感知：ASCII → `LITERAL_CHAR`，多字节 → `LITERAL_CHARACTER`

### M3 · 工程化（CI）
- [x] GitHub Actions（`.github/workflows/ci-compiler.yml`）：Windows(MSVC) + Linux(gcc) 矩阵，cmake configure → build → ctest
- [x] 构建 `collie` + 全部绿色测试集并跑 `ctest`（`lexer_tests` + `parser_tests` + `semantic_tests` + `interpreter_tests` + CLI 端到端门禁 `cli_valid_program`/`cli_syntax_error_gate`）
- [x] 缓存 `compiler/.deps`（GoogleTest 持久源码），避免每次联网克隆
- [x] 将 `parser_tests`（t9 已全绿）与 `semantic_tests`（t10 分诊后绿色子集）纳入 CI 测试步骤

### M4 · 树遍历解释器（路线 A）→ 跑通 helloworld
- [x] 定义 `helloworld.collie` 的最小语义（`print` 为内建函数、字符串字面量转义沿用词法器已解码结果）
- [x] 新增 `interpreter` 模块（复用 AST 的 Visitor）：`Value`（运行期值）/`Environment`（作用域链）/`Interpreter`
- [x] 支持字面量/算术/变量/内建 `print`，输出 "Hello, World!"
- [x] 补齐 if/while/for、比较/逻辑运算（短路求值）、break/continue
- [x] 语义层识别内建 `print`（任意实参、返回 none），使流水线不再拦截
- [x] 为解释器建立端到端测试 `tests/interpreter_test.cpp`（.collie 源 → 期望输出，11 例全绿），并纳入 CI 绿色套件
- [x] **用户自定义函数调用与 return（t11，已完成）**：
    - `Value` 新增 `Function` 类型（持有 `FunctionStmt*`）
    - `visitFunction` 将函数登记到 `Environment`（与变量同层存储）
    - `visitCall` 查找用户函数、创建新作用域、绑定形参、执行体、捕获 `ReturnSignal`
    - `visitReturn` 求值表达式并抛出 `ReturnSignal`（内部信号，类似 `BreakSignal`）
    - 语义层修复：函数符号在体分析前定义（支持递归）；parser `consume_type_token` 接受类型关键字（number/string/bool/none）
    - 新增端到端测试：基本调用、字符串返回、递归（阶乘）、嵌套调用、void、局部变量、提前 return（7 例全绿，总计 18 例）
- [x] **恢复 DISABLED_ 语义测试第一批（t13，已完成）**：将 5 个 DISABLED_ 用例（`ControlFlow`、`Scopes`、`Functions`、`ReturnStatement`、`FunctionScope`）从旧 `EXPECT_THROW` 范式迁移到 `has_errors()`/`get_errors()` 新 API，改用 function 关键字语法，单独 analyzer 实例避免符号表累积；`semantic_tests` 现 21 通过 / 32 禁用（从 16/37）
- [x] **解释器 const 变量保护（t14，已完成）**：
    - [x] `Environment` 改为 `Binding{value, is_const}` 结构追踪 const 属性；新增 `is_const()` 查询
    - [x] `visitVarDecl` 将 `stmt.is_const()` 传递给 `env_.define()`
    - [x] `visitAssign` 赋值前检查 `env_.is_const()`，const 变量抛 RuntimeError
    - [x] parser `parse_declaration` 识别 `KW_CONST` 前缀并传递 `is_const=true` 给 `VarDeclStmt`
    - [x] 补充 interpreter 端到端测试（const 声明正常使用、const 重赋值报错，4 例全绿，总计 22 例）
- [x] **恢复 DISABLED_ 语义测试第二批 + 语义层 const 检查（t15，已完成）**：
    - [x] 语义层 `visitVarDecl` 新增检查：const 变量必须有初始化表达式
    - [x] 迁移 `DISABLED_BasicVariableDeclaration`（5 子用例：正确声明/重复声明/类型不匹配/const 声明/const 未初始化）
    - [x] 迁移 `DISABLED_TypeChecking`（3 子用例：算术运算/类型不兼容/非布尔条件）
    - `semantic_tests` 现 23 通过 / 30 禁用（从 21/32）
- [x] **语义层 const 重赋值检查 + 恢复 ConstAndInitialization（t16，已完成）**：
    - [x] `visitVarDecl` 创建 Symbol 时传递 `stmt.is_const()` （修复缺失的 is_const 传播）
    - [x] 迁移 `DISABLED_ConstAndInitialization` 前 3 子用例（const 声明/未初始化/重赋值）；剩余 2 子用例拆为 `DISABLED_UninitializedVariable` 待流分析实现
    - [x] 更新 interpreter `ConstAssignmentError` 测试（语义层已拦截，改为验证语义错误）
    - `semantic_tests` 现 24 通过 / 30 禁用（从 23/30）
- [x] **恢复 DISABLED_ 语义测试第三批 · 一元/二元操作符（t17，已完成）**：
    - [x] 拆分 `DISABLED_UnaryOperators`：数字取负/布尔取反恢复为 `UnaryOperators`（2 子用例），位取反(`~`)+类型错误(`!number`)拆为 `DISABLED_BitwiseNegateAndUnaryTypeCheck`
    - [x] 拆分 `DISABLED_BinaryOperators`：字符串连接/数值运算/比较运算/逻辑运算恢复为 `BinaryOperators`（4 子用例），char 字面量比较/位运算/类型错误检测拆为 `DISABLED_BitAndCharOperators`
    - `semantic_tests` 现 26 通过 / 30 禁用（从 24/30）
- [x] **实现 do-while 循环（t18，已完成）**：
    - [x] AST 新增 `DoWhileStmt` 节点 + `visitDoWhile` visitor 接口
    - [x] parser 识别 `KW_DO`，解析 `do { body } while (condition);` 语法
    - [x] 语义层 `visitDoWhile`：检查条件类型 + loop_depth_ 支持 break/continue
    - [x] 解释器 `visitDoWhile`：先执行体再检查条件，支持 break/continue 信号
    - [x] 端到端测试：基本循环/至少执行一次/break/continue（4 例全绿）+ 语义测试（break/continue in do-while 合法）
    - `interpreter_tests` 现 26 例全绿，`semantic_tests` 27 通过 / 30 禁用
- [x] **实现 switch 语句（t19，已完成）**：
    - [x] 词法层新增 `KW_DEFAULT` 关键字
    - [x] AST 新增 `SwitchCase` 结构 + `SwitchStmt` 节点 + `visitSwitch` visitor 接口
    - [x] parser 解析 `switch (expr) { value { } value1, value2 { } default { } }` 语法（无 case/break 关键字，无 fallthrough）
    - [x] 语义层 `visitSwitch`：检查条件和 case 值表达式
    - [x] 解释器 `visitSwitch`：匹配第一个等值分支执行，无匹配则执行 default
    - [x] 端到端测试：基本匹配/default/多值/字符串匹配/无匹配无default（5 例全绿）
    - `interpreter_tests` 现 31 例全绿
- [x] **实现复合赋值运算符（t20，已完成）**：
    - [x] 词法层新增 `OP_PLUS_ASSIGN`/`OP_MINUS_ASSIGN`/`OP_MULTIPLY_ASSIGN`/`OP_DIVIDE_ASSIGN`/`OP_MODULO_ASSIGN` 五个 token
    - [x] lexer 识别 `+=`/`-=`/`*=`/`/=`/`%=` 双字符运算符
    - [x] parser `parse_assignment` 脱糖为 `AssignExpr(name, BinaryExpr(name, op, rhs))`，无需修改语义/解释器
    - [x] 端到端测试：5 种运算符 + 字符串拼接 += + 循环累加（7 例全绿）
    - `interpreter_tests` 现 38 例全绿
- [x] **实现三元条件运算符（t21，已完成）**：
    - [x] AST 新增 `TernaryExpr` 节点 + `visitTernary` visitor 接口（token `OP_QUESTION`/`OP_COLON` 已存在）
    - [x] parser 新增 `parse_ternary`（优先级介于赋值与逻辑或之间，右结合）
    - [x] 语义层 `visitTernary`：条件必须 bool，两分支类型需兼容，结果类型取 then 分支
    - [x] 解释器 `visitTernary`：惰性求值（未命中分支不执行）
    - [x] 修正 `TypeInferenceRecovery` 测试期望（3→4 错误：三元解析成功后第 3 个预期错误真正进入语义分析）
    - [x] 端到端测试：true/false 分支、字符串、嵌套右结合、子表达式、惰性求值（6 例全绿）
    - `interpreter_tests` 现 44 例全绿
- [x] **内建函数 len/toString/toNumber（t23，已完成）**：
    - [x] 解释器新增 `call_builtin_len`（UTF-8 码点计数）/`call_builtin_to_string`（与 print 文本表示一致）/`call_builtin_to_number`（string/bool/number 转换，非法字符串报运行时错误）
    - [x] 语义层特判扩展：单参校验 + 固定返回类型（len→number 且仅接受 string、toString→string、toNumber→number）
    - [x] 端到端测试：ASCII/Unicode/空串 len、toString 数字/bool、toNumber 整数/小数/bool、链式组合（9 例全绿）
    - `interpreter_tests` 现 53 例全绿
- [x] **数组字面量与索引（basic 版，t22，已完成）**：
    - [x] 词法层：`token.cpp` keywords 表注册 `array` 关键字（枚举 KW_ARRAY 早已存在但 lexer 不识别）
    - [x] AST 新增 `ArrayLiteralExpr`/`IndexExpr`/`IndexAssignExpr` 三个节点 + visitor 接口；IndexExpr 提供 `take_object/take_index` 供 parser 安全重组
    - [x] parser：`array` 类型声明、数组字面量 `[1,2,3]`（支持尾逗号，依设计稿 draft.md）、后缀索引循环（支持链式 `m[0][1]`）、`arr[i] = v` 重组为 IndexAssignExpr
    - [x] `Value` 新增 Array 类型：`shared_ptr<vector<Value>>` 存储实现**引用语义**（`b=a` 后共享底层数组，与设计稿 `a[:]` 浅拷贝概念一致）
    - [x] 解释器：字面量求值、索引读写、**负索引**（`a[-1]` 为末尾）、越界抛 RuntimeError、`len(array)`、print 输出 `[1, 2, 3]`、数组相等深度比较
    - [x] 语义层：索引结果类型为 `object` 动态放行（is_compatible_type 双向放行、visitBinary 算术/比较放行，均带 TODO 待类型系统完善后收紧）
    - [x] 端到端测试：字面量打印/索引读/负索引/索引赋值/len/空数组/嵌套索引/引用语义/尾逗号/循环求和/越界报错（11 例全绿）
    - `interpreter_tests` 现 64 例全绿；未做：元素类型追踪、切片、索引复合赋值（`arr[i] +=`）
- [ ] 运行期声明类型与初始值类型校验（待排期）
- [ ] 数字类型区分 integer/decimal（当前统一 `double`，见代码 TODO）

### M5 · 语言规范 & 语法闭环（持续）
- [ ] 沉淀一份「实际实现」为准的语言规范草稿
- [ ] parser 补齐已有 token 但缺失的语法（~~`switch`~~、~~`do-while`~~、~~复合赋值`~~、`class` 等）

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
| 用户自定义函数不可执行 | ✅ 已修复 | t11 完成：Value+Function、ReturnSignal、visitFunction/visitCall/visitReturn，递归可用 |
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

- 2026-07-25 `feat(lexer,parser,semantic,interpreter)`: 实现数组字面量与索引（basic 版）：注册 `array` 关键字、AST 新增 ArrayLiteralExpr/IndexExpr/IndexAssignExpr、parser 支持字面量（尾逗号）/链式索引/索引赋值重组、Value 新增 Array（shared_ptr 引用语义）、解释器支持负索引/越界报错/len(array)、语义层 object 动态放行（带 TODO 收紧）；新增 11 个端到端测试；interpreter_tests 64 全绿（M4 t22）

- 2026-07-25 `feat(interpreter,semantic)`: 新增内建函数 len/toString/toNumber：解释器实现 UTF-8 码点计数、任意值转字符串、string/bool/number 转数字（非法报运行时错误）；语义层特判扩展单参校验+固定返回类型；新增 9 个端到端测试；interpreter_tests 53 全绿（M4 t23）

- 2026-07-25 `feat(parser,semantic,interpreter)`: 实现三元条件运算符 `?:`：AST 新增 TernaryExpr、parser 新增 parse_ternary（优先级介于赋值与逻辑或，右结合）、语义层条件 bool + 分支类型兼容检查、解释器惰性求值；修正 TypeInferenceRecovery 期望（3→4）；新增 6 个端到端测试；interpreter_tests 44 全绿（M4/M5 t21）

- 2026-07-25 `feat(lexer,parser)`: 实现复合赋值运算符（+=/-=/*=//=/%=）：词法层新增 5 个 token、lexer 识别双字符运算符、parser parse_assignment 脱糖为 AssignExpr+BinaryExpr（无需修改语义/解释器）；新增 7 个端到端测试；interpreter_tests 38 全绿（M4/M5 t20）

- 2026-07-25 `feat(lexer,parser,semantic,interpreter)`: 实现 switch 语句：新增 KW_DEFAULT 关键字、SwitchStmt AST 节点、parser 解析 Collie 风格 switch（无 case/break，值直接跟 {}）、语义层检查、解释器等值匹配无 fallthrough；新增 5 个端到端测试；interpreter_tests 31 全绿（M4/M5 t19）

- 2026-07-25 `feat(parser,semantic,interpreter)`: 实现 do-while 循环：AST 新增 DoWhileStmt、parser 解析 `do {} while ();`、语义层条件类型检查+loop_depth_、解释器先执行体再检查条件；新增 4 个解释器端到端测试+1 个语义测试；interpreter_tests 26 全绿，semantic_tests 27/30（M4/M5 t18）

- 2026-07-25 `test(semantic)`: 恢复一元/二元操作符 DISABLED_ 测试：拆分 UnaryOperators（数字取负+布尔取反通过，位取反+类型错误仍禁用）、BinaryOperators（字符串连接+数值+比较+逻辑通过，char/byte/类型错误仍禁用）；semantic_tests 26 通过 / 30 禁用（M4/t17）

- 2026-07-25 `fix(semantic),test(semantic,interpreter)`: 修复语义层 const 标记缺失（visitVarDecl 创建 Symbol 时传递 is_const），使语义层 visitAssign 的常量重赋值检查生效；恢复 `DISABLED_ConstAndInitialization` 前 3 子用例（const 声明/未初始化/重赋值），剩余 2 子用例拆为 DISABLED_UninitializedVariable；更新 interpreter ConstAssignmentError 测试为语义层拦截验证；semantic_tests 24 通过 / 30 禁用（M4/t16）

- 2026-07-25 `feat(semantic),test(semantic)`: 语义层新增 const 未初始化检查（`visitVarDecl` 检测 const 无初始化表达式并报错）；恢复 2 个 DISABLED_ 测试用例（`BasicVariableDeclaration` 5 子用例 + `TypeChecking` 3 子用例）从旧 EXPECT_THROW 范式迁移到 has_errors 新 API；`semantic_tests` 现 23 通过 / 30 禁用（M4/t15）

- 2026-07-25 `feat(interpreter,parser)`: 实现 const 变量保护：Environment 改为 Binding{value,is_const} 结构追踪常量属性；visitAssign 赋值前检查 is_const 并抛 RuntimeError；parser 识别 KW_CONST 前缀创建 is_const=true 的 VarDeclStmt；新增 4 个端到端测试（const 声明/读取/重赋值报错/mutable 对照），interpreter_tests 22 例全绿（M4/t14）

- 2026-07-25 `test(semantic)`: 恢复 5 个 DISABLED_ 语义测试用例（ControlFlow/Scopes/Functions/ReturnStatement/FunctionScope）：从旧 EXPECT_THROW 范式迁移到 has_errors/get_errors 新 API，改用 function 关键字语法，每子用例独立 analyzer；`semantic_tests` 现 21 通过 / 32 禁用（M4/t13）

- 2026-07-25 `docs(compiler)`: 重写过时的 `compiler/README.md`（旧内容仍写 IR 已完成），更新为当前架构（Lexer→Parser→Semantic→Interpreter）、项目结构、构建命令、CI 说明，M0 完结（t12）

- 2026-07-25 `feat(interpreter,parser,semantic)`: 实现用户自定义函数调用与 return：Value 新增 Function 类型、visitFunction 登记函数、visitCall 查找+绑参+执行体+捕获 ReturnSignal、visitReturn 抛出返回值；修复语义层函数符号定义顺序（支持递归）；parser 新增 `consume_type_token` 接受类型关键字；端到端测试 7 例全绿（M4/t11）

- 2026-07-25 `test(semantic),ci(compiler)`: 完成 3 个旧语义测试文件分诊（逐用例单跑得 12 通过 / 37 失败），为 37 个面向未实现特性的用例加 `DISABLED_` 前缀 + 每文件分诊注释（文档化待办），消除两处越界崩溃；`semantic_tests` 现全绿（16 通过 / 37 禁用）。CI 纳入 `parser_tests` + `semantic_tests` + CLI 端到端门禁（M3/t10）

- 2026-07-25 `test(semantic)`: 重写陈旧的 `semantic_test.cpp` 为当前 API（`parse_and_get_tokens`+`analyze(vector)`+`has_errors()`），按 D9 定位为语义层 break/continue 循环上下文检查（4 例全绿），`semantic_tests` 目标恢复编译；发现另 3 个旧语义测试文件大面积失效（陈旧 `EXPECT_THROW` 范式、面向未实现的目标文法/语义、2 处越界崩溃），登记为 t10 后续专项（M2/t10）

- 2026-07-25 `test(compiler)`: 为语法错误门禁补回归测试——`parser_test.cpp` 加契约测试（错误恢复后记录错误且返回部分 AST），`tests/fixtures/` + `tests/CMakeLists.txt` 新增 CLI 端到端 ctest（`cli_valid_program`/`cli_syntax_error_gate`，直接跑 `collie` 验证报错即停、不输出 42）

- 2026-07-25 `fix(main)`: 语法分析后检查 `parser.get_errors()`，有语法错误时非零退出，不再带着部分 AST 继续语义/解释（修复 `simple-code.collie` 报 Parse error 后仍输出 `42` 的静默执行，M2）

- 2026-07-25 `fix(parser)`: 修复 `(expr)` 分组 vs `(a,b)` 元组消歧、错误恢复吞后续语句缺陷，删 3 处死代码；对齐 `parser_test.cpp`（`visitCall`/嵌套缩进/陈旧期望、`function` 关键字文法、`parse_program` 恢复、`break/continue` 循环外改为语义层检测），`parser_tests` 14 全绿（t9，D9/D10）
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
