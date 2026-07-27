# codegen · AST → LLVM IR 降级设计（M6 · t48c）

> Living Document：随每个 tNN 任务的实现同步更新。当前状态 = **设计稿**，
> 以 `compiler/SPEC.md`（实际实现规范）为语义依据，不以设计愿景文档为准。

## 一、目标与范围

**总目标（M6）**：`collie` 源码 → LLVM IR → 本地二进制，跑通 helloworld 编译产物。

**本文档锁定的第一期范围（最小子集）**：

| 阶段 | 支持面 | 验收 |
|------|--------|------|
| S1 | `print("字符串字面量")` 顶层语句 | helloworld.collie 编译为 .exe，输出与解释器一致 **✅ t49** |
| S2 | 整数字面量、`+ - * / %`、`print(整数表达式)` | 算术表达式编译执行，输出与解释器一致 **✅ t49** |
| S3 | 变量声明/读写、bool、比较、`if`/`while` | 循环程序编译执行，输出与解释器一致 **✅ t50** |
| S4 | `for`/`do-while`、`break`/`continue`、二分支三元 `a ? x : y` | 循环控制流程序编译执行，输出与解释器一致 **✅ t51** |
| 后续 | 函数、string 运行时、decimal 输出格式、class、BigInt | 逐任务扩展 |

不在第一期范围：tribool/Kleene、tuple、array、class、字符串插值（parser 已脱糖为拼接，
依赖 string 运行时）、`==?`、异常语义。CodeGenVisitor 遇到不支持的节点**显式报错**
（"codegen: not yet supported: XXX"），绝不静默错编。

## 二、总体架构

```
Lexer → Parser → SemanticAnalyzer → CodeGenVisitor → llvm::Module
                                         │                │ verifyModule
                                         │                ├─ .ll 文本（S1 先行，llvm_smoke 同款路径）
                                         │                └─ TargetMachine → .obj → lld/link → .exe（S1 后半）
```

- `CodeGenVisitor` 实现现有 `ASTVisitor` 接口（与 `SemanticAnalyzer`/`Interpreter` 同构），
  树遍历生成 IR；表达式结果通过成员 `llvm::Value* last_value_` 传递（与解释器的
  返回值传递模式一致）。
- 顶层语句收拢进 `define i32 @main()`（与解释器"脚本式执行"语义对齐），正常结束 `ret i32 0`。
- 模块归属：`compiler/codegen/`，静态库 + 驱动可执行文件，依赖链
  `utils ← lexer ← parser ← semantic ← codegen`（仅头文件 + 静态库交互）。

## 三、类型映射表（第一期）

| Collie 类型 | LLVM 类型 | 备注 |
|------------|-----------|------|
| `integer` | `i64` | **妥协点**：SPEC §3.2 规定 integer 为任意精度 BigInt；第一期降为 i64，溢出行为未定义（缺口 CG1，后续接 BigInt 运行时库） |
| `number`（整数表示） | `i64` | 同上 |
| `decimal` / `number`（小数表示） | `double` | IEEE 754，与解释器一致 |
| `bool` | `i1` | |
| `string` 字面量 | `ptr`（指向 `private unnamed_addr constant [N x i8]`） | 第一期只支持字面量常量，无拼接/方法 |
| `none` / `void` | `void` | |
| 其余（tribool/tuple/array/class/char...） | 不支持，显式报错 | |

## 四、降级映射表（S1/S2 核心）

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `print(字符串字面量)` | `call i32 @puts(ptr @str)`（puts 自带换行，与 print 的行尾换行一致） |
| `print(整数表达式)` | `call i32 @printf(ptr @"%lld\n", i64 %v)` |
| 整数字面量 `42` / `0xFF` | `i64` 常量（超出 i64 范围在 codegen 期报错，缺口 CG1） |
| `a + b` / `a - b` / `a * b`（整数） | `add` / `sub` / `mul` `i64`（无 nsw：溢出回绕暂容忍，登记缺口 CG1） |
| `a / b` | **恒小数除法**（SPEC §4）：`sitofp` 两侧 → `fdiv double`；除零自然得 ±Inf/NaN（IEEE 754，t33 语义） |
| `a % b`（整数） | **floor 取模**（SPEC §4，Python 风格）：`r = srem a, b`；`if (r != 0 && (r < 0) != (b < 0)) r += b`（select 实现，无分支） |
| 一元 `-a` | `sub i64 0, %a`（整数）/ `fneg`（小数） |
| 顶层语句序列 | 依序生成进 `@main` entry 起始的基本块链 |

**S3 降级补充（t50 实现）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 变量声明 `integer/decimal/bool/string x = init` | entry 块头部 `alloca`（利于 mem2reg）+ `store`；无初始化拒编（解释器绑 none 无静态对应）；`number` 变量拒编（双表示需运行时标记，缺口 CG5） |
| 读变量 / 赋值 | `load` / `store`；仅 integer→decimal 槽隐式提升（`sitofp`，与语义层一致） |
| 块作用域遮蔽 | `scopes_` 作用域栈（vector<unordered_map>），逆向查找 |
| 比较 `== != < <= > >=` | 纯整数 `icmp eq/ne/slt/sle/sgt/sge`；含小数一侧统一 `sitofp` 后 `fcmp oeq/une/olt/ole/ogt/oge`（`!=` 用 UNE 保 NaN 语义）；bool 仅 `==`/`!=` |
| `&&` / `\|\|` | **短路**（与解释器 Kleene 实现对齐）：`condbr` + merge 块 `phi i1` 双入边 |
| `!a` | `xor i1 %a, true`（CreateNot），仅 bool |
| `if`/`else` | then/else/merge 基本块 + 终结符防御（已有 terminator 不补 br） |
| `while` | cond/body/end 基本块，回边 br cond |

**S4 降级补充（t51 实现）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `for` | 初始化限自身作用域（scopes_ push/pop）；cond/body/inc/end 基本块，body → inc → cond 回边；无条件恒真、无增量则回边直指 cond |
| `do-while` | body/cond/end 基本块，先无条件 br body（至少执行一次），cond 判真回 body |
| `break`/`continue` | loop 上下文栈 `loops_` 记录目标块：break → end；continue → while/do-while 的 cond、for 的 inc（与解释器 continue 后仍执行增量一致）；CreateBr 后落入 `*.dead` 死代码块（IR 每块仅一个终结符） |
| 三元 `a ? x : y` | bool 条件 CondBr + then/else/merge 块，merge 处 PHI 汇合；分支 int/double 混型时在各自分支块内提升 double；三分支 tribool 形式报不支持 |

**print 后续演进**：S2 之后 print 需要匹配解释器的 `to_string` 全部格式
（decimal 6 位有效数字、整值小数按整数打印、`+Infinity`/`NaN`、bool/none/数组格式），
届时引入 **C++ 运行时垫片库 `collie_rt`**（静态库，直接复用/移植解释器 `to_string`
逻辑），print 统一降级为 `call void @collie_print(...)`，不再直连 printf。

## 五、构建与链接方案（关键决策）

**CRT 对齐**（t48b 已验证的事实）：LLVM 官方预编译包为 Release + **/MT 静态 CRT**；
`LLVMConfig.cmake` 会改写 `CMAKE_MSVC_RUNTIME_LIBRARY`（顶层已做保存/恢复）。

**驱动程序 `colliec`**（编译器 driver，区别于解释执行的 `collie`）要链接
codegen + 前端四库，而前端库当前 Release 配置为 /MD —— 直接混链会 LNK2038。拍板：

> **工程级 CRT 规则调整（t49 已实施）**：Debug 配置保持 `/MDd`（既有测试门禁、CI 完全不动）；
> **Release 配置全工程切到 `/MT`**（与 LLVM 对齐，顺带让发布产物自包含、免装 VC 运行时）。
> 即顶层 `CMAKE_MSVC_RUNTIME_LIBRARY` 改为 `MultiThreaded$<$<CONFIG:Debug>:DebugDLL>`。
> 测试仅在 Debug 配置跑（ctest -C Debug），不受影响；gtest 跟随同规则。

**产物链路**（t49 第一版）：`colliec` 驱动跑前端门禁 → CodeGenerator 生成 IR → 写 `.ll` 落盘
（verifyModule 门禁）→ 调 LLVM 包自带 `clang` 把 `.ll` 直接编链为 `.exe`（`--emit-llvm`
可只停在 `.ll`）。后续再接 `TargetMachine::addPassesToEmitFile` 直出 `.obj` + `lld-link`，
免道 clang 驱动。

## 六、验证策略

1. **verifyModule 门禁**：每个模块生成后必过 `llvm::verifyModule`，失败即报错退出。
2. **差分测试（核心手段）**：同一 `.collie` 源，解释器执行 stdout 与编译产物执行
   stdout **逐字节一致**。复用现有 interpreter_tests 的用例源码，逐步纳入。
3. **IR 快照测试**：关键构造的 `.ll` 输出做字符串断言（进 gtest，Release 配置单独
   test target，不进现有 Debug 门禁，避免混 CRT）。

## 七、已知缺口（codegen 专用，编号 CG*）

| 编号 | 缺口 | 计划 |
|------|------|------|
| CG1 | integer 降为 i64，非任意精度；溢出回绕不报错 | BigInt 运行时库（collie_rt），或先加 `llvm.sadd.with.overflow` 陷阱 |
| CG2 | print 仅覆盖 string 字面量/整数，格式未对齐解释器 to_string 全集 | collie_rt 垫片统一接管 |
| CG3 | 运行期类型校验（coerce_to_declared 五处）在编译产物中缺失 | 语义层静态保证覆盖的部分可省；动态部分（object/窄化）随 collie_rt 补 |
| CG4 | 仅支持 x86_64-pc-windows-msvc target | CI 矩阵起来后加 Linux target；LLVM 包已含全部 target 后端 |

## 八、构建方式速查

```bash
# 配置（一次）：
cmake -S compiler -B compiler/build -DCOLLIE_ENABLE_LLVM=ON ^
      -DLLVM_DIR=D:/Program/Development/Environment/llvm-21/lib/cmake/llvm
# 冒烟构建 + 运行：
compiler\build\t48_smoke_build.cmd
compiler\build\codegen\Release\llvm_smoke.exe
```
