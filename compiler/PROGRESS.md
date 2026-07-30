# Collie 编译器 · 开发进度与路线（Living Document）

> 这是一份**持续更新**的工程进度文档，记录当前状态、关键决策、阶段计划、风险与待确认的语言设计问题。
>
> **更新约定**：每完成或修复一块工作，就在对应里程碑打勾，并在文末「变更日志」追加一条（与 git 提交一一对应）。

最后更新：2026-07-31（t89 完成：codegen 嵌套数组放宽——visitArrayLiteral 两守卫解除（≥3 层/内层 bool-str）+ visitIndexAssign 整槽替换放宽为任意元素内层数组，rt 侧零改动，差分 41/41）

---

## 一、当前状态快照

编译流水线已打通到**树遍历解释器**：`helloworld.collie` 可实际执行并输出 `Hello, World!`（尚无代码生成 / LLVM 后端）。

| 阶段 | 状态 | 说明 |
|------|------|------|
| 词法分析 Lexer | ✅ 较成熟 | UTF-8/UTF-16、注释、多类字面量（含前导点小数 `.5`、`f` 后缀 `2f`、科学计数法、特殊数值 `Infinity`/`NaN`、插值字符串 `@"...{expr}..."`），token 种类丰富 |
| 语法分析 Parser | ✅ 基本可用 | 表达式、变量/函数声明、if/while/for/do-while/switch/block/return/break/continue、复合赋值(`+=`/`-=`/`*=`/`/=`/`%=`)、三元运算符(`?:`)、数组字面量与索引(`[1,2,3]`/`a[i]`)、方法调用(`n.toString()`，可与索引混合链式)、属性访问(`s.length`)、插值字符串脱糖(`@"a{x}b"` → `"a" + toString(x) + "b"`)、**class 声明（字段/方法/构造器）与 `new`/`this`/属性赋值、继承 `extends` 与构造器委托 `: base(args)`（脱糖为构造器体首条语句）、显式父类方法调用 `base.method(args)`（primary 层一次性解析，后缀链可接续）**、**元组字面量（`(1, 2)` 无名 / `(name: "Alice", age: 18)` 命名，`IDENTIFIER :` 前瞻）与 `Tuple` 类型** |
| 语义分析 Semantic | ✅ 相对完整 | 类型检查、隐式转换、函数重载打分、作用域、panic-mode 错误恢复 |
| **解释器 Interpreter** | ✅ 基本可用 | **树遍历解释器**：字面量/算术（取模为 **floor 语义**，Python 风格）/比较/逻辑、变量声明与读写（含 const 保护）、if/while/for/do-while/switch、break/continue、内建 `print`/`len`/`toString`/`toNumber`、**用户自定义函数（声明/调用/return/递归）**、**数组（字面量/索引读写/负索引/引用语义）**、**字符串索引（UTF-8 码点、负索引）**、**内建方法（toString/toNumber 通用，abs/integerPart/decimalPart/is* 系列 number 专属，trim/trimLeft/trimRight/subString string 专属）**、**length 属性（string 码点数/array 元素数）**、**Infinity/NaN 特殊数值（字面量/IEEE 754 运算含除零/toString 格式/toNumber 严格匹配）**、**class 基础支持（字段/构造器/方法/`new`/`this`/属性读写，实例引用语义）**、**class 继承（单继承，字段/方法沿继承链查找与覆写，字段初始化 base-first，base 构造器委托按定义类的父类解析，`base.method()` 显式父类方法调用绕过子类覆写）**、**运行期声明类型校验（变量/形参/类字段/返回值，string ← number/bool 隐式转换落地）**、**元组（不可变，`t[0]` 含负索引/`t.name`/`t.get("key")`/length/相等深比较）** |
| 中间代码 IR | ⛔ 已下线 | 旧自研 IR 实现质量不佳，正式移除，未来基于 LLVM 重做 |
| 优化器 Optimizer | ⬜ 未实现 | — |
| 目标代码 Codegen | ⬜ 未实现 | 计划 LLVM 后端 |

已知的语法「半截特性」均已闭环基础子集：`class`（t34）、`tribool`（t43）、`==?`（t44）、tuple（t45）。

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
- [x] **字符串索引（t24，已完成）**：
    - [x] 语义层 `visitIndex` 允许 string 被索引，结果类型为 string（设计稿：string 等价 char[]，待 char 进入 Value 后改返 char）；`visitIndexAssign` 对 string 报错（字符串不可变）
    - [x] 解释器：抽出 `utf8_length`/`utf8_char_at` 辅助（`len` 复用），`visitIndex` 按 **UTF-8 码点**索引返回单字符子串，复用 `normalize_index`（负索引/越界报错）；`visitIndexAssign` 防御 object 动态路径
    - [x] 端到端测试：索引读/负索引/Unicode 码点索引/表达式中使用/len 配合循环遍历/越界报错/索引赋值被拒（7 例全绿）
    - `interpreter_tests` 现 71 例全绿
- [x] **恢复 DISABLED_ 语义测试第四批 · 数组用例（t25，已完成）**：
    - [x] 迁移 `DISABLED_ArrayTypes`：改用 `array` 关键字语法 + has_errors 新 API（4 子用例：声明/访问/字符串索引报错/非数组索引报错）；元素同质性检查拆为 `DISABLED_ArrayElementTypeCheck`（待元素类型追踪）
    - [x] 迁移 `DISABLED_ArrayOperationRecovery`：错误后恢复继续分析后续数组操作（实测 3 错：索引类型 + 数组算术 + 级联赋值不兼容，与 panic-mode 机制一致）
    - [x] 修复 `token_utils.cpp` 缺失 `KW_ARRAY` 映射（错误消息中显示 UNKNOWN）
    - `semantic_tests` 现 29 通过 / 29 禁用（从 27/30）
- [x] **number 语义对齐设计规范（t26，已完成）**：
    - [x] 取模改为 **floor 语义**（Python 风格，结果符号随除数）：`-1 % 5 == 4`、`-1 % -5 == -1`、`1 % -5 == -4`，三用例均出自设计文档 04-numeric.md（原实现用 `std::fmod` C 截断风格，与文档不符）
    - [x] lexer 支持**前导点小数**（`.5`，文档示例 `-.123456`）与 **`f` 后缀**（`2f` 等价 `2.0`，后缀不计入 lexeme；`2fx` 中的 f 不被误吞）
    - [x] 端到端测试：floor 取模 3 文档用例/小数取模/字面量形式（3 例）+ lexer 字面量测试（2 例）
    - **范围决策**：int64/double 双内部表示**推迟**——文档中 number 的可观测语义（`1 == 1.0` 为 true、`1/3 → 0.333...`、整数显示不带小数点）当前 double 实现已全部满足，唯一需要双表示的 `12.00.toString(10) → "12.0"` 依赖尚未实现的方法调用语法；任意精度 integer/decimal 为独立大项，待类型系统闭环时一并设计
    - `interpreter_tests` 现 74 例全绿，`lexer_tests` 现 13 例全绿
- [x] **方法调用语法 `expr.method(args)`（t27，已完成）**：
    - [x] AST 新增 `MethodCallExpr` 节点 + `visitMethodCall` visitor 接口（三处实现同步：semantic/interpreter/parser_test）
    - [x] parser 后缀链重构：索引 `[i]` 与方法调用 `.method(args)` 可混合链式（`arr[0].abs()`、`3.7.integerPart().toString()`）；属性访问（无括号）暂不支持待后续设计
    - [x] 语义层内建方法表（均 0 参）：`toString`→string（任意接收者）、`toNumber`→number（string/bool/number）、number 专属 `abs`/`integerPart`/`decimalPart`→number、`isInteger`/`isDecimal`/`isNaN`/`isInfinity`/`isFinite`/`isPositive`/`isNegative`→bool；未知方法/接收者类型不符/带参均报错（object 动态放行）
    - [x] 解释器分发实现：`integerPart` 向零取整、`decimalPart` 保留符号（文档 `-123.456` 用例）；抽出 `to_number_value` 辅助与内建函数 toNumber 共用
    - [x] 端到端测试：number 谓词方法/abs 与取整/toString+toNumber（含数字字面量接收者 `12.toString()`）/链式混合/未知方法拒绝/错误接收者拒绝（6 例）
    - `interpreter_tests` 现 80 例全绿；未做：带参方法（如 `toString(radix)`，文档示例疑似有误待确认）、string/array 方法、属性访问
- [x] **string/array 内建成员（t28，已完成）**：
    - [x] AST 新增 `PropertyExpr` 节点 + `visitProperty` visitor 接口（三处实现同步）；parser `.` 后缀分支改为带括号方法调用/无括号属性访问双路径
    - [x] `length` 属性（文档 03-character.md `bar.length`）：string 按 UTF-8 码点计数、array 按元素数；其余接收者/未知属性语义层报错（object 动态放行）
    - [x] string 专属方法：`trim`/`trimLeft`/`trimRight`（空白字符为空格与 Tab，按文档）、`subString(start[, end])`（首个带参内建方法，语义层支持 1-2 元参数区间校验；`[start, end)` 码点区间，end 缺省/-1/NaN 取 length，越界截断）；解释器新增 `utf8_byte_offset` 辅助
    - [x] 端到端测试：length 属性/trim 系列/subString（含 UTF-8 多字节）/属性方法混合链式/未知属性拒绝/错误接收者拒绝/元数错误拒绝（7 例）
    - **待确认**：文档 `str.subString(6) // "llo world"` 示例与通用 `[start, end)` 语义矛盾（标准语义应为 `"world"`），按标准语义实现；`indent`/`dedent`/切片 `[a:b]`/`toCharacterArray` 推迟（依赖 character 类型或切片语法）
    - `interpreter_tests` 现 87 例全绿
- [x] **恢复 DISABLED_ 语义测试第五批 · 错误收集用例（t29，已完成）**：
    - [x] 恢复 `semantic_error_test.cpp` 4 个用例：`ErrorRecovery`/`ContinueAfterError`（`string x = 42;` 实为合法的 number→string 隐式转换，反转为 `number x = "42";` 构造真错误）、`FunctionErrors`（C 风格函数声明改写为 function 语法；3 处错误源实测 4 条——getString 级联 2 条：返回类型不匹配 + panic 后 return 不计入路径判定再报 must return）、`ErrorLocation`（同理反转类型方向，行号修正为 3——`R"(` 首行为空行；`EXPECT_EQ`→`ASSERT_EQ` 防错误数不符时越界崩溃）
    - [x] 盘点剩余禁用：`DISABLED_ArrayErrors` 依赖 `number[]` 语法；`semantic_analyzer_test.cpp` 10 个依赖未实现特性（byte/word/char 类型、未初始化流分析、不可达代码检查、C 风格函数）；`semantic_recovery_test.cpp` 14 个待下批评估
    - `semantic_tests` 现 33 通过 / 25 禁用（从 29/29）
- [x] **恢复 DISABLED_ 语义测试第六批 · 错误恢复用例（t30，已完成）**：
    - [x] 恢复 `semantic_recovery_test.cpp` 12 个用例：错误方向反转（string→number / number→bool 非法方向）+ C 风格函数改写为 function 语法（含 `none` 返回类型）+ 遮蔽合法化适配（ScopeLifetime/ResourceCleanup 内层同名变量遮蔽非错误，错误改用非法类型方向构造）
    - [x] 实测级联计数并注释：ComplexExpression 3 源 4 条、RecursiveFunction 3 源 6 条（含 must-return 双级联）、ErrorRecoveryPriority 实测 2 条（undefined 后 panic 跳过同表达式剩余检查 + 初始化级联）
    - [x] 仍禁用 2 个：`ComplexTypeConversionRecovery`（依赖 byte/word 类型）、`MemoryUsageRecovery`（1000 层深嵌套对递归下降 parser 有栈溢出风险且内存断言脆弱）
    - `semantic_tests` 现 45 通过 / 13 禁用（从 33/25）
- [x] **Infinity/NaN 特殊数值字面量（t31，已完成，见 04-numeric.md）**：
    - [x] 词法层：`Infinity`/`NaN` 入关键字表映射为 `LITERAL_NUMBER`（大小写敏感，小写拼写仍是普通标识符），parser/semantic 零改动
    - [x] 解释器：visitLiteral 显式特判取值（不依赖 stod 平台行为）；`Value::to_string` 按文档格式输出 `+Infinity`/`-Infinity`/`NaN`；算术/比较遵循 IEEE 754（NaN != NaN）
    - [x] `toNumber` 对齐文档：特殊形式 `"Infinity"/"+Infinity"/"-Infinity"` 严格大小写匹配；**不可解析字符串改为返回 NaN**（原报运行时错误；`"infinity".toNumber() == NaN`），stod 宽松拼写（inf/nan 等）一律视为不可解析
    - [x] 新增 6 个测试：lexer 1 个（token 分类）+ 端到端 5 个（toString 格式/谓词/NaN 比较/无穷算术/toNumber 形式）；lexer_tests 14、interpreter_tests 92 全绿
    - ~~遗留：除零仍报运行时错误~~（已经作者确认改为 IEEE 754，见 t33）
- [x] **字符串插值 `@"{expr}"`（t32，已完成，见 03-character.md）**：
    - [x] **语法勘正**：文档规定语法为 `@` 前缀 + `{expr}`（`@"{name} is {age}..."`），并非常见的 `${}`；VSCode 扩展 tmLanguage 佐证
    - [x] 词法层：新增 `LITERAL_INTERPOLATED_STRING` token，`@` + `"` 触发 `scan_interpolated_string`；lexeme 保留引号内**原文**（转义不解码，供 parser 区分 `\{` 与 `{`），UTF-8 校验、换行/EOF 报 Unterminated
    - [x] parser 端**脱糖**（AST/semantic/interpreter 零改动）：`@"a{x}b"` → `"a" + toString(x) + "b"`（BinaryExpr(+) 左结合链 + 内建 toString 包裹）；文本段转义解码额外支持 `\{` `\}` 输出字面花括号；插值段用子 Lexer/Parser 解析（天然支持任意表达式与嵌套插值），段内 `\"` 解码为引号支持 `@"{ cond ? \"a\" : \"b\" }"`；非法表达式/不配对花括号报 Parse error
    - [x] 新增 6 个测试：lexer 1 个（token 分类与原文 lexeme）+ 端到端 5 个（文档示例/表达式与三元/`\{` 转义/结果为 string 可拼接可调方法/非法表达式拒绝）；lexer_tests 15、interpreter_tests 97 全绿
- [x] **除零改为 IEEE 754 语义（t33，已完成，经作者确认）**：
    - [x] `1/0 → +Infinity`、`-1/0 → -Infinity`、`0/0 → NaN`（不再报 Division by zero 运行时错误），与 t31 的 Infinity/NaN 语义自然衔接
    - [x] 取模除数为 0 同样遵循 IEEE 754 返回 NaN（原报 Modulo by zero）
    - [x] 新增端到端测试 DivisionByZeroIEEE754；interpreter_tests 98 全绿
- [x] **class 基础支持 · Java/C# 风格最小子集（t34，已完成，经作者确认，见 uncategorized.md 附录）**：
    - [x] 文法：`class 名 { (public|private)? (字段|方法|构造器) ... }`；字段 `type name (= expr)?;`、方法复用 `function` 过渡文法、构造器为**与类名同名 + `(` 前瞻**识别（合成 none 返回类型复用 FunctionStmt）；缺省访问级别 public
    - [x] AST 新增 `PropertyAssignExpr`/`NewExpr`/`ThisExpr` 三节点 + visitor 接口（三处实现同步）；补 `ClassStmt::accept`（原缺失致链接错误）；词法层注册 `new`/`this` 关键字
    - [x] parser：`parse_class_declaration`/`parse_class_member`；`parse_assignment` 将 `obj.name = v` 由 PropertyExpr 重组为 PropertyAssignExpr（仿 IndexAssignExpr）；`parse_primary` 支持 `new X(args)` 与 `this`
    - [x] 语义层：**类实例统一按 object 动态放行**——`declared_classes_` 登记类名（重复定义报错）、`in_class_` 放行 this（类外报错）、`new` 未声明类报错、类名用作变量类型时转 KW_OBJECT；object 的属性/方法/字符串拼接均放行，字段/方法存在性与构造器元数推迟到运行期
    - [x] 解释器：`Value` 新增 Instance 类型（`shared_ptr<InstanceData>{klass, fields}` **引用语义**，与数组一致）；`classes_` 注册表；`new` 时字段按初始化表达式求值（缺省 none）→ 查同名构造器调用（无构造器要求 0 实参）；方法调用 ScopeGuard 内绑定 `this` + 形参、捕获 ReturnSignal；`toString` 保留为实例兜底方法
    - [x] 端到端测试 8 例：基本类（字段/构造器/方法/多实例）/无构造器/实例引用语义/方法互调（this.m(x)）/未定义字段与构造器元数运行期报错/未定义类与类外 this 语义层报错
    - `interpreter_tests` 现 106 全绿；未做：继承（extends/base）、静态成员、方法重载、类作为一等值、字段类型运行期校验
- [x] **运行期声明类型与初始值类型校验（t35，已完成）**：
    - [x] `Environment::Binding` 记录声明类型（缺省 KW_OBJECT 动态不校验），新增 `declared_type()` 查询
    - [x] 新增 `coerce_to_declared` 辅助：number/bool/array 严格匹配，string 接受 number/bool 隐式转字符串（语义层既有规则的运行期落地），object/类名动态放行；不兼容报 RuntimeError
    - [x] 四处接入：变量声明初始化、变量赋值、函数形参绑定、类方法/构造器形参绑定（修复 `string s = 42;` 后 `s.length` 误报——此前变量实际绑定的仍是 number）
    - [x] 修复语义层 `can_implicit_convert` 缺 object 动态放行（与 `is_compatible_type` 不一致，致动态实参传参被误拒）
    - [x] 端到端测试 7 例：声明/赋值/传参的 string 隐式转换 + 动态路径 number/bool/形参不匹配运行期拦截
    - `interpreter_tests` 现 113 全绿；遗留项均已闭环：类字段类型校验（t36）、函数返回值类型校验（t37）
- [x] **类字段类型运行期校验（t36，已完成，t35 遗留项闭环）**：
    - [x] 解释器新增 `find_field` 辅助（在类成员中查字段声明，与 `find_method` 对称）
    - [x] `visitNew` 字段初始化、`visitPropertyAssign` 字段赋值均接入 `coerce_to_declared`（含构造器内 `this.x = v` 路径）
    - [x] 修复 parser `consume_type_token` 缺 `object`/`array`：此前形参类型不能写 `object`/`array`（变量声明可以，属 parser 缺口）
    - [x] 端到端测试 4 例：字段初始化/赋值的 string 隐式转换、动态路径字段初始化/赋值类型不匹配运行期拦截
    - `interpreter_tests` 现 117 全绿
- [x] **函数返回值类型运行期校验（t37，已完成，coerce_to_declared 第五个也是最后一个接入点）**：
    - [x] `visitCall` 与 `call_class_method` 捕获 ReturnSignal 时返回值经 `coerce_to_declared`（仅校验显式 return 路径；无 return 返回 none 不校验；返回类型 none/void 走 default 分支自然放行）
    - [x] 端到端测试 3 例：函数/类方法返回值 string 隐式转换、动态路径返回值类型不匹配运行期拦截
    - `interpreter_tests` 现 120 全绿；至此运行期类型校验链闭环（声明/赋值/形参/字段/返回值）
- [x] **class 继承（t38，已完成）**：文法按 uncategorized.md 附录：`class Dog extends Animal`，构造器委托 `: base(args)`；最小子集：单继承、字段/方法沿继承链查找与覆写；`base.method()` 显式父类调用后续再做
    - [x] 关键字 `extends`/`base` 三处注册；AST 新增 `BaseCallExpr`，`ClassStmt` 加 superclass Token；parser 解析 extends 与 `: base(args)`（脱糖为构造器体首条 ExpressionStmt）
    - [x] 语义层：父类必须已声明、不能继承自身；visitBaseCall 类外报错
    - [x] 解释器：`find_method`/`find_field` 沿继承链查找（子类优先实现覆写）；`visitNew` 字段初始化沿链 base-first；`current_class_` 上下文（RAII 切换）使 base 按“定义构造器的类”的父类解析，多级继承逐级委托不死循环；构造器不继承、子类无构造器时不隐式调父类构造器
    - [x] 修复 visitBaseCall 中直接引用 `env_.get("this")` 返回指针的悬空问题（call_class_method 内 ScopeGuard 压栈可能重分配环境存储，先拷贝再传）
    - [x] 既有测试 `ClassMethodCallsMethod` 字段名 `base` 改为 `offset`（`base` 自 t38 起为保留关键字，与 C# 一致）
    - [x] 端到端测试 8 例：继承字段/方法、方法覆写、base 委托、多级继承 base 链、继承字段运行期类型校验、无父类写 base 运行期报错、父类未声明/继承自身语义报错；`interpreter_tests` 现 128 全绿，全量 ctest 6/6
- [x] **base.method() 显式父类方法调用（t39，已完成）**：语法按 C# 语义（文档未明示例子，但 base 关键字/@override 均按 C# 风格设计）：方法体内 `base.method(args)` 从“定义当前方法的类”的父类链开始查找，绕过子类覆写
    - [x] AST 新增 `BaseMethodCallExpr`（keyword/method/arguments）；parser 在 primary 层解析 `base . 方法名 ( 实参 )`（base 不是一等值，必须紧跟方法调用；返回后通用后缀链可接续 `.length` 等）
    - [x] 语义层：类外使用报错；返回类型 object 动态放行（与类实例方法调用一致）
    - [x] 解释器：从 `current_class_` 的父类链 find_method（绕过子类覆写），以 defining_class 上下文执行（多级 base 逐级上调不死循环）；this 仍为当前实例；沿用“先拷贝 this 再传”防悬空模式
    - [x] 端到端测试 7 例：覆写内调父类实现、带实参（this 为当前实例）、多级 base 链、父类未覆写时命中祖父类、父类链无方法运行期报错、无父类写 base 运行期报错、类外写 base 语义报错；`interpreter_tests` 现 135 全绿，全量 ctest 6/6
- [x] **@override 注解（t40，已完成）**：文档列举 `@override`/`@deprecated`（uncategorized.md 注解节）
    - [x] lexer 新增 ANNOTATION token（`@名字`，lexeme 不含 `@`，与插值字符串 `@"` 分支共存）
    - [x] parser：`classMember -> annotation* 修饰符? 成员`；`@override` 仅可标注方法（字段/构造器报语法错）；`@deprecated` 接受暂不生效（TODO 调用处告警）；未知注解报语法错；`FunctionStmt` 增 `is_override` 标志
    - [x] 语义层：`declared_classes_` 升级为 `unordered_map<string, const ClassStmt*>`，新增 `find_method_in_hierarchy`；`@override` 方法校验父类链确有同名方法，否则 record_error（不中断成员分析）
    - [x] 端到端测试 7 例：覆写标注正常运行、命中祖父类方法、@deprecated 无副作用、父类链无同名方法/无父类语义报错、标注字段语法报错、未知注解语法报错；`interpreter_tests` 现 142 全绿，全量 ctest 6/6
- [x] **数字类型 number/integer/decimal 区分（t42，已完成）**：经作者确认，只保留三类型，Python 式自动扩容（integer 任意精度，使用者无需考虑溢出/精度）；定宽类型留待后续单独实现
    - [x] 解释器新增 `BigInt`（base 2^32 limbs，schoolbook 乘法 + 二进制长除 + floor_mod）；`Value` 双表示（Kind::Number 内 `num_is_int_` 标志，整数 BigInt / 小数 double）
    - [x] lexer：f 后缀计入 lexeme（下游据 lexeme 中的 '.'/'e'/'f' 判定 integer/decimal）
    - [x] 语义层：字面量推导（无 '.'/'e'/'f' 且非 Infinity/NaN → integer，否则 decimal，Infinity/NaN → number）；integer → decimal/number 隐式加宽，decimal → integer 拒绝，number → integer/decimal 静态放行运行期校验；`/` 恒产 decimal（Python true division）；common_type 含 decimal → decimal；len/length → integer
    - [x] 解释器：整数字面量走 BigInt 不经 double；双整数 `+ - * %` 精确路径（% 为 floor 语义，除零/模零仍落 IEEE 754 路径得 Infinity/NaN）；比较/相等双整数精确；coerce_to_declared 支持 integer（拒小数值）/decimal（整数加宽）；toNumber 整数串/bool → integer；整数的 abs/isInteger 等方法走 BigInt 精确路径
    - [x] 端到端测试 11 例：超大整数精确算术/比较、除法恒 decimal、大整数 floor 取模、integer/decimal 声明与加宽、混合算术、大整数方法、toNumber 精确转换、decimal→integer 语义拒绝、number→integer 运行期校验两例；`interpreter_tests` 现 153 全绿，全量 ctest 6/6
- [x] **tribool 三态布尔类型闭环（t43，已完成，经作者确认语义）**：
    - [x] `unset` 字面量（parser primary + 语义推导 KW_TRIBOOL）；Value 层 Tribool 三态表示（`Tri` 编码 False=0 < Unset=1 < True=2，使 Kleene AND/OR 退化为 min/max）；`coerce_to_declared` 支持 tribool（接受 bool 加宽，反向 tribool → bool 拒绝）
    - [x] Kleene 三值逻辑（`! && ||`，保留短路：AND 遇 false、OR 遇 true；任一操作数 tribool → 结果 tribool）；`if/while/for/do-while` 条件必须 bool（语义层拦截 + 运行期 `condition_truthy` 防御动态路径）
    - [x] tribool 内建方法 `isTrue()/isFalse()/isUnset()` 返回 bool；`==`/`!=` 支持与 true/false/unset 比较（三态一致才相等）
    - [x] 三分支三元 `a ? 1 : 2 : 3`（要求 tribool 条件；两分支时 unset 走 false 分支，见文档）；新增 11 个端到端测试，全量 ctest 6/6
- [x] **`==?` 通用多路匹配运算符（t44，已完成，经作者确认语义与消歧义规则）**：
    - [x] AST 新增 MultiMatchExpr（目标 + 分支列表（候选值组 → 结果）+ 可选默认分支）；parser 在 parse_ternary 层解析 `a ==? v1, v2: r1, v3: r2, default_r`（末尾裸表达式 = 默认分支，非末尾未归组裸值报语法错误）
    - [x] 语义：候选值与目标可 `==` 比较（object 动态放行）；tribool 无默认时需字面量穷尽三态，其他类型必须有默认分支；结果类型取首分支（各分支须兼容）
    - [x] 解释器：按序比较候选值（values_equal），命中第一个匹配分支，否则默认分支；惰性求值（未命中分支不执行）
    - [x] 新增 8 个端到端测试（穷尽三态/归组/默认分支/通用匹配/首命中+惰性/三项语义拒绝）；全量 ctest 6/6；uncategorized.md 示例 5 已同步修正
- [x] **tuple 最小闭环（t45，已完成，经作者确认访问语法）**：按索引访问用 `t[0]`（复用索引语法）、命名字段用 `t.name`（复用属性语法）、动态获取用 `t.get("key")`（复用方法语法）；旧半截代码的 `.0` 数字下标语法废弃（消解与前导点小数 `.5` 的词法冲突）
    - [x] 词法层注册 `Tuple` 关键字（KW_TUPLE 枚举早已存在但 keywords 表未注册，同 KW_ARRAY 旧坑）+ token_utils 映射
    - [x] parser：命名元组字面量 `(name: "Alice", age: 18)`（`IDENTIFIER :` 前瞻，首元素带名时无逗号也是元组）；`Tuple` 声明/形参/返回类型；删除死代码 parse_postfix/parse_tuple_type/parse_type 与 TupleMemberExpr；TupleExpr 改为 elements + names 平行向量（无名元素为空串）
    - [x] 语义层：KW_TUPLE 类型放行（索引/命名属性/get 方法（固定 1 参）/length）；索引赋值拒绝（"Tuples are immutable"）；删除单槽 tuple_element_types_ 旧机制
    - [x] 解释器：Value 新增 Tuple（不可变，shared_ptr 存储元素 + 平行名字表）；索引读（含负索引）/命名字段读/get("key")/length/toString `(1, 2)` 与 `(name: Alice)` 格式；values_equal 元素+名字表都一致才相等；coerce_to_declared 支持 Tuple；索引赋值运行期防御
    - [x] 新增 15 个端到端测试（字面量/索引/命名字段/单命名元素/get 动态键/length/toString/混合元素/相等比较/函数返回/分组不受影响/四项拒绝）；全量 ctest 6/6

### M5 · 语言规范 & 语法闭环（持续）
- [x] **沉淀一份「实际实现」为准的语言规范草稿（t46，已完成）**：
    - [x] 从代码盘点实现面（关键字表/运算符与优先级/字面量/语句/类型与转换规则/内建函数方法表/class 文法/执行模型）
    - [x] 撰写 `compiler/SPEC.md` 规范草稿（十章 + 「已知实现缺口」专节 G1–G10：位运算 token 未解析、char/byte 等类型仅声明放行、形参/返回类型缺 integer/decimal/tribool、运行期无重载分发、访问修饰符不强制等）；凭记忆表述逐条回查代码修正（print 分隔符/小数输出格式/null 兜底现状）
    - [x] 风险表过时条目清理（double 单表示已被 t42 解决；文档占位风险更新为 SPEC.md 已建立）
- [ ] parser 补齐已有 token 但缺失的语法（~~`switch`~~、~~`do-while`~~、~~复合赋值`~~、~~`class`（基础子集）~~ 等）
- [x] **char/byte 类型 + 位运算符（t47，已完成）——补齐缺口 G1**：
    - [x] 现状盘点：语义层 `visitBinary`/`visitUnary` 已完整处理 `OP_BIT_*`，缺口在 parser（无位运算优先级层、`~` 未接入 `parse_unary`、char 字面量未接入 `parse_primary`）、lexer（无十六进制字面量 `0xFF`）、interpreter（无位运算求值）
    - [x] lexer：`scan_number` 支持 `0x`/`0X` 十六进制整数字面量（lexeme 保留前缀原文，含 E/f 十六进制位不被误判小数；缺十六进制位报错）
    - [x] parser：新增位运算优先级层（`|` < `^` < `&` < 相等 < 关系 < 移位 `<< >>` < 加法，C 家族一致），`~` 接入 `parse_unary`；顺带补上 `parse_primary` 接受 char 字面量（`'a' >= 'b'` 此前是解析错误）
    - [x] 语义：`is_bit_type` 纳入 `integer`（hex 字面量为 integer），`is_compatible_type` 允许 `integer → byte/word`（整数精确，运行期由 coerce_to_declared 做范围校验 byte 0-255、word 0-65535）
    - [x] interpreter：`~` 走 BigInt 精确取反（`~x = -x-1`），`& | ^ << >>` 走 int64 路径求值（超 64 位报运行时错误不静默截断；移位数限 0-63；负数左移走无符号域回避 UB）；coerce_to_declared 新增 byte/word 范围校验
    - [x] 恢复禁用测试 3 个：`BitwiseNegateAndUnaryTypeCheck`、`BitAndCharOperators`（EXPECT_THROW→has_errors 范式迁移）、`ComplexTypeConversionRecovery`（`string s1 = n` 期望与 t35 隐式转换政策冲突，换用 `tribool ← number` 保持 3 错）；`DISABLED_ArrayErrors`（依赖未实现的 `type[]` 泛型数组语法）、`DISABLED_MemoryUsageRecovery`（内存剖析，与本任务无关）保持禁用并说明；`TypeConversionErrors` 适配为 5 错（`&` 可解析后 panic 级联一条，与既定恢复语义一致）
    - [x] 新增测试：lexer 2 个（hex 字面量/缺位报错）+ 解释器端到端 6 个（位运算求值/优先级/byte・word 声明/超界拦截×2/移位数越界）；全量 ctest 6/6

### M6 · LLVM 后端（路线 B，稳定后启动）
- [ ] **引入 LLVM，设计 AST/新 IR → LLVM IR 的降级（t48，进行中）**：
    - [x] 环境排查 + 安装方案拍板（t48a）：作者选定**官方预编译包**（`clang+llvm-22.1.8-x86_64-pc-windows-msvc.tar.xz`，解压至 `D:\Program\Development\Environment\llvm-21`）；下载步骤已写入贡献文档 compile-and-run（英文 + 中文 i18n，含「bin 加 PATH 可选」说明）；交叉编译/分发需求不锁死：各平台 CI 用各自渠道的 LLVM，target 后端预编译包全启用，静态链接自包含
    - [x] LLVM 依赖接入 CMake + 冒烟验证（t48b）：顶层 `COLLIE_ENABLE_LLVM` 选项（默认 OFF，不影响既有门禁/CI）+ `find_package(LLVM CONFIG)` + codegen 子目录（EXCLUDE_FROM_ALL）；`llvm_smoke` 工具 IRBuilder 构造 hello world 模块、verifyModule、打印 IR 全链路通过；踩坑两个：①官方包是 **/MT 静态 CRT**（非 /MD），需 CMP0091 NEW + 目标级 MSVC_RUNTIME_LIBRARY 对齐；② `LLVMConfig.cmake` 会改写顶层 `CMAKE_MSVC_RUNTIME_LIBRARY` 污染其后目标（collie 主程序 Debug 被带成 /MT Release 报 LNK2038），find_package 前后保存/恢复；回归全量 ctest 6/6
    - [x] AST → LLVM IR 降级设计文档（t48c）：产出 `compiler/codegen/README.md`（以 SPEC.md 为语义依据）：阶段范围 S1 helloworld / S2 整数算术 / S3 变量控制流；类型映射（integer→i64 妥协、decimal→double）；降级映射表（print→puts/printf、`/` 恒小数 fdiv、`%` floor 取模 select 校正）；关键拍板：Release 配置全工程切 /MT 与 LLVM 对齐（Debug 门禁不动，t49 实施）；验证靠解释器/编译产物差分测试；缺口另开 CG* 编号登记（CG1 i64 非任意精度等 4 项）
- [x] **CodeGenVisitor 第一版 + 生成本地二进制，跑通 helloworld 的编译产物（t49，S1/S2）**：
    - [x] 工程级 CRT 规则落地（t49a）：顶层 `CMAKE_MSVC_RUNTIME_LIBRARY` 改为 Debug=/MDd、**Release=/MT**（与 LLVM 官方包对齐，发布产物自包含）；前端四库 Release 产物可直接与 LLVM 混链，Debug 门禁/gtest/CI 不动
    - [x] CodeGenerator（t49b）：实现 ExprVisitor/StmtVisitor 全接口，S1/S2 范围内降级（print→单次 printf（格式串编译期拼接，空格分隔+换行与解释器对齐）、整数→i64、`/` 恒小数 fdiv、`%` floor 取模 select 校正、一元负号）；范围外节点显式报 CodeGenError 绝不静默错编；verifyModule 门禁；模块显式设宿主 triple
    - [x] colliec 驱动（t49c）：前端门禁（词法/语法/语义与 collie 主程序同标准）→ 写 .ll → 调 LLVM 包自带 clang 编链为 .exe（COLLIE_LLVM_BIN 烘焙自 LLVM_TOOLS_BINARY_DIR）；`--emit-llvm`/`-o` 选项
    - [x] 验证（t49d）：helloworld+算术 5 语句用例编译执行，与解释器输出 `fc` 逐字节一致（差分测试首次落地）；回归全量 ctest 6/6
- [x] codegen 扩展 S3 + 差分测试自动化进测试体系（t50，S3）
    - 范围拍板：变量声明（integer/decimal/bool/string，须带初始化）、赋值（integer→decimal 隐式提升）、比较 `== != < <= > >=`、逻辑 `&& || !`（短路，与解释器对齐）、if/else、while、块作用域遮蔽
    - 范围外登记：`number` 变量需整数/小数双表示（缺口 CG5，待运行时垫片/标记表示）；tribool/for/break/continue/函数 后续阶段
    - 差分测试自动化：Release 专属 ctest（CONFIGURATIONS Release）+ `cmake -P` 比对脚本（colliec 编译产物 vs collie 解释器输出）
    - 实现：CGVar{alloca 槽,类型} + scopes_ 作用域栈（entry 块头 alloca 利于 mem2reg）；比较纯整数 icmp/含小数 fcmp（!= 用 UNE 保 NaN）；&&/|| 短路 condbr+phi；if/while 标准基本块 + 终结符防御；bool 字面量 true/false 是 KW_TRUE/KW_FALSE
    - 踩坑：EXCLUDE_FROM_ALL 子目录 add_test 不进 CTestTestfile → 差分测试注册挪 tests/；`codegen` 是 CMake 保留目标名（CMP0171）→ 改名 collie_codegen；语义层拒整数/小数混型字面量比较（用例改用 decimal 变量）
    - 验证：ctest -C Release 差分 s1_hello+s3_control_flow 各 100% 逐字节一致；Debug 门禁全量 ctest 6/6 不受影响（codegen_diff CONFIGURATIONS Release 不进 Debug）
- [x] codegen S4 循环控制流补全（t51）
    - 范围拍板：`for`（初始化/条件/增量作用域限循环内）、`do-while`、`break`/`continue`（loop 上下文栈记录 continue/break 目标块）、二元三元表达式 `a ? x : y`（bool 条件，PHI 汇合，分支类型不同时 int→double 提升）
    - 范围外登记：三分支 tribool 三元 `a ? x : y : z`（属 tribool 后续）；switch/函数/class 仍拒编
    - 实现：LoopContext{continue_target, break_target} 栈 loops_；for 初始化限自身作用域 + cond/body/inc/end 四块（continue 跳 inc，与解释器 continue 后仍执行增量对齐）；do-while 先 br body 保至少执行一次；break/continue CreateBr 后落 `*.dead` 死代码块（IR 每块仅一终结符）；gen_ternary then/else/merge + PHI，混型提升指令落在各自分支块内
    - 踩坑：parser parse_for_statement 初始化类型匹配列表历史缺口（仅 number/string/bool/character/IDENTIFIER，`for (integer i = ...)` 直接语法错误）——对齐 parse_declaration 完整类型列表修复，解释器/编译器共同受益，并补 parser 防退化测试 ForStatementIntegerInitializer
    - 验证：新增差分用例 s4_loops.collie（for 累加/continue/break/do-while/while 内 break-continue/三元/嵌套三元），ctest -C Release 差分 3/3 逐字节一致；Debug 门禁 ctest 6/6 全过
- [x] codegen S5 函数支持（t52）
    - 范围拍板：顶层 `function name(param type, ...) retType { ... }` 声明与调用、`return`、递归；参数/返回类型限 integer/decimal/bool/string，`none` 返回降级 void；两遍处理（先建全部原型再生成函数体，递归天然可用）；实参按形参类型 coerce（integer→decimal 提升）
    - 范围外登记：嵌套函数、同名重载（语义层支持但 codegen 拒编）、函数作值传递；非 none 函数可达末尾无 return 拒编（不可达尾块补 unreachable）
    - 实现：CGType 新增 Void（none 返回函数调用结果）；CGFunction{fn,param_types,ret_type} 表 functions_；generate() 第一遍 declare_function 建全部原型（符号名 `collie.<name>` + InternalLinkage）；visitFunction 现场保存/恢复（插入点/scopes_/loops_）后用全新作用域生成函数体，形参 entry alloca+store 落栈；visitCall 查表 + 实参 int→double coerce；visitReturn 求值+coerce 后落 ret.dead 块；gen_print/gen_ternary 加 Void 防御
    - 踩坑：①parser consume_type_token 类型关键字列表历史缺口（缺 KW_INTEGER/KW_DECIMAL/KW_TRIBOOL/KW_DWORD/KW_BIT）→ `(a integer, ...)` 参数/返回类型语法错误，补齐并加防退化测试 FunctionDeclarationBuiltinTypeParams（继 t51 parse_for_statement 后第二个同类缺口）；②尾块可达性用 `pred_empty` 启发式失效——if/else 双分支均 return 时各 dead 块被 visitIf 补 br 到 merge，merge 有前驱但从 entry 不可达 → 改 reachable_from_entry 从 entry DFS 遍历 successors 真实判定
    - 验证：新增差分用例 s5_functions.collie（add 多参/fib 递归/mix 混型/greet none 副作用/absval 双分支 return），ctest -C Release 差分 4/4 逐字节一致；Debug 门禁 ctest 6/6 全过
- [x] collie_rt 运行时垫片第一版（t53）
    - 范围拍板：纯 C 静态库 `codegen/runtime/collie_rt.c`（collie_rt_print_str/i64/f64/bool/sep/newline 逐参打印接口）；f64 移植解释器 Value::to_string 四步格式化（NaN → ±Infinity → 整值<1e15 按整数 → 其余 %g 与 ostringstream 默认一致）；gen_print 改逐参调用 collie_rt（不再直连 printf/puts）；colliec clang 命令行追加 collie_rt.lib
    - 关键发现：CG2 真实差异不止 Infinity/NaN 拼写——整值大数 3000000.0 解释器打 3000000 而 %g 打 3e+06；纯 C 实现避免 clang 链 .ll 时不自动带 C++ 标准库的坑
    - 踩坑：①COLLIE_RT_LIB 宏烘焙绝对路径方案失败——构建树路径含中文时宏值经 MSVC 命令行编码错乱（clang 收到乱码路径找不到 lib），改为 colliec 运行期 GetModuleFileName 从自身目录定位 collie_rt.lib（两目标同目录产出）；②windows.h 的 min/max 宏污染 LLVM 头文件→必须 NOMINMAX + WIN32_LEAN_AND_MEAN
    - 验证：新差分用例 s6_print_format（整值大数/非整值/科学计数/1.0÷0.0/0.0÷0.0/混合行），ctest -C Release 差分 5/5 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen string 运行时第一步（t54）
    - 实现：collie_rt 新增 collie_rt_concat（malloc 拼接）/i64_to_str/f64_to_str（与 print_f64 共享四步格式化 helper）/bool_to_str（静态串）；codegen visitBinary OP_PLUS 任一侧 Str 走拼接路径（非 Str 侧经 to_str 隐式转串，与解释器任一侧 string 即拼接对齐）；visitCall 新增内建 toString（单参转串，分发先于用户函数查表，与解释器一致）→ 插值脱糖产物（字面量 + toString(expr) 左结合 + 链）自然打通
    - 范围外：len/toNumber 内建、tribool 拼接（语义层本就拒绝）；BigInt 大整数拼接精度属 CG1 延伸（i64 回绕容忍）；新登记缺口 CG6：拼接串 malloc 后不 free，短生命周期编译产物暂容忍泄漏
    - 验证：新差分用例 s7_string_concat（串+串/串+整数/串+小数四步格式/±Infinity/NaN 拼接/串+bool/显式 toString/插值 @"{expr}"/string 变量拼接赋值/函数返回拼接），ctest -C Release 差分 6/6 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen string 比较运算（t55）
    - 实现：collie_rt 新增 collie_rt_strcmp（ptr×ptr→i32，strcmp 语义）单接口覆盖六种比较；codegen visitBinary 比较 case 在 bool 特判/require_numeric 前加 Str×Str 分支（call 后与 0 做对应 icmp EQ/NE/SLT/SLE/SGT/SGE）；逐字节字典序与解释器 std::string 比较一致，无需 UTF-8 特殊处理
    - 范围外：Str×非 Str 混型比较（解释器运行期本就 RuntimeError，codegen 维持 require_numeric 拒编）；char 到 codegen 已是 Str 形态自然合流（G2 行为一致）
    - 附带：SPEC.md §4.4 补 string 关系比较（逐字节字典序）条目（解释器早已实现但规范未登记）
    - 验证：新差分用例 s8_string_compare（==/!=/</<=/>/>= 正反例、空串、前缀序、拼接结果参与比较、if 条件/三元/函数参数中使用），ctest -C Release 差分 7/7 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen string length 属性 + 索引 s[i]（t56）
    - 实现：collie_rt 新增 collie_rt_str_len（UTF-8 码点计数→i64，照抄解释器 utf8_length/utf8_char_length 首字节步进算法）+ collie_rt_str_index（负索引归一化，越界 stderr 报 "Index N out of range (size S)" 后 exit(1)，返 malloc 单码点子串，对齐解释器 normalize_index/utf8_char_at）；codegen visitProperty 支持 Str 的 length（→Int），visitIndex 支持 Str×Int（→Str），其余接收者/索引类型维持拒编
    - 范围外：array/tuple 索引与 length（对应类型 codegen 未支持）；trim/trimLeft/trimRight/subString 方法（留后续任务）；Double 索引拒编不错编（解释器非整数索引运行期报错）
    - 验证：新差分用例 s9_string_index（ASCII/中文多字节 length、正/负索引、首尾码点、索引结果拼接/比较、拼接产物再索引、循环遍历、函数参数中使用），ctest -C Release 差分 8/8 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen string 方法（t57）
    - 实现：collie_rt 新增 collie_rt_str_trim（ptr×i32 mode⇒ptr，mode 0=两端/1=左/2=右，只剥空格与 Tab，对齐解释器 is_blank）+ collie_rt_str_substring（ptr×i64×i64⇒ptr，UTF-8 码点区间 [start,end)，end==-1 特判取 length，clamp 截断、start>=end 空串，对齐解释器 subString）；codegen visitMethodCall 接入：Str 接收者的 trim/trimLeft/trimRight（0 参）与 subString（1-2 参，参数限 Int，缺 end 传 -1）；任意标量接收者的 toString()（0 参，复用 to_str，与内建 toString(x) 同一降级）
    - 范围外：toNumber（返回动态 number，codegen 无对应类型）、number/tribool/tuple 方法（abs/isTrue/get 等，对应类型降级未就绪）；subString 的 Double/NaN 参数拒编不错编（解释器 NaN 特判属 Double 域）
    - 验证：新差分用例 s10_string_methods（trim 三形态正反例/全空白/Tab、subString 基本/缺省 end/-1/越界截断/start>=end/中文码点、toString 方法形式四类接收者、链式调用、函数中使用），ctest -C Release 差分 9/9 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen CG1 整数溢出陷阱（t58）
    - 实现：M6 三大剩余方向（class/BigInt/array）调研后，先做成本最低的 CG1 溢出陷阱——i64 加/减/乘/一元负号换 llvm.s{add,sub,mul}.with.overflow intrinsic（checked_int_arith helper，每检查点独立 trap/cont 块），溢出即调 collie_rt_trap_int_overflow 报错退出，把"静默回绕错值"变为"显式运行期报错"（符合绝不静默错编原则）；`%` 的 INT64_MIN % -1 硬件陷阱边缘用 select 安全除数处理（数学结果 0，与解释器 BigInt floor_mod 对齐）；后续顺序：t59 array 最小闭环 → t60 class 最小闭环 → BigInt 运行时化远期
    - 范围外：BigInt 任意精度运行时化（313 行 BigInt 移植纯 C + 整数值改不透明指针，改造面大，远期）；差分用例不含溢出场景（溢出时解释器算对而编译产物报错，输出必然不同，trap 行为单独手工验证）
    - 验证：新差分用例 s11_int_edge（i64 边界内大数加减乘/负号/INT64_MIN%-1/floor 取模回归/复合赋值/函数中运算），ctest -C Release 差分 10/10 逐字节一致；溢出 trap 手工验证通过（编译 INT64_MAX+1 程序，stderr 输出 runtime error: integer overflow、非零退出码、trap 后语句未执行）；Debug 门禁 6/6 不受影响
- [x] codegen array 最小闭环（t59）
    - 实现：语义层对 array 一刀切 KW_ARRAY（元素类型不追踪、索引结果 KW_OBJECT 动态放行），codegen 自行做字面量同质推断（CGValue/CGVar 增设 elem 元素类型字段，Int/Double 混合提升 Double，其余混合/嵌套拒编）；运行时表示为不透明 ptr → collie_rt 数组对象（单块 malloc：头部 len+kind，8 字节槽：Int 直存/Double bitcast/Bool zext/Str ptrtoint），指针拷贝天然对齐解释器引用语义；支持：同质字面量/索引读写（负索引+越界报错退出，对齐 normalize_index）/length 属性 + len() 内建（顺带支持 string）/print+toString（[1, 2, 3] 格式对齐 Value::to_string）/引用语义赋值/三元（elem 一致校验）
    - 范围外：嵌套数组（元素为数组拒编）、array 作函数参数/返回值（KW_ARRAY 无元素类型标注，无法定签名）、数组比较运算、无初始化 array 声明（既有拒编）；另语义层不允许 string + array 直接拼接（比解释器运行期更严，属既有语义面，用例经 toString 转换）
    - 验证：新差分用例 s12_array（四类元素字面量 print/空数组/混合提升/正负索引读写/length+len/引用语义共享写入/循环求和/反向遍历/toString+拼接/三元/函数内局部数组），ctest -C Release 差分 11/11 逐字节一致；越界索引手工验证通过（stderr "Index 5 out of range (size 3)"、非零退出码、后续语句未执行，与解释器核心消息一致）；Debug 门禁 6/6 不受影响
- [x] codegen class 最小闭环（t60）
    - 实现：单类无继承——每类一个 LLVM StructType（collie.class.<类名>，字段按声明顺序布局，下标即 GEP 索引），`new` 降级 collie_rt_obj_new（malloc+memset 零初始化，size=8×字段数上界与 DataLayout 解耦）+ 字段初始值写入 + 构造器调用（三段顺序对齐解释器 visitNew），实例值为不透明 ptr（指针拷贝即引用语义）；方法/构造器降级 `collie.类名.方法名` InternalLinkage 独立函数、this 作隐藏首参 ptr（直持 SSA 值不落栈槽）；支持字段读写（含 this.x）、方法调用（含 this.m() 互调、toString 无参兜底）、print/toString 实例固定 "<object>"、三元（类名一致校验）；CGType 新增 Obj，CGValue/CGVar 增设 cls 字段，visitVarDecl 前置 IDENTIFIER 类名分支；解释器四处 coerce 以静态检查 + Int→Double 提升等价对齐
    - 范围外（拒编不错编）：extends/base/@override（继承二期）、字段无初值（解释器为 none，codegen 无 none 表示，零值初始化会静默错编——对齐 visitVarDecl 先例）、number/tribool/tuple/array 类型字段（对应 codegen 降级未就绪）、实例相等比较、实例进数组/元组、实例作普通函数参数/返回值（二期）、object 声明类型动态放行路径、方法重载
    - 验证：新差分用例 s13_class（字段初值/构造器赋字段/方法读写字段/this 互调/多实例独立/引用语义共享/print 实例 + toString 兜底/四类字段初值无构造器类/三元/循环方法调用），ctest -C Release 差分 12/12 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen class 二期：继承/base/实例作函数参数返回值（t61）
    - 实现：①继承布局——子类字段 = 父链 base-first 合并 + 自身追加（GEP 索引前缀不变），同名字段遮蔽拒编；②方法按分派类单态化——对每个类 C 沿链每个 (定义类 D, 方法 m) 生成 `collie.C.D.m`（分派上下文 C、base 解析上下文 D），与解释器 call_class_method(instance, method, defining_class) 同构，模板方法模式（父类方法内 this.m() 命中子类覆写）与解释器动态分派等价（向上转型拒编保证静态 cls 即动态类）；CGClass 改造为 super/dispatch/instances，CGMethod 携 defining + AST 指针，register_class 拆分为 register_class_layout + register_class_methods，generate 第一遍改三阶段（全部类布局→全部类方法原型→函数原型）；③base(...) 构造器委托/base.method() 按定义类（current_defining_class_）的父类静态解析，调当前分派类下单态化实例，父类无构造器时 0 实参空操作；④实例作函数参数/返回值——declared_signature_type 把签名处 IDENTIFIER 类名→Obj+cls，coerce_call_arg/visitReturn 处 cls 严格相等；语义层同步支持类名签名→object 动态放行（visitFunction 参数/返回类型映射，与 visitVarDecl 同规则），并修复 must-return 检查在 end_scope 前抛出导致的作用域泄漏级联误报（既有 bug）；⑤@override 纯语义层校验，codegen 忽略
    - 范围外（拒编维持）：向上转型（Base b = new Derived()）、子类同名字段遮蔽、object 声明类型、方法/构造器重载、实例比较/进容器、父类声明晚于子类（布局合并需父类先注册）
    - 验证：新差分用例 s14_inherit（继承字段/方法、覆写、: base(...) 构造器委托、base.method 绕过覆写、模板方法动态分派、三级继承、实例作函数参数返回值、无构造器父类 base() 空操作），先解释器跑通再进差分门禁；ctest -C Release 差分 13/13 逐字节一致；Debug 门禁 6/6（含语义层改动回归）
- [x] codegen number 双表示（t62，CG5 收窄）
    - 实现：number 值为 tagged 双表示（tag：0=整数 i64 直存/1=小数 double bitcast i64 位模式），codegen 内以 LLVM first-class struct `{i64 tag, i64 bits}` 单 SSA 值流转（CGType 新增 Num；变量单 alloca 槽、函数签名/三元 PHI 直用该 struct，LLVM 自动处理 ABI 降级），仅 collie_rt 边界 extractvalue 拆散标量传参 + out 指针写回（规避 MSVC x64 16 字节 struct 传参隐藏指针 ABI 错配——比登记时"两个 SSA 值成对流转"方案更简洁，verifyModule 全链路验证通过）；算术（+ - * / % 一元负号）/比较/toString/print 全部下沉 collie_rt 四接口（collie_rt_num_arith/num_cmp/num_to_str/print_num）单点对齐解释器语义：双整数精确 + - * 与 floor 取模（i64 加/减/乘溢出复用 CG1 陷阱报错）、/ 恒 double、混合运算走 double、除零 IEEE 754、打印格式对齐 Value::to_string；Int/Double→Num 加宽保持原表示打 tag（coerce_for_slot/coerce_call_arg/visitReturn 三处一致，对齐 coerce_to_declared 的 KW_NUMBER 分支）；三元任一分支 Num 则两分支统一 Num；解锁 number 变量声明/赋值/算术/比较/print/toString/三元/函数参数返回值
    - 范围外：任意精度自动扩容（BigInt 运行时化留远期——需 extern "C" 包装 C++ BigInt 进 collie_rt 且全部整数运算变运行时调用，收益仅超大数场景）、number→integer/decimal 窄化（解释器 integer 拒 decimal 值为运行期错误，codegen 静态无法判定 tag → number 赋给 integer/decimal 变量拒编）、number 数组元素/类字段（维持 S12/S13 拒编）、toNumber 返 number（维持拒编）
    - 验证：新差分用例 s15_number（整数/小数两态与重赋值、integer/decimal 加宽、混合算术、/ 恒小数、floor 取模含负数四象限与小数、除零 ±Infinity/NaN、一元负号、比较相等含 5==5.0 与 NaN 语义、print/toString 格式、三元混型统一、number 签名函数与 return 加宽），先解释器跑通再进差分门禁；ctest -C Release 差分 14/14 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen toNumber 内建（t63，收口 t62 范围外遗留）
    - 实现：string 解析下沉 collie_rt 新接口 collie_rt_str_to_num（复刻解释器 to_number_value 的 string 分支——剥两端空白 → 严格大小写 "Infinity"/"+Infinity"/"-Infinity" → 纯整数串（可带单个 +/- 前缀）精确整数表示 → strtod 等价 std::stod（须整串消费且结果有限，"1.5f" 尾部残留/"infinity" 宽松拼写/ERANGE 下溢均失败）→ 一切失败返 NaN 不报错，结果经出参写回同 num_arith 的 ABI 规避）；bool → 0/1 整数表示、integer/decimal/number 纯 IR 内联转 Num（复用 to_num，visitCall 内建分发插在 len 之后/用户函数查表之前）；方法形式 x.toNumber() 与内建 toNumber(x) 共用 to_number_num 降级；超 i64 纯整数串 strtoll ERANGE 走 CG1 陷阱报错退出（解释器 BigInt 精确，i64 承载不了则拒绝静默错编）
    - 范围外：none/array/tuple/实例参数拒编维持（解释器此处为运行期报错）
    - 验证：新差分用例 s16_tonumber（整数/小数/带符号/前后空白含 Tab/严格 Infinity 三形式/宽松拼写与尾部残留与空串失败 NaN/科学计数法/前导点小数/bool 0、1/数值透传加宽/方法形式含字面量接收者/NaN 运算与比较语义/与算术组合/函数内使用），先解释器跑通再进差分门禁；ctest -C Release 差分 15/15 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen `==?` 多路匹配（t64，MultiMatchExpr）
    - 实现：级联比较块链降级（gen_multi_match）——目标求值一次后，按分支序/候选序生成「比较→命中跳分支结果块/未中顺延下一候选」块链，链末端即默认块，天然对齐解释器首命中 + 惰性求值语义；相等比较复用 == 四路降级出 i1（gen_match_eq：Str×Str 走 collie_rt_strcmp==0、任一 Num 走 collie_rt_num_cmp op 0、Bool×Bool icmp、Int/Double icmp/fcmp 含混型提升 5==5.0），零新增 collie_rt 接口；结果分支类型统一沿用 gen_ternary 规则扩展到 N+1 支（同型直用含 Arr elem/Obj cls 一致性校验、数值混型任一 Num 统一 Num 否则 Double），merge 块 N+1 入口 PHI 收拢
    - 范围外（拒编）：无默认分支形式（语义层保证仅 tribool 穷尽三态可无默认，tribool 不在 codegen 范围，故 codegen 一律要求默认分支）；tribool/unset 目标或候选；object 动态比较；数组/元组深比较候选
    - 验证：新差分用例 s17_multimatch（number/string/integer/decimal/bool 目标、候选归组、默认分支、混合表示 double 视图 5==5.0、首命中优先、结果混型统一 Double/Num、目标/候选为表达式、嵌套于三元双向、函数 return 路径），先解释器跑通再进差分门禁；ctest -C Release 差分 16/16 逐字节一致；Debug 门禁 6/6 不受影响
- [x] codegen tribool 三态布尔（t65）
    - 实现：CGType 新增 Tri → LLVM `i8`，沿用解释器三态编码 False=0 < Unset=1 < True=2；`to_tri` 统一加宽（Tri 透传 / Bool select 0|2，覆盖赋值 coerce_for_slot/传参 coerce_call_arg/返回值 visitReturn/declared_cgtype 与签名类型 KW_TRIBOOL）；unset 字面量 → i8 1；gen_logical 重构统一 i8 域——短路条件「AND 左==0 / OR 左==2」CondBr，右支 `umin`/`umax` intrinsic 合并，merge PHI i8，纯 bool 输入 `icmp eq 2` 收窄回 i1（短路边静态 widen 与解释器输出等价，差分实证）；visitUnary `!` → `2 - t`；`==`/`!=` 与 gen_match_eq 三态判等（双方限 Tri/Bool，icmp i8）；gen_ternary 重写为 Arm 向量式——两分支 Tri 条件 `==2` 判真（unset 走 false 分支）、三分支三路 CondBr（entry `==2` → then/rest，rest `==0` → else/unset），分支混 Tri/Bool 统一 Tri；isTrue/isFalse/isUnset 方法 icmp 出 i1；print/toString 双 select 三常量串；`==?` tribool 目标穷尽省默认形式——默认检查移至 target 求值后，无默认链尾 `unreachable`（i8 值域严格 {0,1,2} + 语义层保证穷尽）；零新增 collie_rt 接口
    - 范围外（拒编维持）：tribool 进数组元素/元组；object 动态路径三态；`if/while/for/do-while` 条件 tribool（语义层已拦，codegen 无需处理）
    - 验证：新差分用例 s18_tribool（unset 字面量/加宽/Kleene 真值表含短路副作用验证/三态判等/isTrue 系/两分支 unset 走 false/三分支三元/==? 穷尽省默认与默认分支形式/toString 与 print/函数参数返回值），先解释器跑通再进差分门禁；ctest -C Release 差分 17/17 逐字节一致（s18_tribool 首跑即过）；Debug 门禁 6/6 不受影响
- [x] codegen switch 语句（t66）
    - 实现：visitSwitch 级联比较块链降级（与 gen_multi_match 同构的语句版，无结果 PHI）——条件求值一次，按 case 序/候选序生成「比较→命中跳 case body/未中顺延」，命中执行 body 后跳 switch.end（无 fallthrough，对齐解释器）；default 位置无关最后兜底（非 default 分支优先比较，链尾跳 default body，无 default 则跳 end）；候选相等比较复用 gen_match_eq（Int/Double/Bool/Str/Num/Tri 含混型提升，零新增 collie_rt 接口）；case body 为 BlockStmt 自带作用域；body 内 break/continue 维持绑定外层循环（解释器 switch 不捕获 BreakSignal，loop 栈不动），body 含 return/break 等终结器时不补 br
    - 范围外（拒编维持）：object 动态比较目标/候选；数组/元组深比较候选（同 ==? 拒编面，gen_match_eq 内自然拒编）
    - 验证：新差分用例 s19_switch（number/string/integer/bool/tribool 目标、多值 case、default 位置无关与缺省、命中后不穿透、case body 作用域、候选为表达式惰性求值、循环内 switch 含 break 绑定外层循环、函数内 switch 含 return），先解释器跑通再进差分门禁；ctest -C Release 差分 18/18 逐字节一致（s19_switch 首跑即过）；Debug 门禁 6/6 不受影响
- [x] codegen number 专属方法（t67）
    - 实现：gen_number_method 三路降级 10 个方法（abs/integerPart/decimalPart → 接收者同型数值，isInteger/isDecimal/isNaN/isInfinity/isFinite/isPositive/isNegative → bool）——Int 纯 IR（abs = checked ssub(0,n) + select，INT64_MIN 取负走溢出陷阱对齐 CG1——解释器 BigInt 可精确表示、codegen i64 不可，拒错编从陷阱；integerPart 恒等/decimalPart 恒 0/isInteger、isFinite 恒真等常量折叠）；Double 走 llvm.fabs/llvm.trunc/llvm.floor intrinsic + fcmp（isNaN 用 uno 自反比较，isFinite/isInfinity 用 |a| 与 +inf 有序比较 NaN 天然 false，isInteger/isDecimal = finite AND floor 判等，integerPart 向零取整、decimalPart 保留符号对齐解释器）；Num 接收者 tag 分支两路 + PHI 合并（整数态保持整数态）；零新增 collie_rt 接口（复用 collie_rt_trap_int_overflow）
    - 验证：新差分用例 s20_number_methods（integer/decimal/number 三类接收者 × 十方法、负数/零/±Infinity/NaN 边界、链式 d.abs().integerPart()、方法结果参与算术比较、函数传参），先解释器跑通再进差分门禁；ctest -C Release 差分 19/19 逐字节一致（s20 首跑即过）；Debug 门禁 6/6 不受影响
- [x] codegen tuple 静态展开（t68）
    - 实现：语义层对 tuple 元素类型零追踪，但 tuple 不可变且字面量处元素类型/名字表编译期完全可知——采 codegen 侧纯静态展开：CGType::Tup 虚值（value 恒 nullptr，元素 CGValue 向量 + 平行名字表登记 tuple_values_ 注册表，无运行时对象）；变量声明解构为逐元素独立 alloca 槽（tuple_vars_，槽名 var.0/var.1，嵌套递归子条目，形状取自初始值），变量读逐槽 load 重组、重赋值同形状（元素数+名字表一致）逐槽写否则拒编；t[常量 i] 编译期解析（const_int_of AST 层模式匹配含一元负号包字面量，负索引归一化，越界拒编）；t.length 常量折叠（优先于同名字段，对齐解释器分支顺序）；t.name 线性扫名字表（不排除空名）、t.get("字面量键")（排除空名）双双对齐解释器；print/toString/插值经 tuple_to_str 静态展开成 "(1, 2)" / "(name: v)" 拼接（常量段编译期合并 + rt_concat 链，嵌套递归）；零新增 collie_rt 接口
    - 范围外（拒编维持）：动态索引/动态键 get/tuple 进函数签名（declared_signature_type 显式拒编，形状跨边界不可知）/tuple 进数组/tuple 相等比较/三元与 ==? 分支产 tuple（虚值防进 PHI 显式守卫）/类字段 tuple（llvm_type_of Tup 拒编）
    - 验证：新差分用例 s21_tuple（无名/命名/混合字面量、常量正负索引、命名字段、get 常量键、length 优先于同名字段、print 空元组/嵌套/混型元素、同形状重赋值、toString/插值/经 toString 拼接、元素参与算术），先解释器跑通再进差分门禁；ctest -C Release 差分 20/20 逐字节一致（s21 首跑即过）；Debug 门禁 6/6 不受影响
- [x] codegen char/byte/word + 位运算（t69）
    - 实现：char/character 承载 = CGType::Str（visitLiteral LITERAL_CHAR/LITERAL_CHARACTER→GlobalString + declared_cgtype KW_CHAR/KW_CHARACTER→Str，打印/比较/拼接零新触点复用既有 Str 路径，对齐解释器 char 即 string 语义）；byte/word 承载 = i64 零类型扩散（CGVar 新增 bit_max 字段 255/65535/0）——visitVarDecl 前置分支特判 KW_BYTE/KW_WORD（初始值须 Int，Num/Double 拒编）+ visitAssign 按 bit_max 在赋值点插范围检查（check_bit_range：(u64)v > max 一次覆盖负数与超上限，越界陷阱），对齐解释器 coerce_to_declared 只在赋值点校验、表达式域无截断、位运算结果加宽 integer；declared_cgtype 不映射 KW_BYTE/KW_WORD（类字段/函数签名维持拒编不静默丢范围校验）；位运算双侧限 Int（Num tag 静态不可判/Double 拒编不错编）：& | ^ → and/or/xor，<< >> → 移位量 (u64)count > 63 检查后 shl/ashr，~ → CreateNot（xor -1，i64 域 = 解释器 BigInt -x-1）；collie_rt 新增陷阱 2 个 collie_rt_trap_bit_range(name,max,got)/collie_rt_trap_shift_count()（t58 风格 stderr+exit(1)，文案对齐解释器措辞）
    - 验证：新差分用例 s22_bits（hex 字面量、& | ^ ~ << >> 与优先级、~0xFF=-256、-8>>1=-4、byte/word 声明打印 "15 65535"、byte 参与算术加宽与重赋值、变量间位运算移位、char/character 声明打印比较拼接含 UTF-8），先解释器跑通再进差分门禁；byte 越界（256）/word 负值（~0xFF）/移位越界（变量移位量 64）三陷阱手动实证（stderr 报错文案对齐解释器 + exit 1）；ctest -C Release 差分 21/21 逐字节一致（s22 首跑即过）；Debug 门禁 6/6 不受影响
- [x] codegen 数组进函数签名（t70）
    - 实现：array 形参/返回值放行（顶层函数 + 类方法），签名处元素类型不可知——elem 动态化为 Num 哨兵（关键洞察：collie_rt 数组 kind 编码 0=int/1=double 与 t62 Num tag 完全重合，被调方索引读 = rt_arr_get bits + 新接口 rt_arr_kind 直接拼 Num 零转换）；不变量：进动态域的数组 elem 限 {Int, Double, Num}（bool/str 数组作实参/返回值静态拒编不错编），保证动态域运行期 kind ∈ {0,1}；触点：declare_function/register_class_methods 4 处 Arr 拒编放行、形参落槽/调用返回点 elem=Num 共 5 处、visitIndex/visitIndexAssign 动态路径（写下沉新接口 collie_rt_arr_set_num：tag==kind 直存 / int→double 提升 / decimal 写 int 数组陷阱，新缺口 CG7）、visitAssign 规则扩展（Num 槽 ← 数值系来源；静态槽 ← Num 来源拒编）；length/len/print/toString 运行时 kind 驱动零改动；范围外：数组类字段（coerce_for_slot 标量向牵扯三处重构，留后续）、嵌套/异质数组
    - 验证：新差分用例 s23_array_signature（int/double 数组同函数运行时 kind 分流、返回值接收打印/负索引/长度、动态数组再作实参、跨边界引用语义 fill、冒泡排序读写比较交换全链路、类方法数组参数/返回值、动态元素参与算术比较插值），先解释器跑通再进差分门禁；CG7 陷阱（decimal 写 int 数组经动态域，解释器可容异质产物陷阱退出）+ bool 数组作实参拒编 + 动态数组赋静态槽拒编三实证通过；ctest -C Release 差分 22/22 逐字节一致（s23 首跑即过）；Debug 门禁 6/6 不受影响
- [x] codegen 数组作类字段（t71）
    - 实现：放行 array 类字段（register_class_layout 删拒编；字段槽本就 opaque ptr，struct 建型/malloc 上界零改动）；CGField 无 elem 伴随信息，字段读出即动态域（visitProperty/visitPropertyAssign 置 elem=Num 哨兵，t70 机制全套复用，零新 rt 接口）；字段写入守卫下沉 coerce_for_slot 相等分支（右值 elem 限 {Int,Double,Num}，bool/str 数组拒编，一处覆盖 visitPropertyAssign + visitNew 字段初始化两入口；变量/tuple 槽的 Arr 另有前置分支，下沉零回归）维持动态域 kind ∈ {0,1} 不变量；调研修正：Obj 作字段实也未支持（CGField 无 cls 伴随，declared_cgtype 不识 IDENTIFIER），数组字段无先例可拄但 Num 哨兵方案零伴随信息恰好绕开缺口
    - 验证：新差分用例 s24_array_field（字段初始值 int/double 数组、方法内 this 读写求和原地加、外部索引写直存/提升、字段读出共享底层存储、方法形参/直接赋值换字段数组、字段数组再跨签名边界、构造器接数组存字段、跨构造器引用语义、继承父类数组字段、元素参与算术比较插值），先解释器跑通再进差分门禁；bool 数组赋字段 + string 数组作字段初始值两拒编实证通过（解释器均可容，产物拒编不错编）；ctest -C Release 差分 23/23 逐字节一致（s24 首跑即过）；Debug 门禁 6/6 不受影响
    - 范围外：Num 字段（16 字节 tagged 装不进 8 字节槽，t62 拍板不变）、Obj 字段（另立任务）、嵌套/异质数组
- [x] codegen 类实例作类字段（t72）
    - 实现：CGField 加 cls 伴随 + register_class_layout 加 IDENTIFIER 前置分支（照抄 visitVarDecl 形态：类须已注册——声明在前；前向引用实为语义层更早拦截 "Invalid type"，classes_ 查询是防御性双保险）；coerce_for_slot 加 slot_cls 参数（默认空串）相等分支 Obj 严格同类校验（t61 拍板：静态 cls 即动态类是单态化分派前提；其余调用点 Obj 均有前置分支零回归）；visitProperty/visitPropertyAssign 字段读写带 cls，下游属性链/方法调用/传参/返回全走 t61 既有 Obj 路径；调研：Obj cls 守卫在变量赋值/声明/返回/传参/三元/==?/tuple 槽全部已有唯独字段路径缺；parser 字段接受 IDENTIFIER 类型（构造器判定靠 peek_next==LPAREN 不误伤），解释器 coerce_to_declared default 动态放行
    - 验证：新差分用例 s25_object_field（字段初始值 new/属性链读写/字段实例上调方法/引用语义共享/整体替换（方法形参+直接赋值）/跨签名边界（函数接实例返回其字段）/构造器接实例存字段/深层属性链写 g.car.engine.power/继承父类实例字段/插值比较），先解释器跑通（11 行）再进差分门禁；向上转型字段赋值 + 字段初始值向上转型两拒编实证通过（"storing instance of class 'Sub' where 'Base' is declared"，解释器均可容输出 1）；ctest -C Release 差分 24/24 逐字节一致（s25 首跑即过）；Debug 门禁 6/6 不受影响
    - 范围外：向上转型字段（同 t61）、相互/自引用类字段（声明序不可达，语义层已拦）、Num/Tup 字段（既有拒编不变）
- [x] codegen 顶层变量提升 LLVM 全局变量（t73）
    - 范围拍板：CGVar.slot 改型 Value*（全部使用点仅 CreateLoad/CreateStore，纯声明面）；顶层判定 !in_function_ && scopes_.size()==1 的 VarDecl 建 GlobalVariable（InternalLinkage + 零初始化，名加 collie.g. 前缀防符号冲突），初始值仍在 main 当前位置按源序 store；块内声明维持 alloca；visitFunction/gen_method_body 重建作用域栈时以顶层层拷贝为链底（剔除 Tup 条目——tuple 槽组是 main 的 alloca，跨函数引用非法，剔除后走既有 identifier 拒编），lookup_var 零改动；顺序安全论证：语义层在函数声明处分析函数体（只见此前声明的顶层变量）+ 解释器前向调用运行期报错（调用必在函数声明语句之后）⇒ 变量 store 必先于任何函数内读，零初始化值不可能被观察到
    - 实现：create_var_slot 统一建槽入口（顶层建 GlobalVariable，否则 create_entry_alloca），visitVarDecl 三处建槽（Obj/byte-word/普通标量）改走该入口；tuple 分支不动（create_tuple_var 内仍 alloca，符合范围外拍板）
    - 验证：新差分用例 s26_globals（函数读多类型全局/函数写全局可见性/decimal 读写运算/全局数组元素写引用语义/形参与局部声明遮蔽/全局实例跨函数字段写 + 方法体读写全局/顶层 for 块内声明不全局化/byte 全局函数内重赋值范围检查/插值比较算术），先解释器跑通（11 行）再进差分门禁；拒编实证：函数内引用顶层 tuple 报 "identifier 't'"（解释器可容输出 10）；ctest -C Release 差分 25/25 逐字节一致（s26 首跑即过）；Debug 门禁 6/6 不受影响；陷阱：cmake --build 默认目标不含 colliec（EXCLUDE_FROM_ALL），须走 t50_build.cmd 或 MSBuild colliec.vcxproj，否则改动未编译门禁"假绿"
    - 范围外：顶层 tuple 跨函数访问（拒编）、函数内整体替换全局数组换 elem（既有静态守卫维持）、const 守卫（语义层既有职责）
- [x] codegen number 作类字段（t74）
    - 范围拍板：复评并推翻 t62「字段块 8 字节槽装不下 16 字节 tagged 表示」的拒编理由——类布局并非固定 8 字节槽，StructType 由各字段 llvm_type_of 结果直接拼装，Num 放行则建型/字段 GEP 读写零改动；改动仅两处：①删 register_class_layout 的 Num 字段拒编守卫；②visitNew malloc 上界 `8 * fields.size()` 改按字段类型累计（Num 记 16、其余记 8，维持编译期常量上界风格）；转换零新增：coerce_for_slot 已有 Num 槽加宽分支（Int/Double→to_num）+ 相等直通，覆盖字段初始值/属性赋值两入口，与解释器 coerce_to_declared KW_NUMBER 原样通过语义对齐；16 字节按值流转先例充分（变量/全局槽、tuple 解构槽、函数签名传参返回）
    - 实现：register_class_layout 守卫删除（放行注释登记）+ visitNew size 累计循环（空字段类保底 8）；collie_rt.c rt_obj_new 注释同步；调研修正：tribool 字段实测早已随 S18 tribool 支持自然放行（此前文档「tribool 字段拒编」记录有偏差，README 已更正），实际拒编面仅 Num（本次解锁）/Tup
    - 验证：新差分用例 s27_number_field（字段两态初始值/方法内 this 读写混合算术/外部写三态加宽直通/字段参与算术除法取模比较/abs・isInteger・integerPart・toString 字段上调用/跨签名传参返回写回/构造器接 number 形参存字段/Num 夹 8 字节字段混合布局 GEP 偏移/继承父类 Num 字段/插值三元），先解释器跑通（17 行）再进差分门禁；拒编实证两项：Tuple 字段报 "tuple value in this position"、number 字段读出赋 decimal 槽报 "implicit conversion"（解释器均可容）；ctest -C Release 差分 26/26 逐字节一致（s27 首跑即过）；Debug 门禁 6/6 不受影响
    - 范围外：Tup 字段（既有拒编不变）、number 字段读出赋窄化静态槽（既有拒编）、字段上直接一元负号/相等比较（语义层对属性访问的既有限制，非 codegen 范围）、数组元素 Num 表示（t70 拆 kind+payload 路线不变）
- [x] codegen tuple 相等比较（t75）
    - 选型：候选盘点（实例相等——解释器 values_equal 无 Instance 分支恒 false、tuple 相等——解释器深比较、顶层 tuple 全局化——横跨 t68/t73 两机制回归面大）中 tuple 相等差分价值最高且预检确认语义层放行（is_comparable_type 可比）、解释器深比较正确（长度/名字表/嵌套/5==5.0 混型全通）、codegen 现拒编 "non-numeric operand of '=='"
    - 范围拍板：visitBinary ==/!= 加 Tup×Tup 前置分支（先于 require_numeric），纯编译期静态展开递归：长度或名字表不一致 → 常量 i1（对齐解释器先比 size 再比 names）；逐元素按既有标量降级复用（Str→rt_strcmp、Tri/Bool→三态 icmp、Num/混型→rt_num_cmp/fcmp、Int→icmp），And 链合并，嵌套 tuple 递归；元素含 Arr/Obj 或与异型标量配对时贡献恒 false（对齐解释器 kind 不等/无 Instance 分支恒 false，Arr 深比较例外——含 Arr 元素拒编不错编，避免错值）；Tup × 非 Tup 恒 false（kind 不等）；!= 整体取反
    - 实现：gen_tuple_eq 递归辅助函数（tuple_values_ 按值拷贝不留引用，防 register_tuple 扩容失效）+ visitBinary 前置分支（!= 对结果 CreateNot）；零新增 collie_rt 接口；实测修正：Tup × 非 Tup 整体比较实为语义层更早拦截（"Incomparable operand types"），codegen 恒 false 分支是防御性双保险
    - 验证：新差分用例 s28_tuple_eq（无名/命名相等与不等、长度不一致、名字表不一致、嵌套递归 + 5==5.0 混型、异型标量配对恒 false、bool/tribool 三态元素、字面量直比、空元组、if 条件/三元中使用、number 元素 rt_num_cmp、重赋值后再比较），先解释器跑通（15 行）再进差分门禁；实证两项：含数组元素 tuple 相等拒编 "tuple equality with array element"（解释器深比较可容输出 true）、Obj 元素两端恒 false 一致（同一实例也 false）；ctest -C Release 差分 27/27 逐字节一致（s28 首跑即过）；Debug 门禁 6/6 不受影响
    - 范围外：==? / switch 的 tuple 候选（gen_match_eq 另一路径，可留后续）、关系比较 < <= > >=（解释器也不支持 tuple 关系比较）、数组深比较（Arr×Arr 独立任务）
- [x] codegen 顶层 tuple 全局化（t76）
    - 选型：t73 顶层变量全局化时 tuple 解构槽组维持 @main alloca（跨函数引用拒编），t75 后 tuple 机制唯一大缺口；预检确认解释器全支持（函数内读元素/整体重赋值跨函数可见）、colliec 现拒编 "identifier 't'"
    - 范围拍板：①create_tuple_var 建槽从 create_entry_alloca 换 create_var_slot（顶层判定单点复用 t73 入口——顶层建零初始化 GlobalVariable 名 collie.g.t.0 式、初始值仍当前位置 store，块内/函数内维持 alloca；嵌套子槽组递归同条件天然覆盖）；②visitFunction/gen_method_body 链底拷贝取消 Tup 条目剔除（顶层层 Tup 槽组改后必为全局槽，函数内引用合法；tuple_vars_ 注册表本为成员跨函数可用，lookup_var/load_tuple_var/store_tuple_var 零改动）
    - 实现：三处——create_tuple_var 换 create_var_slot、visitFunction/gen_method_body 剔除循环换整层拷贝 `scopes_.back() = saved_scopes.front();`
    - 验证：新差分用例 s29_tuple_global（函数内读/索引/length/运算、命名 .name/.get/插值、整体重赋值跨函数可见、嵌套读写、全局 tuple 相等（结合 t75）、方法体读写、局部遮蔽、bool/number 元素）；两实证——函数内换形状重赋值拒编 "assigning tuple of different shape"、tuple 形参既有拒编 "tuple in function signature"；ctest -C Release 差分 28/28 逐字节一致（s29 首跑即过），Debug 门禁 6/6；期间发现既有缺口 CG8（print 逐参求值边打边走，实参含副作用输出时次序与解释器不同，s29 经局部变量中转回避，已登记 codegen/README 缺口表）
    - 范围外：tuple 进函数签名（形状跨边界不可知，既有拒编不变）、顶层块内声明 tuple（非顶层层，维持 alloca 不入链底，与 t73 标量一致）
- [x] codegen 修复 CG8：print 先求值全部实参再统一输出（t77）
    - 选型：CG8 是活错编面——print 实参含副作用输出（函数/方法体内 print）时产物输出次序与解释器不同且静默（t76 发现），违背"拒编不错编"，优先于 ==?/switch tuple 候选等解锁类任务；预检实证：解释器 `side`↵`a 1 b`（先求值后打印），产物 `a side`↵`1 b`（逐参交错）
    - 范围拍板：gen_print 两阶段化——第一循环 emit 全部实参按值收集 vector<CGValue>（副作用按源序发生），第二循环打印 sep+值+换行；打印阶段 to_str/tuple_to_str/arr_to_str 调用无输出副作用安全；Tup 注册表只追加不重排，按值收集下标有效；零新增 collie_rt 接口
    - 实现：gen_print 单函数改动（两循环拆分），改后预检产物输出与解释器逐行一致
    - 验证：新差分用例 s30_print_order（单副作用实参居中、多副作用实参源序求值、方法体内 print 副作用（s29 曾回避形态直接使用）、副作用与 string/decimal/bool/tuple/数组混排、副作用返回值参与运算、无副作用多参回归保护）；s29 历史注更新（CG8 已修复指向 s30）；README 缺口表 CG8 划线标记已消除；ctest -C Release 差分 29/29 逐字节一致（s30 首跑即过），Debug 门禁 6/6
    - 范围外：CG2（none 等复合值打印格式）不动；toString/插值拼接链单实参无交错问题不涉及
- [x] codegen ==?/switch 的 tuple 候选（t78）
    - 选型：t75 范围外遗留（gen_match_eq 另一路径），tuple 相等机制就绪后即轻量单点；预检实证：解释器全支持（==? tuple 目标/候选命中与不命中、switch tuple 目标、命名/嵌套 tuple 候选），colliec 拒编 "'==?' comparison of these value types"
    - 范围拍板：gen_match_eq 在末尾 unsupported 前加 Tup 分支——双 Tup 走 gen_tuple_eq（t75 静态展开深比较单点复用），Tup×非 Tup 恒 false（对齐解释器 values_equal kind 不等，防御性双保险）；含 Arr 元素 tuple 由 gen_tuple_eq 递归内既有拒编天然覆盖；==?（visitMultiMatch）与 switch 两调用点同时解锁，零新增接口
    - 实现：gen_match_eq 单分支改动；语法事实补充：单元素 tuple 字面量 `(42,)` 解析不支持（parser 层，两端一致）
    - 验证：新差分用例 s31_match_tuple（==? 命中/次候选/默认、命名/名字表不一致、嵌套+5.0 混型、长度不一致、bool/tribool 元素、空元组、首命中优先、switch 命中/归组/default/无 default 静默跳过、全局 tuple 目标函数内 ==?（结合 t76）、变量候选）；两实证——含数组元素 tuple 候选拒编 "tuple equality with array element"（解释器可容输出 hit）、Tup×非 Tup 候选两端语义层一致拦截 "Incomparable candidate value type in '==?'"；ctest -C Release 差分 30/30 逐字节一致（s31 首跑即过），Debug 门禁 6/6
    - 范围外：数组目标/候选（Arr×Arr 深比较独立任务）维持拒编；tuple 关系比较不涉及
- [x] codegen 数组相等比较（t79）
    - 选型：t75/t78 两次列为范围外的独立任务——数组深比较（Arr×Arr ==/!=、tuple 含 Arr 元素、==?/switch 数组候选）；预检实证：解释器 values_equal Array 分支先比 size 再逐元素递归深比较（`[1,2,3] == [1.0,2.0,3.0]` 为 true 的 double 视图、空数组相等、tuple 含数组元素深比较、==?/switch 数组候选全支持），colliec 拒编 "non-numeric operand of '=='"；Arr×非Arr 整体 == 语义层两端一致拦截 "Incomparable operand types"（codegen 无需处理该配对）
    - 范围拍板：新增 rt 接口 `collie_rt_arr_eq(l, r) -> i64` C 层深比较（先比 len；逐元素按两侧运行时 kind：同 kind 0/2/3 分别 i64 直比/直比/strcmp、kind 1 double 值比较、{0,1} 混合 double 视图对齐解释器混合表示、bool/str 与数值系配对恒 false；len==0 天然 true；运行时 kind 判定天然覆盖 t70 动态域数组）；codegen 三触点同时解锁并单点复用：visitBinary ==/!=（Arr×Arr 前置分支）、gen_tuple_eq Arr 元素（t75 遗留拒编）、gen_match_eq Arr 候选（t78 遗留拒编）
    - 实现：collie_rt.c 新增 arr_eq + codegen 三分支（visitBinary Arr×Arr、gen_tuple_eq 双 Arr 下沉/Arr×非Arr 元素恒 false、gen_match_eq 双 Arr 下沉/Arr×非Arr 恒 false 双保险）
    - 验证：新差分用例 s32_array_eq（==/!= 命中/不命中/长度不等、int×double 数组 double 视图、string/bool 数组、bool×int 数组 kind 不等恒 false、空数组、tuple 含数组元素三态、==? 命中/字面量候选/默认、switch 命中/归组/default、跨签名动态域数组、全局数组函数内比较、比较结果进逻辑运算）；实证——数组关系比较 a < b 语义层两端一致拦截 "Invalid operands for comparison"；ctest -C Release 差分 31/31 逐字节一致（s32 首跑即过），Debug 门禁 6/6
    - 范围外：数组关系比较 < <= > >=（语义层拦截）；嵌套数组元素（字面量层已拒编不存在）；Arr×非Arr 整体比较（语义层拦截）
- [x] codegen 小数取模（t80）
    - 选型：拒编面盘点（130+ 处 unsupported 归类）后选定唯一"解释器支持、机制就绪、单触点"的活跃解锁面——decimal 参与的 `%`（Double×Double / Int×Double / Double×Int）；预检实证：解释器 10 行全支持（floor 符号随除数 -7.5%2.0=0.5、7.5%-2.0=-0.5、除零 7.5%0.0=NaN、混合 7%2.5=2），colliec 拒编 "'%' on non-integer operands"；其余候选均排除——bool/str 数组跨签名需新动态值表示（规模大）、实例相等解释器 values_equal default 恒 false（语义存疑）、函数重载解释器实为后定义覆盖（非解锁面）
    - 范围拍板：visitBinary OP_MODULO 单触点——Int×Int 与 Num 路径不动，Double 参与时 FRem（语义即 fmod）+ floor 修正（r 非零且与 b 异号时 r += b，select 无分支，仿 Int 路径既有写法）；除零 FRem 天然 NaN 且 NaN 下 FCmp ONE 为 false 不触发修正（对齐解释器 b==0.0 提前返 NaN）；零新增 rt 接口
    - 实现：visitBinary OP_MODULO 加 Double 分支（Num 路径之后、Int×Int 之前）单处改动；-0.0 == 0.0 使 nonzero 为 false 同解释器 r != 0.0 判定
    - 验证：新差分用例 s33_decimal_mod（四象限符号、混合 Int×Double/Double×Int、精确整除 -0.0 归一格式化、除零 NaN、Infinity 边界（-7.5%Infinity 修正为 +Infinity）、NaN 传播、%= 复合赋值、表达式嵌套与比较参与、跨函数签名含浮点残差 8.88178e-16 两端一致、循环累积、数组 double 元素参与、number 路径回归保护）；实证——true % 2.0 语义层两端一致拦截 "Numeric operands expected for arithmetic operation"；ctest -C Release 差分 32/32 逐字节一致（s33 首跑即过），Debug 门禁 6/6
    - 范围外：number 参与的 `%`（t62 已下沉 rt_num_arith op 4）；BigInt 超 i64 整数取模（既有 CG1 陷阱域）
- [x] codegen none 值的 print/toString/==（t81）
    - 选型：CG2 缺口的 none 部分——解释器 none 函数调用结果可 print（打 "none"）/toString/插值/相等比较（values_equal None 分支恒 true），colliec 三触点分别拒编 "print of 'none' value"、"string conversion of this value"、"non-numeric operand of '=='"；预检实证：print(greet()) 解释器输出 side↵none、print(1, greet(), "x") 输出 side↵1 none x、toString(f())="none"、@"got {f()}"="got none"、f()==f() 为 true
    - 范围拍板：三触点——gen_print Void case 打常量串 "none"（替换拒编，两阶段收集 Void 值 value=nullptr 无碍）、to_str Void case 返回 "none" 常量串（覆盖 toString 与插值脱糖）、visitBinary ==/!= 双 Void 前置分支恒 true/false（两侧已 emit 副作用保序）；零新增 rt 接口
    - 实现：code_generator.cpp 三处——gen_print Void case（CreateGlobalString("none") → rt_print_str）、to_str Void case（返回 "none" 常量串）、visitBinary 比较分支链首加 Void 分支（双 Void 恒 true / Void×非Void 恒 false，!= 取反）
    - 验证：新差分用例 s34_none_value（print 单/多参混排副作用保序、print(quiet(),quiet())、toString/插值/s.length=4、==/!= 双 Void、副作用求值序、bool 变量参与逻辑运算、if 条件、类方法体内 print("box", quiet())）；实证——true % 2.0 已于 t80；none 拼接 "v="+f() 语义层两端一致拦截 "Invalid operands for string concatenation"、f()==1 拦 "Incomparable operand types"、print(none) 字面量 parser 两端一致 Parse error；ctest -C Release 差分 33/33 逐字节一致（s34 首跑即过），Debug 门禁 6/6
    - 范围外：Void×非Void ==、==? none 目标、none 拼接（语义层两端一致拦截实证）；`none` 非表达式字面量（parser 两端一致 Parse error）；none 变量声明 `none n = f()`（解释器支持但价值低、牵动声明面，codegen 维持拒编）；tuple/数组含 none 元素、三元 none 分支（维持既有拒编）
- [x] codegen 实例（Obj）相等比较（t82）
    - 选型：拒编面盘点（约 120 处 unsupported 归类）后，最小规模且有实测价值的解锁面——实例（Obj）相等 `==`/`!=`/`==?`/switch；预检实证：解释器 values_equal Instance 落 default 恒 false（`a==b`→false、`a!=b`→true、**同一实例 `a==a` 也 false**），`==?`/switch 目标为实例时全部不命中（miss/default）；colliec 两触点拒编 "non-numeric operand of '=='"（visitBinary L572）、"'==?' comparison of these value types"（gen_match_eq L2590）；候选零 rt 改动、零结构改动，`gen_tuple_eq` L2620-2624 已有"任一 Obj 元素恒 false"先例。排除 tribool 混型（`tribool==integer`/`==string` 语义层两端一致拦截 "Incomparable operand types"，无实测差分价值）
    - 范围拍板：两触点常量折叠——visitBinary 比较分支在 require_numeric 前加 Obj 分支（任一侧 Obj 且 ==/!=：eq=false 常量、!= 取反，镜像 Tup 分支 L483-493）；gen_match_eq 末尾拒编前加 Obj 分支（任一侧 Obj → false，镜像 Tup/Arr L2574-2588，覆盖 ==?/switch）；关系比较 `<`/`<=`/`>`/`>=` 仍落 require_numeric 拒编（解释器同样不支持）；零新增 rt 接口
    - 实现：code_generator.cpp 两处——visitBinary 比较分支在 Arr 分支之后加 Obj 分支（任一侧 Obj 且 ==/!= 时 eq=false 常量、!= 取反）；gen_match_eq 末尾拒编前加 Obj 分支（任一侧 Obj → false）
    - 验证：新差分用例 s35_object_eq（不同实例/同一实例 ==/!=、引用别名 == 仍 false、比较结果进逻辑与 if/else 分支、==? 目标为实例全 miss/def、switch 目标为实例全走 default、switch 无 default 静默跳过、函数内实例相等、实例相等结果再参与 ==/!= 复合表达式）；实证——实例关系比较 `a < b` 两端一致拦截（解释器 runtime "Comparison operands must be both numbers or both strings"、colliec 拒编 "non-numeric operand of '<'"）；ctest -C Release 差分 34/34 逐字节一致（s35 首跑即过），Debug 门禁 6/6
    - 范围外：实例关系比较 `<`/`<=`/`>`/`>=`（两端一致拦截实证）；tribool 混型相等（语义层拦截，无实测价值）；实例作为 tuple/数组元素相等（`gen_tuple_eq` 已恒 false，本任务前即支持）
- [x] codegen 非常量 tuple 索引（t83）
    - 选型：拒编面盘点后选定"解释器支持、机制就绪、单触点"的解锁面——非常量 tuple 索引 `t[i]`（i 为变量）；预检实证：解释器同质/异质 tuple 均动态求值（`homo[i]`/`homo[i+1]`/`het[j]` 含负索引），colliec visitIndex L1074 拒编 "non-constant tuple index"；排除 Num 整态位运算/移位/`~`（语义层两端一致拦截 "Bit operands expected for bitwise operation"/"Left operand must be a bit type"，number 非 bit 类型无实测差分价值）、tuple `get()` 动态键（同构候选，留作后续 t84）
    - 范围拍板：visitIndex Tup 分支加非常量索引路径——限**同质** tuple（所有元素同 CGType 且 ∈ {Int/Double/Bool/Str}，结果类型静态可定）：物化为运行时数组（rt_arr_new + 逐元素 rt_arr_set）后 rt_arr_get(动态 idx) 取值，复用负索引归一化 + 越界陷阱（消息 "Index N out of range (size M)" 与解释器 normalize_index/collie_rt_arr_norm_index 完全一致）；异质 tuple、Num/嵌套(Tup/Arr/Obj)元素、空 tuple 保持拒编（结果类型静态不可定 / 数组槽无法承载，拒编不错编）；零新增 rt 接口
    - 实现：code_generator.cpp visitIndex Tup 分支重构——常量索引路径不变（内联至 if(const_int_of)），新增非常量路径：空 tuple / 元素非 {Int/Double/Bool/Str} / 异质三重守卫（分别拒编），过守卫后 rt_arr_new + 逐元素 elem_to_bits/rt_arr_set 物化、rt_arr_get(动态 idx) 取 bits、bits_to_elem 还原为元素类型；**关键修复**：`const CGTuple& t` 改按值拷贝 `const CGTuple t`——非常量路径 emit(index) 可能触发 register_tuple 扩容 tuple_values_ 致引用悬垂（`t[idxs[0]]` 嵌套索引实测 0xC0000005，与 gen_tuple_eq 同一防护）
    - 验证：新差分用例 s36_tuple_index（同质 integer 变量/表达式/负索引变量、string/bool/decimal 同质 tuple 动态索引、命名同质 tuple 索引（名字不影响）、字面量负索引回归常量路径、索引结果参与算术/比较、for 循环动态索引遍历、`t[idxs[0]]` 嵌套索引（常量路径产值作动态索引）、函数内局部 tuple 动态索引）；实证——越界 `t[5]` 两端一致 "Index 5 out of range (size 3)"、异质 tuple 非常量索引 colliec 拒编 "non-constant index on heterogeneous tuple"（解释器动态可求值）；ctest -C Release 差分 35/35 逐字节一致（s36 修复悬垂后过），Debug 门禁 6/6
    - 范围外：异质 tuple 非常量索引（结果类型静态不可定，拒编实证）；Num/嵌套(Tup/Arr/Obj)元素同质 tuple（数组槽 elem_to_bits 仅 4 类无法承载）；空 tuple 非常量索引；tuple 作函数形参/实参（"tuple in function signature" 既有拒编）；`(t[i]==literal)`/`&&` 混逻辑（语义层不追踪 tuple 元素类型，两端一致 "Incomparable operand types"）；tuple `get()` 动态键（同构候选，留作 t84）
- [x] codegen 非常量 tuple get() 动态键（t84）
    - 选型：t83 明确记录的同构候选——tuple `t.get(k)`（k 为运行期字符串变量/表达式）；预检实证：解释器动态求值（`t.get("age")` 变量键返 20/30），未命中抛 `Runtime error at line N, column M: Undefined tuple field '<key>'`（核心消息 "Undefined tuple field '<key>'"）；colliec visitMethodCall L1256 拒编 "non-constant tuple get() key"
    - 范围拍板：visitMethodCall Tup+get 分支加动态键路径——限**同质命名** tuple（元素同 CGType 且 ∈ {Int/Double/Bool/Str}、≥1 非空名，结果类型静态可定）：物化 names 数组（Str）+ values 数组（elem）后新增 rt 接口 `collie_rt_tuple_get(names, vals, key)` 按非空名 strcmp 扫描，命中返 i64 bits（bits_to_elem 还原）、未命中打 "Undefined tuple field '<key>'" + exit(1)（核心消息与解释器一致，位置前缀缺失同 t83 越界陷阱既定分歧）；异质/非 4 类元素/空 tuple/无命名字段/非 Str 键保持拒编（拒编不错编）；新增 1 个 rt 接口
    - 实现：collie_rt.c 加 collie_rt_tuple_get（names/values 等长逐位对应，空名槽 `name[0] != '\0'` 天然跳过，对齐解释器非空名匹配）；code_generator.cpp visitMethodCall Tup+get 分支重构——常量字符串键回归编译期解析路径（t68 不变），动态键路径四守卫（空 tuple / 非 4 类元素 / 异质 / 无命名字段）后 emit(key) 校验 Str、rt_arr_new 物化 names（kind 3，无名元素存空串 ""）+ values（elem kind，逐元素 elem_to_bits）、rt_tuple_get 取 bits、bits_to_elem 还原；`const CGTuple t` 按值拷贝防 emit(key) 触发 register_tuple 扩容悬垂（与 t83 visitIndex 同一防护）
    - 验证：新差分用例 s37_tuple_get（integer/string/bool/decimal 同质命名 tuple 变量键、常量键回归、混合命名+无名（只匹配非空名）、拼接键 `pre + "pha"`、`"m" + toString(idx)` 循环动态键、两 get 结果相加、函数内局部 tuple 动态 get）；实证——未命中键两端核心消息一致 "Undefined tuple field 'missing'"（编译产物 exit 1）、异质 / 无命名字段 / 非 string 键 / 嵌套元素四守卫分别拒编 "non-constant get() on heterogeneous tuple" / "non-constant get() on tuple with no named fields" / "non-string tuple get() key" / "non-constant tuple get() on this element type"；ctest -C Release 差分 36/36 逐字节一致，Debug 门禁 6/6
    - 范围外：异质 / 无命名字段 / 空 tuple / Num/嵌套(Tup/Arr/Obj)元素 tuple 动态键（结果类型静态不可定或数组槽无法承载，拒编实证）；非 string 键（解释器亦报错，编译期更早拦截）；get() 返回值直接作 object 动态使用（语义层 get 返回 KW_OBJECT，codegen 侧还原为静态元素类型，同质前提下语义一致）
- [x] codegen 嵌套数组（t85）
    - 选型：剩余拒编面盘点（约 90 处 unsupported 归类核实：array/tuple length、len(array)、decimal 取模、to_str/print 的 Arr/Obj/Tup/Void、字符串/number/tribool 方法面均已打平；len(tuple)/toNumber(array) 等两端一致报错非差分面；tuple 进函数签名需运行时物化属架构级改动）——嵌套数组是「解释器支持 + 机制半就绪 + 规模中」的最优活跃候选；预检实证：解释器完整支持（字面量/print `[[1, 2], [3, 4]]`/逐层索引读写/别名引用联动/length/len），colliec visitArrayLiteral L1035 拒编 "nested array literal"
    - 范围拍板：新增数组 kind 4（元素为数组，槽存内层数组 ptr bits，PtrToInt/IntToPtr 同 Str 先例）——限**两层且内层数值系**（内层 elem ∈ {Int/Double/Num}，保动态域不变量 kind∈{0,1}）：visitArrayLiteral 放行全 Arr 元素字面量、elem_to_bits/bits_to_elem/arr_kind_of 加 Arr case、visitIndex object.elem==Arr 读出 {Arr, elem=Num 动态域哨兵}（内层读写/print/len 全走既有动态域机制）、visitIndexAssign 整槽替换内层数组、rt_arr_to_str/rt_arr_eq 加 kind 4 递归；内层 bool/str/≥3 层拒编；嵌套数组进函数签名/字段/返回值由 t70/t71 既有守卫（elem 限数值系）天然拒编；零新增 rt 接口
    - 实现：visitArrayLiteral Arr 元素两守卫（elem==Arr 拒 "array nesting deeper than two levels"、elem ∉ 数值系拒 "nested array with non-numeric inner elements"）后走既有同质推断（elem=Arr → arr_kind_of 4 → elem_to_bits PtrToInt 存槽）；visitIndex object.elem==Arr 分支 IntToPtr 还原内层 ptr、elem 记 Num 哨兵；visitIndexAssign object.elem==Arr 分支限数值系内层数组整槽替换；rt_arr_to_str case 4 递归转串（string 显式 case 3）、rt_arr_eq kind 4 递归深比较（kind 4 × 其它 kind 落既有恒不等）
    - 验证：s38 差分 13 行逐字节一致（含内层混合 Int/Double 提升 `[[1.5, 2]...]`、别名联动、整槽替换、深比较改元素翻 false、m[0].length 链式）；四拒编实证（三层嵌套/内层 bool/嵌套数组作实参（t70 既有守卫消息 "passing bool/string array as argument"）/整槽替换写入 string 内层数组）；差分 37/37 + Debug 6/6
    - 范围外：≥3 层嵌套与内层 bool/str 数组（动态域不变量 kind∈{0,1} 无法承载，拒编不错编）；嵌套数组进函数签名/类字段/返回值（t70/t71 守卫拦截，消息沿用 "bool/string array" 措辞未改）；嵌套数组元素进 tuple（gen_tuple_eq/物化路径既有拒编）
- [x] codegen 类继承向上转型（t86）
    - 选型：剩余拒编面盘点核实（约 140 处 unsupported 归类：字符串方法面不成立——解释器 string 方法仅 trim 系/subString，codegen 已齐平；tribool 关系比较两端一致运行期报错非差分面；函数/方法重载为陷阱面——语义层 find_best_overload 按签名择优但解释器 env_.define 单值覆盖实为"后定义者胜"，两端语义本就不一致，排除并记录；tuple 进函数签名需运行时物化属架构级）——向上转型是「解释器完整支持 + 差分缺口实证 + 机制可承载」的最优活跃候选；预检实证：解释器 4 行输出（子类实例传父类形参 `intro(a Animal)`、返回父类 `make() Animal`、父类变量槽 `Animal b = d`，覆写方法沿动态类分派生效），colliec 拒编 "returning instance of class 'Dog' where 'Animal' is declared"（三触点：coerce_call_arg L2175 / visitReturn L2052 / coerce_for_slot L2281）
    - 范围拍板：对象头类 id + 调用点「实现唯一直调 / 多实现 switch」动态分派——(1) 对象 struct 头部加 i64 类 id（注册序分配），字段 GEP 下标整体 +1，visitNew 分配 +8 字节并写 id；(2) is_subclass_of 沿 CGClass.super 链判祖先；(3) coerce_call_arg/visitReturn/coerce_for_slot 三触点放行子类→父类（静态 cls 记声明类，向下转型维持拒编）；(4) visitMethodCall Obj 分支：静态 cls 无子类 → 直调（现状零开销）；有子类 → 读头部 id switch 到子树各类既有单态化实例（collie.<分派类>.<定义类>.<方法名>，模板方法 this 分派天然正确）+ PHI 合流，各副本签名不一致拒编；(5) Obj ==/!= 恒 false 折叠不动——解释器 values_equal 无 Instance 分支恒 false（含 a==a，t82 实证），upcast 不改变该语义
    - 实现：register_class_layout 分配 `cls.id = classes_.size()` + StructType 元素 0 加 i64 头部；visitNew 尺寸 8 起算 + store 类 id + 字段 GEP +1（visitProperty/visitPropertyAssign 同步 +1）；is_subclass_of 沿 super 链上溯；coerce_call_arg/visitReturn/coerce_for_slot/visitVarDecl(IDENTIFIER)/visitAssign(Obj) 五触点 `!is_subclass_of` 才拒编；visitMethodCall Obj 分支收集后代类（按 id 排序保 IR 确定性）——无后代直调，有后代签名防御校验后 load 头部 id + switch（default=静态类）各 arm 调既有单态化实例 + PHI 合流
    - 验证：s39 差分 11 行逐字节一致（三级链 Animal←Dog←Puppy：子类传父类形参/中层静态类形参/返回父类装子类/父类槽覆写+继承方法混调/父类字段前缀读取/类字段 upcast/无后代直调/base.info() 静态绑定）；三拒编实证（rej1 downcast "initializing 'B' variable with incompatible value"、rej2 无关类同消息、rej3 父类静态类型调子类特有方法——解释器 42 成功 vs codegen 拒编 "undefined method 'only' on class 'A'"，拒编不错编陷阱面）；差分 38/38 + Debug 6/6
    - 范围外：downcast/无关类互赋（拒编）；父类静态类型调子类特有方法（解释器动态类可成功，codegen 拒编不错编）；覆写变签名（防御拒编 "overriding method with a different signature"）；函数/方法重载（陷阱面已记录，两端语义本就不一致）；零新增 collie_rt 接口
- [x] codegen byte/word 类字段（t87）
    - 选型：剩余拒编面盘点核实（子代理候选逐一对照实测：char 字面量 L260/byte-word 变量 t69/构造函数实参 t61 均已实现非缺口；byte/word 进函数签名被语义层拦截 "No matching overload for function"——两端一致非差分面，实证）——byte/word 类字段是实证成立的最小差分缺口：预检解释器 4 行输出（byte/word 字段初始化/读取/赋值 255/表达式域 p.level+1=256 无截断），colliec 拒编 "variable type 'byte'"（register_class_layout L2503 走 declared_cgtype default）
    - 范围拍板：CGVar.bit_max 先例平移到字段——(1) CGField 加 bit_max（255/65535，0 即非位类型）；(2) register_class_layout 字段类型 KW_BYTE/KW_WORD 前置分支：ftype=Int（i64 槽 8 字节，visitNew 尺寸累计不变）+ 记 bit_max；(3) visitNew 字段初始化与 visitPropertyAssign 两触点：coerce_for_slot 后 field.bit_max>0 插 check_bit_range 范围陷阱（复用 t69，对齐解释器 coerce_to_declared KW_BYTE/KW_WORD 赋值点校验）；(4) visitProperty 读出恒 Int 无需改（表达式域无截断，对齐 t69 变量语义）；(5) 继承字段 bit_max 随父类前缀拷贝天然工作，纳入用例；初始值/赋值非 integer 拒编不错编（coerce_for_slot 既有 Int 槽严格性，解释器为运行期报错）
    - 实现：CGField 加 bit_max 字段；register_class_layout KW_BYTE/KW_WORD 前置分支（ftype=Int + fbit_max 255/65535，push_back 聚合初始化补第五参）；visitNew 字段初始化与 visitPropertyAssign 两触点 coerce_for_slot 后 field.bit_max>0 插 check_bit_range（消息 "Value out of range for 'byte/word'" 与解释器一致）；visitProperty 读出路径零改动
    - 验证：s40 差分 10 行逐字节一致（构造器内 this.field 赋值/边界值 255与65535与0/表达式域无截断 300·400·60200/方法体内赋值恰达上界 255/继承字段范围保持 254·65000·65254）；两陷阱实证（trap1 赋值 256 双端 "Value out of range for 'byte' (expected 0-255, got 256)"、trap2 初始值 70000 双端 word 越界报错，均输出前缀一致后陷阱）；一拒编实证（rej1 byte 字段初始值 "x"——语义层双端同拦 "Cannot initialize variable of type 'KW_BYTE'"）；差分 39/39 + Debug 6/6
    - 范围外：byte/word 进函数签名（语义层 "No matching overload" 拦截，两端一致非差分面）；非 integer 初始值/赋值（语义层双端同拦）；零新增 collie_rt 接口
- [x] codegen bool/string 数组动态域透传（t88）
    - 选型：剩余拒编面盘点核实（子代理候选对照实测确认：四守卫行号准确——visitAssign L961 动态槽来源限数值系 / visitReturn L2142 / coerce_call_arg L2268 / coerce_for_slot L2369；rt 侧 arr_to_str case 2/3、arr_eq kind 2/3、arr_len 已全 kind 覆盖——print/len/==/toString 透传后天然安全零改动；真难点收敛两处：动态域读 visitIndex L1169 把运行时 kind 直拼 Num tag（kind 2/3 放行即错编）、动态域写 L1213 rt_arr_set_num 仅 tag 0/1；解释器 coerce_to_declared KW_ARRAY 只查"是数组"全线放行，差分缺口成立；for-in 不存在，消费面收敛为索引读写/print/len/==/透传）——候选 2 嵌套内层 bool/str 依赖本项后置，候选 3 顶层结构包规模更大后置
    - 范围拍板：「透传子集」——(1) 解除四守卫：bool/str 数组过签名/返回值/字段槽/动态槽赋值指针透传（kind 随数组对象自带）；(2) 动态域读插运行时守卫：kind ≥ 2 陷阱退出（新缺口 CG9——元素静态类型不可定，解释器可行、编译产物显式陷阱不错值，同 CG7 先例；嵌套内层读 kind 恒 0/1 守卫恒通过无害），新增 rt 接口 collie_rt_trap_arr_kind；(3) 动态域写泛化：值 Bool（tag 2 zext）/Str（tag 3 PtrToInt）放行下沉 rt_arr_set_num——tag==kind 直存天然覆盖 2/3、0→1 提升保留、其余 mismatch 陷阱（CG7 消息泛化）；(4) print/len/==/toString 零改动；范围外：嵌套数组内层 bool/str（字面量守卫不动）、动态域索引读出 bool/str 元素（CG9 陷阱面）
    - 实现：code_generator.h 加 rt_trap_arr_kind_ 成员；code_generator.cpp 六处——注册 collie_rt_trap_arr_kind、visitAssign 动态槽来源守卫解除、visitIndex 动态域读插 dynkind.trap/cont 块（icmp ugt kind,1 → 陷阱 + unreachable）、visitIndexAssign 动态域写 Bool/Str 分支（arr_kind_of tag + elem_to_bits 直写 rt_arr_set_num）、visitReturn/coerce_call_arg/coerce_for_slot 三守卫删除；collie_rt.c 两处——arr_set_num mismatch 消息泛化 "array element type mismatch"、新增 collie_rt_trap_arr_kind（kind 2/3/其它 报 bool/string/nested，stderr+exit(1)）
    - 验证：s41 差分 15 行逐字节一致（bool/str 数组作实参 show/透传返回 pick+==（同内容 true/异内容 false/kind 2×3 恒 false）/类字段 Bag 构造器存 str 数组+swap 方法字段读出换存返回/动态域写 bool-str 值引用联动+负索引/数值数组动态域读写回归 40·3.75/嵌套数组透传 print-len-==）；两陷阱实证（trap1 经签名 bool 数组 f[0] 读——解释器 before+true vs 产物 CG9 "reading bool array element in dynamic context"；trap2 str 值动态域写 int 数组——解释器 before+[oops, 2] vs 产物 CG7 泛化消息）；差分 40/40 + Debug 6/6
    - 范围外：嵌套数组字面量内层 bool/str（visitArrayLiteral 守卫不动）；动态域索引读出 bool/str/嵌套元素（CG9 陷阱面，候选后续任务）；collie_rt 新增 1 个陷阱接口
- [x] codegen 嵌套数组放宽（内层 bool/str + ≥3 层）（t89）
    - 选型：剩余拒编面盘点（子代理全量归类 + 触点核实）——候选 A 嵌套数组放宽最小规模最高价值：visitArrayLiteral 两守卫（L1042 "array nesting deeper than two levels" / L1046 "nested array with non-numeric inner elements"）+ visitIndexAssign 整槽替换限数值系（L1214）共三触点；rt 侧零改动（elem_to_bits Bool/Str/Arr 全覆盖、rt_arr_to_str case 2/3/4 递归、rt_arr_eq kind 3 strcmp/kind 4 递归已就绪，t85/t88 铺垫）；预检实证：解释器内层 bool/str 字面量/三层嵌套/整槽替换/深比较 6 行全可跑 vs colliec 字面量拒编，活跃差分面成立；候选 B（Num 元素数组字面量）、候选 E（tuple 结构包，7+ 触点规模大）后置
    - 范围拍板：(1) visitArrayLiteral 两守卫解除——任意 elem 内层数组（Bool/Str/Arr 即 ≥3 层）进 kind 4 槽；(2) visitIndexAssign elem==Arr 整槽替换放宽为任意元素内层数组；(3) visitIndex elem==Arr 分支注释更新（内层 kind 可 ≥2，内层索引读经动态域 kind≥2 落 t88 既有 CG9 陷阱，拒编转陷阱不错值）；范围外：内层元素经动态域索引读出（CG9 陷阱面）、Num 元素数组字面量（候选 B）；零新增 collie_rt 接口
    - 实现：code_generator.cpp 三处——visitArrayLiteral 两守卫块删除（任意 elem 内层数组放行进 kind 4 槽，kind 随内层对象自带）+ 头注释更新；visitIndexAssign elem==Arr 分支条件放宽为仅 `v.type != Arr` 拒编（非数组值写 kind 4 槽拒编不错编，解释器可行故为活跃拒编实证面）；visitIndex elem==Arr 分支零代码改动仅注释（内层读出记 Num 哨兵，内层 kind≥2 索引读落 CG9 陷阱）
    - 验证：s42 差分 21 行逐字节一致（内层 bool/str 字面量/print/length/len/bs[0] 读+length/整槽替换 str 内层/深比较 3 例/三层嵌套 print+==+整槽替换/混合内层 kind/别名联动/数值内层逐层读写回归 m[0][1]=25）；陷阱实证 trap1（bs[0][0] 内层 bool 动态域读——解释器 before+true vs 产物 CG9 "reading bool array element in dynamic context"）；拒编实证 rej1（m[0]=5 非数组值写 kind 4 槽——解释器 [5, [3]] vs colliec 拒编 "array element type mismatch in index assignment"）；差分 41/41 + Debug 6/6
    - 范围外：内层元素经动态域索引读出（CG9 陷阱面，候选后续任务）；Num 元素数组字面量（候选 B 后置）；零新增 collie_rt 接口

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
| 设计愿景 > 已实现，文档多为占位 | 🟢 低 | ✅ t46 建立 `compiler/SPEC.md` 实现规范草稿（以代码为准逐条核实，差距登记为缺口 G1–G10），后续随任务同步更新 |
| 依赖在线拉取 GoogleTest | 🟢 低 | M0 改离线友好，评估 doctest |
| 无 CI / 无端到端测试 | 🟢 低 | ✅ CI 已加（M3，Windows+Linux 矩阵，跑 `lexer_tests`+`interpreter_tests`）；端到端测试已随 M4 解释器建立 |
| 用户自定义函数不可执行 | ✅ 已修复 | t11 完成：Value+Function、ReturnSignal、visitFunction/visitCall/visitReturn，递归可用 |
| 数字类型统一用 `double`，未区分 integer/decimal | ✅ 已修复 | t42：BigInt 任意精度整数 + Value 整数/小数双表示，integer/decimal/number 三类型闭环 |

---

## 六、待与作者确认的语言设计问题

> 实现过程中遇到语法歧义会在此登记，逐条与作者确认后更新。

- [x] 源文件后缀：`.collie` 为主、`.col` 为别名 —— 已确认
- [x] helloworld 的 `print`：内建函数 `print(...)`（任意实参）；字符串转义 `\n \t \\ \"` 由词法器解码，解释器原样输出 —— 已确认
- [x] 数值除零行为：IEEE 754（`1/0 → +Infinity`、`0/0 → NaN`）—— 已确认（t33）
- [x] `class` 纳入实现范围：按 **Java/C# 风格最小子集**实现（字段/方法/`new`/`this`，继承后续再做；以 uncategorized.md 附录示例为准）—— 已确认（t34 已完成）
- [x] `tribool`（三态布尔）与 `==?` 运算符的确切语义 —— 已确认（t43/t44）：
    - 逻辑运算 `! && ||` 采用 **Kleene 三值逻辑**（`!unset=unset`、`unset&&false=false`、`unset&&true=unset`、`unset||true=true`、`unset||false=unset`）
    - **`if()`/条件表达式必须是 bool**，tribool 不能直接作条件；需显式写 `t.isTrue()/.isFalse()/.isUnset()`（tribool 内建方法，返回 bool）或 `t == true/false/unset`
    - `==?` 是**通用多路匹配**运算符（不限 tribool，类 switch 表达式）；tribool 匹配需穷尽三态或给默认分支，其他类型必须有默认分支；命中第一个匹配分支（见 uncategorized.md 运算符节）
    - `==?` 归组/默认分支消歧义规则（t44 补充确认）：**末尾裸表达式 = 默认分支，且只能在末尾；其余裸值一律与后面最近的「值: 结果」归组**；原文档示例 5（默认在前 `a ==? 2, unset: 1`）不再支持，uncategorized.md 已同步修正
- [x] tuple 成员访问语法 —— 已确认（t45）：**按索引 `t[0]`、命名字段 `t.name`、动态获取 `t.get("key")`**；不采用 Rust 风格 `.0`（避免与前导点小数 `.5` 的词法冲突）、不采用 C# 风格 `Item1`；uncategorized.md 的 `coords.Item1` 示例同步修正

---

## 七、变更日志

> 与 git 提交一一对应，最新在上。

- 2026-07-31 `feat(compiler)`: codegen 嵌套数组放宽（t89，M6）：解除 t85 两限制——visitArrayLiteral 两守卫删除（"array nesting deeper than two levels" 与 "nested array with non-numeric inner elements"），任意 elem 内层数组（bool kind 2 / str kind 3 / 更深嵌套 kind 4 即 ≥3 层）放行进 kind 4 槽，kind 随内层对象自带（arr_kind_of），elem_to_bits Bool zext / Str·Arr PtrToInt 全覆盖；visitIndexAssign elem==Arr 整槽替换条件放宽为仅 `v.type != Arr` 拒编——任意元素内层数组放行，非数组值写 kind 4 槽维持拒编不错编（解释器可行，活跃拒编面实证）；visitIndex elem==Arr 分支零代码改动（内层读出记 Num 动态域哨兵 t85 机制不变，内层 kind≥2 索引读落 t88 既有 CG9 陷阱 collie_rt_trap_arr_kind 不错值，数值系内层逐层读写照常）；rt 侧零改动（rt_arr_to_str case 4 递归、rt_arr_eq kind 3 strcmp/kind 4 递归/跨 kind 恒不等，深度无关天然覆盖，t85/t88 铺垫就绪）；零新增 collie_rt 接口；新差分用例 s42_nestedwide（内层 bool/str 字面量/print/length/len/bs[0] 读+length/整槽替换 str 内层/深比较（同异内容与跨 kind）3 例/三层嵌套 print+==+整槽替换/混合内层 kind/别名联动/数值内层逐层读写回归，21 行输出）+ 陷阱实证（bs[0][0] 内层 bool 动态域读——解释器 true vs 产物 CG9 陷阱）+ 拒编实证（m[0]=5 非数组值整槽写——解释器 [5, [3]] vs colliec 拒编），ctest -C Release 差分 41/41 逐字节一致，Debug 门禁 6/6（M6 t89）
- 2026-07-31 `feat(compiler)`: codegen bool/string 数组动态域透传（t88，M6）：解除 t70/t71 四守卫（visitAssign 动态槽来源限数值系 / visitReturn / coerce_call_arg / coerce_for_slot 的 "elem 限 {Int,Double,Num}" 拒编全部删除）——任意元素数组（bool kind 2 / str kind 3 / 嵌套 kind 4）过函数签名/返回值/类字段槽/动态槽赋值指针透传，kind 随数组对象自带（对齐解释器 coerce_to_declared KW_ARRAY 只查"是数组"不查元素类型）；print/len/==/toString 零改动天然安全（rt_arr_to_str case 2/3/4、rt_arr_eq kind 2/3/4、rt_arr_len 均已全 kind 覆盖，t88a 核实）；动态域索引读（visitIndex elem==Num 分支）插运行时守卫 icmp ugt kind,1 → dynkind.trap 调新增 rt 接口 collie_rt_trap_arr_kind(kind) + unreachable（**新缺口 CG9**：bool/str/嵌套数组经透传后元素静态类型不可定，解释器动态类型可行、编译产物陷阱退出不错值；数值系 kind 0/1 恒通过无害）；动态域索引写（visitIndexAssign elem==Num 分支）Bool/Str 值放行——打对应 kind tag（arr_kind_of 2/3）+ elem_to_bits 直写下沉 rt_arr_set_num（tag==kind 直存天然覆盖、0→1 提升保留、其余 mismatch 落 CG7 陷阱，消息泛化 "array element type mismatch"）；新差分用例 s41_dynarray（bool/str 数组作实参/透传返回+==（同内容/异内容/kind 2×3 恒 false）/类字段存取与方法内换存/动态域写 bool-str 值引用联动+负索引/数值域读写回归/嵌套数组透传 print-len-==，15 行输出）+ 两陷阱实证（经签名 bool 数组索引读——解释器 true vs 产物 CG9 陷阱；str 值动态域写 int 数组——解释器 [oops, 2] vs 产物 CG7 泛化消息），ctest -C Release 差分 40/40 逐字节一致，Debug 门禁 6/6（M6 t88）
- 2026-07-31 `feat(compiler)`: codegen byte/word 类字段（t87，M6）：CGVar.bit_max 先例平移到字段——CGField 加 `bit_max`（255/65535，0 即非位类型）；register_class_layout 字段类型 KW_BYTE/KW_WORD 前置分支（IDENTIFIER 分支后、declared_cgtype 之前）：ftype=Int（i64 槽 8 字节，visitNew 尺寸累计不变）+ 记 bit_max，push_back 聚合初始化补第五参；visitNew 字段初始化与 visitPropertyAssign 赋值两触点 coerce_for_slot 后 `field.bit_max > 0` 插 check_bit_range 范围陷阱（t69 机制复用，无符号比较一次覆盖负数与超上限，消息 "Value out of range for 'byte' (expected 0-255, got N)" 与解释器 coerce_to_declared 一致）；visitProperty 读出恒 Int 零改动（表达式域无截断，对齐 t69 变量语义）；继承字段 bit_max 随父类前缀拷贝天然保持；零新增 collie_rt 接口；选型排除记录：char 字面量/byte-word 变量（t69 已支持）、构造函数实参（t61 已实现）非缺口，byte/word 进函数签名被语义层拦截 "No matching overload"（两端一致非差分面，实证）；新差分用例 s40_bitfield（构造器内 this.field 赋值/边界值 255与65535与0/表达式域无截断 300·400·60200/方法体内赋值恰达上界/继承字段范围保持，10 行输出）+ 两陷阱实证（赋值 256、初始值 70000 双端越界报错一致）+ 一拒编实证（byte 字段初始值 "x" 语义层双端同拦），ctest -C Release 差分 39/39 逐字节一致，Debug 门禁 6/6（M6 t87）
- 2026-07-31 `feat(compiler)`: codegen 类继承向上转型（t86，M6）：对象头类 id + 调用点动态分派解锁 upcast——register_class_layout 注册序分配 `cls.id`、StructType 元素 0 加 i64 类 id 头部（字段 GEP 下标整体 +1，visitNew/visitProperty/visitPropertyAssign 三处同步），visitNew malloc 尺寸 8 起算并 store 类 id；新增 is_subclass_of 沿 CGClass.super 链判真后代；coerce_call_arg/visitReturn/coerce_for_slot/visitVarDecl(IDENTIFIER 初始化)/visitAssign(Obj) 五触点改 `cls 不等 && !is_subclass_of` 才拒编（子类实例进父类形参/返回值/变量与类字段槽/赋值全放行，槽静态 cls 记声明类，downcast/无关类维持拒编）；visitMethodCall Obj 分支动态分派——收集静态 cls 全部后代类（按 id 排序保 IR 确定性）：无后代直调既有单态化实例（零开销行为不变），有后代先防御校验各后代覆写签名一致（否则拒编 "overriding method with a different signature"）再 load 头部 id + switch（default=静态类 arm，case=各后代 arm，每 arm 调 collie.<分派类>.<定义类>.<方法名> 既有单态化实例）+ 非 Void PHI 合流；单态化副本使模板方法 this.m() 经分派后天然按动态类解析、base.m() 静态绑定不受影响；Obj ==/!= 恒 false 折叠不动（解释器 values_equal 对 Instance 恒 false 含 a==a，t82 实证）；零新增 collie_rt 接口；选型排除记录：字符串方法扩展面不成立（解释器仅 trim 系/subString 已齐平）、tribool 关系比较两端一致运行期报错非差分面、函数/方法重载为陷阱面（语义层 find_best_overload 择优 vs 解释器 env_.define 后定义者胜，两端本就不一致）；新差分用例 s39_upcast（三级链 Animal←Dog←Puppy：子类传父类形参/中层静态类形参 bark(x Dog) 收 Puppy/返回父类装子类/父类槽覆写+继承方法混调/父类字段前缀读取/类字段 upcast/无后代直调/base.info() 静态绑定，11 行输出）+ 三拒编实证（downcast、无关类互赋、父类静态类型调子类特有方法——解释器 42 成功 vs colliec 拒编 "undefined method 'only' on class 'A'" 拒编不错编陷阱面），ctest -C Release 差分 38/38 逐字节一致，Debug 门禁 6/6（M6 t86）
- 2026-07-30 `feat(compiler)`: codegen 嵌套数组（t85，M6）：新增数组 kind 4（元素为数组，槽存内层数组 ptr 位模式，PtrToInt/IntToPtr 同 Str kind 3 先例）——限**两层且内层数值系**（内层 elem ∈ {Int/Double/Num}，保 t70 动态域不变量 kind∈{0,1}）：visitArrayLiteral 放行全 Arr 元素字面量（elem==Arr 拒 "array nesting deeper than two levels"、内层 bool/str 拒 "nested array with non-numeric inner elements"，混合 Arr/非 Arr 落既有异质拒编）、elem_to_bits/bits_to_elem/arr_kind_of 加 Arr case（kind 4）、visitIndex object.elem==Arr 分支 IntToPtr 还原内层数组 ptr 且 elem 记 Num 动态域哨兵（内层索引读写/print/len 全走 t70 既有动态域机制零新码）、visitIndexAssign object.elem==Arr 分支限数值系内层数组整槽替换、rt_arr_to_str 加 case 4 递归转串 + rt_arr_eq 加 kind 4 递归深比较（kind 4 × 其它 kind 落既有恒不等），零新增 rt 接口；嵌套数组进函数签名/类字段/返回值由 t70/t71 既有守卫（elem 限数值系）天然拒编；新差分用例 s38_nested_array（字面量/print/逐层索引读写/负索引/别名引用联动/整槽替换/内层 Int-Double 混合提升/深比较（含改元素翻 false）/length/len/m[0].length 链式/toString）+ 四拒编实证（三层嵌套/内层 bool/作实参/整槽替换写 string 内层），ctest -C Release 差分 37/37 逐字节一致，Debug 门禁 6/6（M6 t85）
- 2026-07-29 `feat(compiler)`: codegen 非常量 tuple get() 动态键（t84，M6）：visitMethodCall Tup+get 分支加动态键路径——限**同质命名** tuple（元素同 CGType 且 ∈ {Int/Double/Bool/Str}、≥1 非空名，结果类型静态可定）：四守卫（空 tuple / 非 4 类元素 / 异质 / 无命名字段分别拒编不错编）后 emit(key) 校验 Str，rt_arr_new 物化 names 数组（kind 3 string，无名元素存空串）+ values 数组（elem kind，逐元素 elem_to_bits），新增 rt 接口 collie_rt_tuple_get(names, vals, key) 按非空名 strcmp 扫描——命中返对应 values 槽 i64 bits（bits_to_elem 还原元素类型）、未命中打 "Undefined tuple field '<key>'" + exit(1)（核心消息对齐解释器 RuntimeError，位置前缀缺失同 t83 越界陷阱既定分歧）；常量字符串键回归 t68 编译期解析路径不变；`const CGTuple t` 按值拷贝防 emit(key) 触发 register_tuple 扩容悬垂（与 t83 visitIndex 同一防护）；新差分用例 s37_tuple_get（四类同质命名 tuple 变量键/常量键回归/混合命名+无名/拼接键/toString 循环动态键/get 结果算术/函数内局部 tuple）+ 未命中键两端核心消息一致、四守卫拒编四实证，ctest -C Release 差分 36/36 逐字节一致，Debug 门禁 6/6（M6 t84）
- 2026-07-29 `feat(compiler)`: codegen 非常量 tuple 索引（t83，M6）：visitIndex Tup 分支加非常量索引路径——限**同质** tuple（所有元素同 CGType 且 ∈ {Int/Double/Bool/Str}，结果类型静态可定）：三重守卫（空 tuple / 元素非 4 类 / 异质分别拒编不错编）后 rt_arr_new + 逐元素 elem_to_bits/rt_arr_set 物化为运行时数组、rt_arr_get(动态 idx) 取 bits、bits_to_elem 还原元素类型，复用负索引归一化 + 越界陷阱（消息 "Index N out of range (size M)" 与解释器 normalize_index/collie_rt_arr_norm_index 完全一致），零新增 collie_rt 接口；常量索引路径内联不变；**关键修复** `const CGTuple& t` 改按值拷贝——非常量路径 emit(index) 可能触发 register_tuple 扩容 tuple_values_ 致引用悬垂（`t[idxs[0]]` 嵌套索引实测 0xC0000005，与 gen_tuple_eq 同一防护）；选型来自拒编面盘点，排除 Num 整态位运算/移位/`~`（语义层两端一致拦截，number 非 bit 类型无实测价值）；新差分用例 s36_tuple_index（integer/string/bool/decimal 同质变量/表达式/负索引变量、命名同质 tuple、字面量负索引回归常量路径、索引结果参与算术/比较、for 循环遍历、`t[idxs[0]]` 嵌套索引、函数内局部 tuple）+ 越界 `t[5]` 两端一致消息、异质 tuple 非常量索引 colliec 拒编 "non-constant index on heterogeneous tuple" 两实证，ctest -C Release 差分 35/35 逐字节一致，Debug 门禁 6/6（M6 t83）
- 2026-07-28 `feat(compiler)`: codegen 实例（Obj）相等比较（t82，M6）：两触点常量折叠解锁实例 `==`/`!=`/`==?`/switch——visitBinary 比较分支在 Arr 分支之后加 Obj 分支（任一侧 Obj 且 ==/!= 时 eq=false 常量、!= 取反，镜像 Tup 分支写法，替换既有拒编 "non-numeric operand of '=='"）、gen_match_eq 末尾拒编前加 Obj 分支（任一侧 Obj → false，覆盖 ==?/switch 两调用点，替换 "'==?' comparison of these value types"）；对齐解释器 values_equal 无 Instance 分支落 default 恒 false（含同一实例 `a==a` 也 false、引用别名亦 false），`gen_tuple_eq` 早有"任一 Obj 元素恒 false"先例，零新增 collie_rt 接口、零结构改动；关系比较 `<`/`<=`/`>`/`>=` 仍落 require_numeric 拒编；选型来自拒编面盘点（约 120 处 unsupported 归类，最小规模且有实测价值的活跃解锁面），排除 tribool 混型（语义层两端一致拦截 "Incomparable operand types"，无实测差分价值）；新差分用例 s35_object_eq（不同/同一实例 ==/!=、引用别名、比较结果进逻辑与 if/else、==? 全 miss/def、switch 全 default、无 default 静默跳过、函数内实例相等、复合表达式）+ 实例关系比较 `a < b` 两端一致拦截实证，ctest -C Release 差分 34/34 逐字节一致（s35 首跑即过），Debug 门禁 6/6（M6 t82）
- 2026-07-28 `feat(compiler)`: codegen none 值的 print/toString/==（t81，M6）：三触点解锁 CG2 缺口的 none 部分——gen_print Void case 打常量串 "none"（CreateGlobalString → rt_print_str，替换既有拒编 "print of 'none' value"，t77 两阶段收集中 Void 值 value=nullptr 无碍、副作用求值保序）、to_str Void case 返回 "none" 常量串（覆盖显式 toString 与插值脱糖 `"..."+toString(f())+"..."`，替换 "string conversion of this value"）、visitBinary 比较分支链首加 Void 分支（双 Void 恒 true 对齐解释器 values_equal None 分支、Void×非Void 恒 false 防御性双保险，!= 取反，替换 "non-numeric operand of '=='"）；零新增 collie_rt 接口；新差分用例 s34_none_value（print 单/多参混排副作用保序、toString/插值/s.length=4、==/!= 双 Void、bool 变量参与逻辑运算、if 条件、类方法体内 print）+ none 拼接 "v="+f() 语义层两端一致拦截 "Invalid operands for string concatenation" 实证，ctest -C Release 差分 33/33 逐字节一致（s34 首跑即过），Debug 门禁 6/6（M6 t81）
- 2026-07-28 `feat(compiler)`: codegen 小数取模（t80，M6）：visitBinary OP_MODULO 加 Double 分支（Num 路径之后、Int×Int 之前）——decimal 参与的 `%`（Double×Double / Int×Double / Double×Int）两侧 to_double 后 FRem（语义即 fmod 截断取余）+ floor 修正（r 非零且与除数异号时 r += b，FCmp ONE/OLT + select 无分支，仿 Int 路径既有写法），对齐解释器 eval_arithmetic；除零 FRem 天然 NaN 且 NaN 使 ONE 比较为 false 不触发修正（与解释器 b==0.0 提前返 NaN 殊途同归），-0.0 == 0.0 同解释器 r != 0.0 判定；零新增 collie_rt 接口；选型来自拒编面盘点（130+ 处 unsupported 归类，唯一"解释器支持、机制就绪、单触点"活跃解锁面）；新差分用例 s33_decimal_mod + true % 2.0 语义层两端一致拦截实证，ctest -C Release 差分 32/32 逐字节一致，Debug 门禁 6/6（M6 t80）
- 2026-07-28 `feat(compiler)`: codegen 数组相等比较（t79，M6）：新增 rt 接口 collie_rt_arr_eq(l, r) C 层深比较——先比 len 再逐元素按两侧运行时 kind（同 kind integer/bool i64 直比、decimal double 值比较（NaN != NaN 与解释器一致）、string strcmp；kind {0,1} 混合按 double 视图对齐解释器混合表示 `[1,2,3] == [1.0,2.0,3.0]` 为 true；bool/string 与其它 kind 配对恒不等；len==0 天然相等；运行时 kind 判定天然覆盖 t70 动态域数组），对齐解释器 values_equal Array 分支；codegen 三触点同时解锁并单点复用：visitBinary ==/!=（Arr×Arr 前置分支）、gen_tuple_eq Arr 元素（替换 t75 既有拒编 "tuple equality with array element"）、gen_match_eq Arr 候选（==?/switch 两调用点，Arr×非Arr 恒 false 双保险）；新差分用例 s32_array_eq + 数组关系比较语义层两端一致拦截实证，ctest -C Release 差分 31/31 逐字节一致，Debug 门禁 6/6（M6 t79）
- 2026-07-28 `feat(compiler)`: codegen ==?/switch 的 tuple 候选（t78，M6）：gen_match_eq 末尾 unsupported 前加 Tup 分支——双 Tup 走 gen_tuple_eq（t75 静态展开深比较单点复用：形状不一致编译期常量 false、逐元素四路降级 And 链、嵌套递归），Tup×非 Tup 恒 false（对齐解释器 values_equal kind 不等，实测语义层更早拦截 "Incomparable candidate value type"，codegen 为防御性双保险）；==?（级联比较块链）与 switch 两调用点同时解锁，含 Arr 元素 tuple 由 gen_tuple_eq 既有拒编覆盖，零新增 collie_rt 接口；新差分用例 s31_match_tuple + 含数组元素候选拒编/Tup×非Tup 两端语义层一致两实证，ctest -C Release 差分 30/30 逐字节一致，Debug 门禁 6/6（M6 t78）
- 2026-07-28 `fix(compiler)`: codegen 修复 CG8 print 求值序（t77，M6）：gen_print 两阶段化——第一循环 emit 全部实参按值收集（副作用调用按源序发生），第二循环统一打印 sep+值+换行，对齐解释器 call_builtin_print 先求值全部实参再打印；此前逐参求值边打边走，实参含副作用输出（函数/方法体内 print）时产物输出次序与解释器不同且静默错编（t76 发现登记 CG8）；打印阶段转串调用无输出副作用、Tup 注册表只追加下标跨阶段有效、零新增 collie_rt 接口；新差分用例 s30_print_order（单/多副作用实参、方法体副作用、多类型混排、运算嵌套、无副作用回归保护），s29 历史注更新、README 缺口表 CG8 标记已消除，ctest -C Release 差分 29/29 逐字节一致，Debug 门禁 6/6（M6 t77）
- 2026-07-28 `feat(compiler)`: codegen 顶层 tuple 全局化（t76，M6）：create_tuple_var 建槽从 create_entry_alloca 换 create_var_slot——顶层 tuple 逐元素解构槽升零初始化 GlobalVariable（collie.g. 前缀，t73 机制单点复用，初始值仍当前位置 store，嵌套子槽组递归天然覆盖，块内/函数内维持 alloca）；visitFunction/gen_method_body 链底拷贝取消 Tup 条目剔除（换整层拷贝），tuple_vars_ 注册表本为成员跨函数存活，lookup/load/store 零改动；函数/方法内读全局 tuple、同形状整体重赋值跨函数可见、局部同名遮蔽均对齐解释器；tuple 进签名/换形状重赋值既有拒编不变；期间发现并登记缺口 CG8（print 逐参求值边打边走，实参含副作用输出时次序与解释器不同）；新差分用例 s29_tuple_global + 换形状重赋值/tuple 形参两拒编实证，ctest -C Release 差分 28/28 逐字节一致，Debug 门禁 6/6（M6 t76）
- 2026-07-28 `feat(compiler)`: codegen tuple 相等比较（t75，M6）：visitBinary ==/!= 加 Tup×Tup 前置分支（先于 require_numeric），gen_tuple_eq 纯编译期静态展开递归深比较——长度或名字表不一致编译期常量 false（对齐解释器 values_equal 先比 size 再比 names），逐元素复用四路标量降级（Str→rt_strcmp、Tri/Bool→三态 icmp、Num/混型→rt_num_cmp op 0、Int/Double→icmp/fcmp）And 链合并，嵌套递归（注册表按值拷贝防扩容失效）；Obj 元素/异型标量配对恒 false 对齐解释器，含 Arr 元素拒编不错编（深比较恒 false 会错值），!= 整体取反；零新增 collie_rt 接口；新差分用例 s28_tuple_eq + 含数组元素拒编/Obj 元素两端恒 false 两实证，ctest -C Release 差分 27/27 逐字节一致，Debug 门禁 6/6（M6 t75）
- 2026-07-28 `feat(compiler)`: codegen number 作类字段（t74，M6）：复评推翻 t62「字段块 8 字节槽装不下 16 字节 tagged」拒编理由——StructType 按 llvm_type_of 逐字段拼装，Num 自动占位 {i64,i64}；改动两处：删 register_class_layout 拒编守卫 + visitNew malloc 上界改按字段类型累计（Num 16、其余 8，空字段类保底 8）；字段 GEP 读写/coerce_for_slot 加宽（Int/Double→to_num、Num 直通）全零新增，对齐解释器 coerce_to_declared KW_NUMBER；调研修正：tribool 字段实已随 S18 自然放行，实际拒编面仅 Num/Tup；新差分用例 s27_number_field + Tuple 字段/Num 读出赋 decimal 槽两拒编实证，ctest -C Release 差分 26/26 逐字节一致，Debug 门禁 6/6（M6 t74）
- 2026-07-28 `feat(compiler)`: codegen 顶层变量提升 LLVM 全局槽（t73，M6）：CGVar.slot 改型 Value*（全使用点仅 load/store 零风险），create_var_slot 统一建槽入口——顶层（!in_function_ && 深度 1）建零初始化 GlobalVariable（InternalLinkage + collie.g. 前缀），初始值仍在 @main 按源序 store，块内声明维持 alloca；visitFunction/gen_method_body 以顶层层拷贝为作用域链底（Tup 剔除，跨函数引用走既有 identifier 拒编），lookup_var 零改动，形参/局部天然遮蔽；顺序安全：语义层函数声明处分析 + 前向调用报错 ⇒ 零初始化值不可观察；新差分用例 s26_globals + 顶层 tuple 跨函数拒编实证，ctest -C Release 差分 25/25 逐字节一致，Debug 门禁 6/6（M6 t73）
- 2026-07-28 `feat(compiler)`: codegen 类实例作类字段（t72，M6）：CGField 加 cls 伴随，register_class_layout 加 IDENTIFIER 前置分支（类名须已注册声明在前，前向引用语义层更早拦截，classes_ 查询防御性双保险）；coerce_for_slot 加 slot_cls 参数，相等分支 Obj 严格同类校验（向上转型拒编不错编，一处覆盖字段赋值/字段初始化两入口，其余调用点 Obj 均有前置分支零回归）；visitProperty/visitPropertyAssign 字段读写带 cls，属性链/单态化方法调用/深链写/继承/跨签名全走 t61 既有 Obj 路径；新差分用例 s25_object_field + 两拒编实证，ctest -C Release 差分 24/24 逐字节一致，Debug 门禁 6/6（M6 t72）
- 2026-07-28 `feat(compiler)`: codegen 数组作类字段（t71，M6）：register_class_layout 放行 array 字段（字段槽即 opaque ptr，struct 建型/malloc 上界零改动）；CGField 无元素类型伴随，字段读出即动态域——visitProperty/visitPropertyAssign 置 elem=Num 哨兵，下游索引读写/print/传参/返回全走 t70 动态路径零新 rt 接口；写入守卫下沉 coerce_for_slot 相等分支（右值 elem 限 {Int,Double,Num}，bool/str 数组拒编，一处覆盖字段赋值/字段初始化两入口，变量/tuple 槽另有前置分支零回归）维持动态域 kind ∈ {0,1}；Num/Obj 字段、嵌套/异质数组维持拒编；新差分用例 s24_array_field + 两拒编实证，ctest -C Release 差分 23/23 逐字节一致，Debug 门禁 6/6（M6 t71）

- 2026-07-28 `feat(compiler)`: codegen 数组进函数签名（t70，M6）：array 形参/返回值放行（顶层函数 + 类方法），elem 动态化为 Num 哨兵——collie_rt 数组 kind 0/1 与 Num tag 编码重合，动态域索引读 rt_arr_get bits + 新接口 rt_arr_kind 直接拼 Num 零转换，写下沉新接口 rt_arr_set_num（tag==kind 直存/int→double 提升/decimal 写 int 数组陷阱，新缺口 CG7）；不变量：进动态域数组 elem 限 {Int,Double,Num}（bool/str 数组作实参/返回值拒编）保 kind ∈ {0,1}；数组赋值规则扩展（Num 槽 ← 数值系；静态槽 ← Num 拒编）；数组类字段/嵌套/异质维持拒编；新差分用例 s23_array_signature + CG7 陷阱与两拒编实证，ctest -C Release 差分 22/22 逐字节一致，Debug 门禁 6/6（M6 t70）

- 2026-07-28 `feat(compiler)`: codegen char/byte/word + 位运算（t69，M6）：char/character 承载 CGType::Str（字面量 GlobalString + declared_cgtype 映射，打印/比较/拼接零新触点）；byte/word 承载 i64 零类型扩散（CGVar.bit_max，声明/赋值点 check_bit_range 范围陷阱，表达式域无截断加宽 integer，类字段/函数签名维持拒编）；位运算双侧限 Int：& | ^ → and/or/xor、<< >> 移位量 0-63 检查后 shl/ashr、~ → xor -1；collie_rt 新增 trap_bit_range/trap_shift_count 两陷阱（文案对齐解释器）；新差分用例 s22_bits + 三陷阱手动实证，ctest -C Release 差分 21/21 逐字节一致，Debug 门禁 6/6（M6 t69）
- 2026-07-28 `feat(compiler)`: codegen tuple 静态展开（t68，M6）：CGType::Tup 虚值（元素 CGValue 向量 + 名字表登记注册表，无运行时对象）；变量解构逐元素 alloca 槽（嵌套递归，重赋值同形状逐槽写）；t[常量 i]（AST 层 const_int_of 含负索引归一化）/t.name/t.get("字面量键")/length（优先于同名字段）全部编译期解析；print/toString/插值经 tuple_to_str 静态展开（常量段编译期合并 + rt_concat 链，嵌套递归）零新增 collie_rt 接口；动态索引/动态键/函数签名/进数组/相等比较/三元与 ==? 分支产 tuple 拒编维持；最后一个未降级类型收口；新差分用例 s21_tuple，ctest -C Release 差分 20/20 逐字节一致，Debug 门禁 6/6（M6 t68）
- 2026-07-28 `feat(compiler)`: codegen number 专属方法（t67，M6）：gen_number_method 三路降级 10 个方法（abs/integerPart/decimalPart → 同型数值，7 个 is* 谓词 → bool）——Int 纯 IR（abs 走 checked ssub 陷阱，INT64_MIN 拒错编从陷阱；谓词常量折叠）、Double 走 fabs/trunc/floor intrinsic + fcmp（isNaN uno 自反、isFinite/isInfinity |a| 与 inf 有序比较、isInteger/isDecimal finite AND floor 判等）、Num tag 分支两路 PHI（整数态保持整数态）；零新增 collie_rt 接口；方法调用最后一块标量拒编面收口；新差分用例 s20_number_methods，ctest -C Release 差分 19/19 逐字节一致，Debug 门禁 6/6（M6 t67）
- 2026-07-26 `feat(compiler)`: codegen switch 语句（t66，M6）：visitSwitch 级联比较块链降级（gen_multi_match 同构语句版，无结果 PHI）——条件求值一次、候选惰性求值首命中即执行 body 后跳 end（无 fallthrough），default 位置无关最后兜底，候选比较复用 gen_match_eq（Int/Double/Bool/Str/Num/Tri 含混型提升）零新增 collie_rt 接口；body 内 break/continue 绑定外层循环，含终结器不补 br；object/数组/元组候选拒编维持；新差分用例 s19_switch，ctest -C Release 差分 18/18 逐字节一致，Debug 门禁 6/6（M6 t66）
- 2026-07-26 `feat(compiler)`: codegen tribool 三态布尔（t65，M6）：CGType 新增 Tri → LLVM i8（False=0 < Unset=1 < True=2，沿用解释器编码）；to_tri 统一 bool→tribool 加宽（赋值/传参/返回值/签名三处一致）；gen_logical 重构统一 i8 域（短路条件 AND 左==0 / OR 左==2，右支 umin/umax intrinsic，纯 bool 收窄回 i1）；`!t` → 2-t；三态判等 icmp i8；gen_ternary 重写 Arm 向量式（两分支 Tri 条件 ==2 判真 unset 走 false、三分支三路 CondBr）；isTrue/isFalse/isUnset icmp 出 i1；print/toString 双 select 三常量串零新增 collie_rt 接口；==? tribool 穷尽省默认链尾 unreachable；数组/元组/object 动态路径拒编维持；新差分用例 s18_tribool，ctest -C Release 差分 17/17 逐字节一致，Debug 门禁 6/6（M6 t65）
- 2026-07-26 `feat(compiler)`: codegen `==?` 多路匹配（t64，M6）：级联比较块链降级 gen_multi_match（目标求值一次，命中跳分支结果块/未中顺延下一候选，链末端即默认块，天然对齐解释器首命中 + 惰性求值）；相等比较复用 == 四路降级出 i1（gen_match_eq：Str×Str strcmp==0、任一 Num 走 num_cmp op 0、Bool×Bool icmp、Int/Double 含混型提升）零新增 collie_rt 接口；结果混型统一沿用 gen_ternary 规则扩展到 N+1 支 + merge 块 PHI；无默认分支/tribool/object/数组元组候选拒编；新差分用例 s17_multimatch，ctest -C Release 差分 16/16 逐字节一致，Debug 门禁 6/6（M6 t64）
- 2026-07-26 `feat(compiler)`: codegen toNumber 内建（t63，M6，收口 t62 范围外遗留）：collie_rt 新增 collie_rt_str_to_num（复刻解释器 to_number_value 的 string 分支：剥空白/严格 Infinity 三形式/纯整数串精确整数表示/strtod 等价解析，失败返 NaN 不报错，超 i64 整数串 ERANGE 走 CG1 陷阱）；codegen 新增 to_number_num 降级（bool → 0/1 整数表示、integer/decimal/number 纯 IR 内联转 Num、string 下沉运行时），visitCall 内建分发与 visitMethodCall 方法形式共用，none/array/tuple/实例参数拒编维持；新差分用例 s16_tonumber，ctest -C Release 差分 15/15 逐字节一致，Debug 门禁 6/6（M6 t63）
- 2026-07-26 `feat(compiler)`: codegen number 双表示（t62，M6，CG5 收窄）：collie_rt 新增 number 运行时四接口（collie_rt_num_arith/num_cmp/num_to_str/print_num，标量参数 + out 指针规避 16 字节 struct ABI）；codegen CGType 新增 Num——{i64 tag, i64 bits} first-class struct 单 SSA 值流转（tag 0=整数 i64 直存/1=小数 double bitcast），算术/比较/转串/打印下沉运行时单点对齐解释器（双整数精确运算 + floor 取模 + i64 溢出陷阱、/ 恒 double、混合走 double、除零 IEEE 754、打印格式对齐 Value::to_string），Int/Double→Num 三处加宽保持原表示，三元分支混 Num 统一 Num，number 类字段/数组元素/窄化维持拒编；新差分用例 s15_number，ctest -C Release 差分 14/14 逐字节一致，Debug 门禁 6/6（M6 t62）
- 2026-07-26 `feat(compiler)`: codegen class 二期（t61，M6）：继承布局父链字段 base-first 合并；方法按分派类单态化生成 collie.C.D.m（CGClass super/dispatch/instances + CGMethod defining，register_class 拆布局/方法两遍，generate 第一遍三阶段），模板方法动态分派与解释器等价；: base(...) 委托/base.method() 按定义类父链静态解析；实例作函数参数/返回值（签名类名→Obj+cls 严格同类）；语义层函数签名类名→object 动态放行 + 修复 visitFunction must-return 异常路径作用域泄漏；新差分用例 s14_inherit，ctest -C Release 差分 13/13 逐字节一致，Debug 门禁 6/6（M6 t61）
- 2026-07-26 `feat(compiler)`: codegen class 最小闭环（t60，M6）：collie_rt 新增 collie_rt_obj_new（malloc+memset 零初始化）；codegen CGType 新增 Obj，CGValue/CGVar 增设 cls 字段，每类一个 StructType（字段按声明顺序布局），方法/构造器降级 collie.类名.方法名 独立函数 + this 隐藏首参，支持 new 三段顺序/字段读写/方法调用含 this 互调/toString 兜底/print 实例 "<object>"/引用语义/三元，继承、无初值字段、实例作函数参数/返回值等拒编；新差分用例 s13_class，ctest -C Release 差分 12/12 逐字节一致，Debug 门禁 6/6（M6 t60）
- 2026-07-26 `feat(compiler)`: codegen array 最小闭环（t59，M6）：collie_rt 新增数组运行时（collie_rt_arr_new/get/set/len/to_str，单块 malloc 对象头部 len+kind + 8 字节槽位模式，负索引归一化+越界报错退出，[1, 2, 3] 格式对齐 Value::to_string）；codegen CGType 新增 Arr，CGValue/CGVar 增设 elem 字段做字面量同质推断（Int/Double 混合提升 Double，异质/嵌套拒编），支持同质字面量/索引读写/length+len 内建/print+toString/引用语义赋值/三元，array 函数参数/返回值拒编；新差分用例 s12_array，ctest -C Release 差分 11/11 逐字节一致，越界索引手工验证通过，Debug 门禁 6/6（M6 t59）
- 2026-07-26 `feat(compiler)`: codegen CG1 整数溢出陷阱（t58，M6）：i64 加/减/乘/一元负号换 llvm.s{add,sub,mul}.with.overflow intrinsic（checked_int_arith helper，每检查点独立 trap/cont 块），溢出调 collie_rt_trap_int_overflow 报错退出，静默回绕改显式运行期报错；INT64_MIN % -1 硬件陷阱边缘 select 安全除数（结果 0 对齐解释器 floor_mod）；新差分用例 s11_int_edge（边界大数/负号/取模边缘/复合赋值），ctest -C Release 差分 10/10 逐字节一致，溢出 trap 手工验证通过（stderr 报错+非零退出码），Debug 门禁 6/6（M6 t58）
- 2026-07-26 `feat(compiler)`: codegen string 方法降级（t57，M6）：collie_rt 新增 collie_rt_str_trim（mode 0=两端/1=左/2=右，只剥空格与 Tab）+ collie_rt_str_substring（UTF-8 码点区间 [start,end)，end==-1 取 length，越界 clamp）；codegen visitMethodCall 接入 Str 的 trim 系列/subString（参数限 Int，缺 end 传 -1）与任意标量的 toString() 方法形式（复用 to_str），toNumber 等维持拒编；新差分用例 s10_string_methods（含中文码点/链式调用），ctest -C Release 差分 9/9 逐字节一致，Debug 门禁 6/6（M6 t57）
- 2026-07-26 `feat(compiler)`: codegen string length 属性 + 索引 s[i] UTF-8 码点降级（t56，M6）：collie_rt 新增 collie_rt_str_len（码点计数，照抄解释器 utf8_length 首字节步进）+ collie_rt_str_index（负索引归一化，越界报错退出，返 malloc 单码点子串）；codegen visitProperty 支持 Str.length（→Int）、visitIndex 支持 Str×Int（→Str），其余维持拒编；新差分用例 s9_string_index（含中文多字节码点），ctest -C Release 差分 8/8 逐字节一致，Debug 门禁 6/6（M6 t56）
- 2026-07-26 `feat(compiler)`: codegen string 六种比较运算 strcmp 降级（t55，M6）：collie_rt 新增 collie_rt_strcmp（strcmp 语义，逐字节字典序与解释器 std::string 比较一致）单接口；codegen visitBinary 比较 case 新增 Str×Str 分支（call 后与 0 做对应 icmp EQ/NE/SLT/SLE/SGT/SGE），混型维持拒编；SPEC.md §4.4 补 string 关系比较（逐字节字典序）规范条目；新差分用例 s8_string_compare，ctest -C Release 差分 7/7 逐字节一致，Debug 门禁 6/6（M6 t55）
- 2026-07-26 `feat(compiler)`: codegen string 运行时第一步——拼接 + toString 内建 + 插值路径（t54，M6）：collie_rt 新增 collie_rt_concat（malloc 拼接）/i64_to_str/f64_to_str（与 print_f64 共享四步格式化 helper collie_rt_format_f64）/bool_to_str（静态串）4 接口；codegen visitBinary OP_PLUS 任一侧 Str 走 rt_concat（非 Str 侧经新 to_str 转串，对齐解释器任一侧 string 即拼接语义）；visitCall 新增内建 toString（单参转串，分发先于用户函数查表）→ 字符串插值 @"{expr}"（parser 脱糖为 toString 拼接链）自然打通；新登记缺口 CG6（拼接串 malloc 不 free，短生命周期编译产物暂容忍）；新差分用例 s7_string_concat，ctest -C Release 差分 6/6 逐字节一致，Debug 门禁 6/6（M6 t54）
- 2026-07-26 `feat(compiler)`: collie_rt 运行时垫片第一版，print 输出格式对齐解释器（t53，M6）：纯 C 静态库 `codegen/runtime/collie_rt.c`（print_str/i64/f64/bool + 参间 sep + 末尾 newline 逐参接口；纯 C 免 clang 链 .ll 时的 C++ 标准库依赖）；f64 移植解释器 to_string 四步格式（NaN→±Infinity→整值<1e15 按整数打，修复 %g 把 3000000 打成 3e+06→其余 %g）；gen_print 改逐参调用垫片不再直连 printf/puts；colliec 运行期从自身目录定位 collie_rt.lib（CMake 宏烘焙绝对路径在非 ASCII 构建树下编码错乱，改 GetModuleFileName 方案；windows.h 需 NOMINMAX 免污染 LLVM 头）；新差分用例 s6_print_format，ctest -C Release 差分 5/5 逐字节一致，Debug 门禁 6/6（M6 t53）
- 2026-07-26 `feat(compiler)`: codegen 扩展 S5 函数 + 修复 parser 参数类型 token 缺口（t52，M6）：顶层 `function name(param type, ...) retType` 声明/调用/`return`/递归；两遍处理（generate() 第一遍 declare_function 建全部原型→递归/前向调用天然可用，符号名 `collie.<name>`+InternalLinkage；第二遍 visitFunction 现场保存/恢复后生成函数体）；CGType 新增 Void（none 返回降 void）；形参 entry alloca+store，实参/返回值仅 integer→decimal 提升；visitReturn 落 ret.dead 块；尾块收尾（void 补 RetVoid/不可达补 unreachable/可达无 return 拒编，可达性用 entry 起 DFS 判定）；同名重载/嵌套函数拒编；修复 parser consume_type_token 类型关键字列表缺口（缺 KW_INTEGER/KW_DECIMAL/KW_TRIBOOL/KW_DWORD/KW_BIT，影响函数参数/返回类型与类字段）+ 防退化测试；新增差分用例 s5_functions.collie，ctest -C Release 差分 4/4 逐字节一致，Debug 门禁 6/6（M6 t52）
- 2026-07-26 `feat(compiler)`: codegen 扩展 S4 循环控制流 + 修复 parser for 初始化类型缺口（t51，M6）：LoopContext 栈支撑 break/continue（break→end、continue→while/do-while 的 cond、for 的 inc，与解释器对齐；CreateBr 后落 `*.dead` 块保每块单终结符）；for 初始化限自身作用域 + cond/body/inc/end 四块；do-while 先 br body 保至少执行一次；gen_ternary（bool 条件 CondBr + PHI 汇合，int/double 混型在各分支块内提升 double，三分支 tribool 拒编）；修复 parser parse_for_statement 初始化类型列表历史缺口（缺 integer/decimal/tribool 等，对齐 parse_declaration，解释器/编译器共同受益）+ 防退化测试；新增差分用例 s4_loops.collie，ctest -C Release 差分 3/3 逐字节一致，Debug 门禁 6/6（M6 t51）
- 2026-07-26 `feat(compiler)`: codegen 扩展 S3 + 差分测试自动化进 ctest（t50，M6）：`code_generator.{h,cpp}` 新增 S3 降级——变量声明（integer/decimal/bool/string，entry 块头 alloca 利于 mem2reg，须带初始化否则拒编，`number` 变量拒编登记缺口 CG5）、赋值（仅 integer→decimal sitofp 提升）、比较 `== != < <= > >=`（纯整数 icmp/含小数 fcmp，`!=` 用 UNE 保 NaN，bool 仅 ==/!=）、逻辑 `&& || !`（短路 condbr+phi，与解释器 Kleene 对齐）、if/else、while、块作用域遮蔽（scopes_ 栈）、bool 字面量 KW_TRUE/KW_FALSE；差分测试自动化：`codegen/tests/run_diff_test.cmake` 四步比对脚本（colliec 编译产物 vs collie 解释器逐字节）+ `tests/CMakeLists.txt` 注册 codegen_diff_{s1_hello,s3_control_flow}（if COLLIE_ENABLE_LLVM + CONFIGURATIONS Release）；踩坑：EXCLUDE_FROM_ALL 子目录 add_test 不进 CTestTestfile（注册挪 tests/）、`codegen` 保留目标名 CMP0171（改名 collie_codegen）；ctest -C Release 差分 2/2 逐字节一致，Debug 全量 ctest 6/6 不受影响（M6 t50）
- 2026-07-26 `feat(compiler)`: CodeGenerator 第一版 + colliec 本地编译驱动（t49，M6）：Release 全工程切 /MT 静态 CRT（t49a，Debug 保持 /MDd，门禁 ctest 6/6 不受影响）；`codegen/code_generator.{h,cpp}` 实现 S1/S2 降级（print→printf 编译期格式串、`/` 恒小数 fdiv、`%` floor 取模 srem+select 校正、一元负号；范围外节点统一抛 CodeGenError 绝不静默错编；verifyModule 门禁）；`colliec_main.cpp` 驱动：前端三层门禁→写 .ll→调 LLVM 包 clang 编链本地 .exe（`--emit-llvm`/`-o` 选项，COLLIE_LLVM_BIN 编译期烘焙）；t49_hello.collie 端到端跑通，编译产物与 Debug 解释器输出 fc 逐字节一致（首个解释器/编译产物差分验证）（M6 t49）
- 2026-07-26 `docs(compiler)`: AST → LLVM IR 降级设计文档 `compiler/codegen/README.md`（t48c）：阶段范围 S1/S2/S3；类型映射（integer→i64 妥协登记 CG1、decimal→double）；降级映射表（print→puts/printf、`/` 恒小数 fdiv、`%` floor 取模 select 校正）；CRT 链接方案拍板（Release 全工程切 /MT，Debug 门禁不动，t49 实施）；验证策略：verifyModule 门禁 + 解释器/编译产物差分测试；CG1～CG4 缺口登记（M6 t48）
- 2026-07-26 `build(compiler)`: LLVM 22.1.8 官方预编译包接入 CMake + 冒烟验证（t48a/t48b，M6 启动）：顶层 `COLLIE_ENABLE_LLVM` 选项（默认 OFF）+ `find_package(LLVM CONFIG)` + codegen 子目录（EXCLUDE_FROM_ALL）；`llvm_smoke` 工具 IRBuilder 构造 hello world 模块/verifyModule/打印 IR 全链路通过；CRT 对齐两坑：官方包为 /MT 静态 CRT（CMP0091 NEW + 目标级 MSVC_RUNTIME_LIBRARY）、`LLVMConfig.cmake` 污染顶层 `CMAKE_MSVC_RUNTIME_LIBRARY`（find_package 前后保存/恢复）；LLVM 下载步骤写入贡献文档 compile-and-run（英文 + 中文 i18n）；回归全量 ctest 6/6（M6 t48）
- 2026-07-26 `feat(compiler)`: char/byte 类型 + 位运算符 `~ & | ^ << >>` + 十六进制字面量（t47，补齐 SPEC.md 缺口 G1）：lexer `0x`/`0X` hex 字面量；parser 新增 C 家族位运算优先级层（`|`<`^`<`&`<相等<关系<移位<加法）、`~` 接入 unary、char 字面量接入 primary；语义 `is_bit_type` 纳入 integer、`integer → byte/word` 静态放行；解释器 `~` BigInt 精确、二元位运算 int64 域（超范围/移位数越界报错）、coerce_to_declared 新增 byte 0-255/word 0-65535 范围校验；解锁 3 个禁用语义测试 + 新增 8 个测试；全量 ctest 6/6（M5 t47）
- 2026-07-26 `docs(compiler)`: 新增 `compiler/SPEC.md` 语言规范草稿（t46，M5）：以实际实现为准逐条经代码核实（词法/字面量/类型系统与转换规则/运算符优先级与语义/语句/内建函数方法表/class 文法/执行模型与门禁/输出格式），「已知实现缺口」专节登记 G1–G10（位运算未解析、char/byte 类型不完整、形参/返回类型缺口、无重载分发、访问修饰符不强制等）；风险表清理两条过时条目（double 单表示已由 t42 解决、文档占位风险降级）（M5 t46）
- 2026-07-26 `feat(compiler)`: tuple 最小闭环（t45，经作者确认访问语法 `t[0]`/`t.name`/`t.get("key")`）：词法注册 `Tuple` 关键字；parser 命名元组字面量（`IDENTIFIER :` 前瞻，首元素带名无逗号也是元组）+ `Tuple` 声明/返回类型，删除 `.0` 死代码（parse_postfix/parse_tuple_type/parse_type/TupleMemberExpr）；语义层 KW_TUPLE 接入索引/属性/get/length 并拒绝索引赋值；解释器 Value 新增不可变 Tuple（元素+平行名字表）含负索引/相等深比较/coerce；新增 15 个端到端测试；全量 ctest 6/6；同步修正 uncategorized.md 两处矛盾示例并补充中文 03-tuple.md 成员访问节（M4 t45）
- 2026-07-26 `feat(compiler)`: `==?` 通用多路匹配运算符（t44，经作者确认语义与消歧义规则）：AST 新增 MultiMatchExpr；parser 在 parse_ternary 层解析（末尾裸表达式 = 默认分支、其余裸值与后面最近「值: 结果」归组）；语义层候选值可比性/tribool 穷尽三态/非 tribool 必须默认分支/结果类型兼容检查；解释器按序匹配首命中 + 惰性求值；同步修正 uncategorized.md 示例 5（默认在前写法废弃）；新增 8 个端到端测试；全量 ctest 6/6（M4 t44）
- 2026-07-26 `feat(compiler)`: tribool 三态布尔类型闭环（t43，经作者确认语义）：`unset` 字面量 + Value 层 Tri 三态（编码使 Kleene AND/OR 退化为 min/max）；`! && ||` Kleene 三值逻辑（保留短路，混合运算结果加宽为 tribool）；条件语句必须 bool（语义拦截 + 运行期 condition_truthy 防御）；bool → tribool 单向加宽；`==`/`!=` 三态比较；内建方法 isTrue/isFalse/isUnset；三分支三元 `a ? x : y : z`（需 tribool 条件，两分支时 unset 走 false 分支）；新增 11 个端到端测试；全量 ctest 6/6（M4 t43）
- 2026-07-26 `feat(compiler)`: 数字类型 number/integer/decimal 三类型区分（t42，经作者确认 Python 式设计）：解释器新增 BigInt（base 2^32，任意精度自动扩容）与 Value 双表示；lexer f 后缀计入 lexeme；语义层字面量推导/转换规则（integer→decimal 加宽、decimal→integer 拒绝、number 超类型）与 `/` 恒产 decimal；解释器双整数 `+ - * %` 精确路径、比较/相等精确、coerce/toNumber/len/数字方法整数路径；新增 11 个端到端测试；interpreter_tests 153 全绿（M4 t42）
- 2026-07-26 `feat(compiler)`: @override 注解（t40）：lexer 新增 ANNOTATION token（@名字，与 @" 插值共存）；parser 类成员注解解析（@override 仅限方法，@deprecated 接受不生效，未知注解报错）；语义层类表升级为 map 并新增 find_method_in_hierarchy，@override 校验父类链确有同名方法；新增 7 个端到端测试；interpreter_tests 142 全绿（M4 t40）
- 2026-07-26 `feat(compiler)`: base.method() 显式父类方法调用（t39）：AST 新增 BaseMethodCallExpr，parser 在 primary 层解析（base 非一等值，必须紧跟方法调用）；解释器从 current_class_ 的父类链查找绕过覆写，以 defining_class 上下文执行；语义层类外报错；新增 7 个端到端测试；interpreter_tests 135 全绿（M4 t39）
- 2026-07-26 `feat(compiler)`: class 继承（t38）：`extends` 单继承 + 构造器委托 `: base(args)`（脱糖为体首语句）；字段/方法沿继承链查找与覆写，字段初始化 base-first，current_class_ 上下文使 base 按定义类父类解析；语义层父类已声明/非自身检查；新增 8 个端到端测试；interpreter_tests 128 全绿（M4 t38）
- 2026-07-26 `feat(interpreter)`: 函数返回值类型运行期校验（t37）：visitCall 与 call_class_method 捕获 ReturnSignal 时返回值经 coerce_to_declared（显式 return 路径；none/void 返回类型自然放行）；新增 3 个端到端测试；interpreter_tests 120 全绿，运行期类型校验链（声明/赋值/形参/字段/返回值）闭环（M4 t37）

- 2026-07-26 `feat(interpreter),fix(parser)`: 类字段类型运行期校验（t36，闭环 t35 遗留项）：新增 find_field 辅助，visitNew 字段初始化与 visitPropertyAssign 字段赋值接入 coerce_to_declared；修复 parser consume_type_token 缺 object/array 致形参类型不能写这两个关键字；新增 4 个端到端测试；interpreter_tests 117 全绿（M4 t36）

- 2026-07-26 `feat(interpreter),fix(semantic)`: 运行期声明类型校验与隐式转换（t35）：Environment 记录声明类型，新增 coerce_to_declared（number/bool/array 严格匹配、string ← number/bool 转字符串、object/类名动态放行），接入变量声明/赋值/函数形参/类方法形参四处；修复语义层 can_implicit_convert 缺 object 动态放行；新增 7 个端到端测试；interpreter_tests 113 全绿（M4 t35）

- 2026-07-26 `feat(lexer,parser,semantic,interpreter)`: 实现 class 基础支持（Java/C# 风格最小子集，t34）：注册 new/this 关键字；AST 新增 PropertyAssignExpr/NewExpr/ThisExpr 并补 ClassStmt::accept；parser 解析 class 声明（字段/方法/构造器同名识别/访问修饰符）、new/this、属性赋值重组；语义层类名登记 + object 动态放行（this 类外报错、未定义类报错、类名作变量类型转 object）；解释器 Value 新增 Instance（shared_ptr 引用语义）、字段初始化、构造器/方法调用绑定 this；新增 8 个端到端测试；interpreter_tests 106 全绿（M4/M5 t34）

- 2026-07-25 `feat(interpreter)`: 除零改为 IEEE 754 语义（经作者确认，闭环 t31 遗留）：`1/0 → +Infinity`、`-1/0 → -Infinity`、`0/0 → NaN`，取模除数为 0 返回 NaN，不再报运行时错误；新增端到端测试；interpreter_tests 98 全绿（M4 t33）

- 2026-07-25 `feat(lexer,parser)`: 实现字符串插值 `@"{expr}"`（文档语法为 @ 前缀非 ${}，见 03-character.md）：词法层新增 LITERAL_INTERPOLATED_STRING（lexeme 保留原文）；parser 脱糖为 `"a" + toString(x) + "b"`（AST/semantic/interpreter 零改动）；支持 `\{` `\}` 字面花括号与插值段内字符串字面量；新增 6 个测试，lexer_tests 15 / interpreter_tests 97 全绿（M4 t32）

- 2026-07-25 `feat(lexer,interpreter)`: 实现 Infinity/NaN 特殊数值字面量（见 04-numeric.md）：词法层归为 LITERAL_NUMBER（大小写敏感）；toString 输出 +Infinity/-Infinity/NaN；toNumber 特殊形式严格匹配、不可解析返回 NaN（不再报错）；新增 6 个测试，lexer_tests 14 / interpreter_tests 92 全绿（M4 t31）

- 2026-07-25 `test(semantic)`: 恢复错误恢复类 DISABLED_ 语义测试 12 个（semantic_recovery_test.cpp）：错误方向反转 + function 语法改写 + 遮蔽合法化适配；实测并注释级联计数（ComplexExpression 4、RecursiveFunction 6、ErrorRecoveryPriority 2）；仍禁用 ComplexTypeConversionRecovery/MemoryUsageRecovery；semantic_tests 45 通过 / 13 禁用（M4 t30）

- 2026-07-25 `test(semantic)`: 恢复错误收集类 DISABLED_ 语义测试：ErrorRecovery/ContinueAfterError/ErrorLocation（利用 string→number 非法方向构造错误，number→string 隐式转换实为合法）、FunctionErrors（改写为 function 语法，getString 级联 2 错共 4 条）；ErrorLocation 行号修正并改 ASSERT_EQ 防越界；semantic_tests 33 通过 / 25 禁用（M4 t29）

- 2026-07-25 `feat(parser,semantic,interpreter)`: 实现 string/array 内建成员：AST 新增 PropertyExpr、parser 支持无括号属性访问；`length` 属性（string 码点数/array 元素数）；string 方法 trim/trimLeft/trimRight 与首个带参方法 subString(start[, end])（码点区间，end 缺省/-1/NaN 取 length）；新增 7 个端到端测试；interpreter_tests 87 全绿（M4 t28）

- 2026-07-25 `feat(parser,semantic,interpreter)`: 实现方法调用语法 `expr.method(args)`：AST 新增 MethodCallExpr、parser 后缀链支持索引与方法调用混合、语义层内建方法表（toString/toNumber 通用 + number 专属 abs/integerPart/decimalPart/is* 系列）、解释器分发实现并抽出 to_number_value 与内建函数共用；新增 6 个端到端测试；interpreter_tests 80 全绿（M4 t27）

- 2026-07-25 `feat(interpreter,lexer)`: number 语义对齐设计规范：取模改为 floor 语义（Python 风格，`-1 % 5 == 4`，修复与 04-numeric.md 不符的 fmod 截断行为）；lexer 支持前导点小数 `.5` 与 `f` 后缀 `2f`（后缀不计入 lexeme）；int64/double 双表示推迟（当前无可观测差异）；新增 3 个端到端 + 2 个 lexer 测试；interpreter_tests 74 / lexer_tests 13 全绿（M4 t26）

- 2026-07-25 `test(semantic),fix(utils)`: 恢复数组相关 DISABLED_ 语义测试：ArrayTypes（array 关键字语法 + has_errors 新 API，同质性检查拆待办）与 ArrayOperationRecovery（实测 3 错含级联赋值错误）；修复 token_utils 缺失 KW_ARRAY 字符串映射；semantic_tests 29 通过 / 29 禁用（M4 t25）

- 2026-07-25 `feat(semantic,interpreter)`: 实现字符串索引：语义层允许 string 被索引（结果 string）且拒绝字符串索引赋值（不可变）；解释器抽出 utf8_length/utf8_char_at 辅助（len 复用），按 UTF-8 码点索引返回单字符子串，复用 normalize_index 支持负索引/越界报错；新增 7 个端到端测试；interpreter_tests 71 全绿（M4 t24）

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
