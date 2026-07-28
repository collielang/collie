# Collie 编译器 · 开发进度与路线（Living Document）

> 这是一份**持续更新**的工程进度文档，记录当前状态、关键决策、阶段计划、风险与待确认的语言设计问题。
>
> **更新约定**：每完成或修复一块工作，就在对应里程碑打勾，并在文末「变更日志」追加一条（与 git 提交一一对应）。

最后更新：2026-07-26（t55 完成：codegen string 六种比较运算 strcmp 降级，差分 7/7；SPEC 补 string 字典序条目）

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
