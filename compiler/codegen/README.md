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
| S5 | 顶层函数声明/调用/`return`/递归 | 函数程序编译执行，输出与解释器一致 **✅ t52** |
| S6 | collie_rt 垫片：print 输出格式对齐解释器 to_string | decimal 四步格式/±Infinity/NaN/混合行输出与解释器一致 **✅ t53** |
| S7 | string 运行时第一步：拼接 `+`、`toString` 内建、插值路径 | 拼接/插值程序编译执行，输出与解释器一致 **✅ t54** |
| 后续 | string 方法/索引、class、BigInt | 逐任务扩展 |

不在第一期范围：tribool/Kleene、tuple、array、class、`==?`、异常语义。
CodeGenVisitor 遇到不支持的节点**显式报错**（"codegen: not yet supported: XXX"），绝不静默错编。

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
| `string` | `ptr`（指向常量串或 collie_rt malloc 串） | 字面量 = `private unnamed_addr constant [N x i8]`；拼接结果 = `collie_rt_concat` malloc 串（不 free，缺口 CG6） |
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

**S5 降级补充（t52 实现）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 顶层函数声明 | 两遍处理：第一遍扫顶层建全部原型（递归/前向调用天然可用），第二遍生成函数体；符号名 `collie.<name>` + InternalLinkage（用户标识符无 '.'，不与 main/printf 冲突）；同名重载拒编；嵌套函数拒编 |
| 参数/返回类型 | 限 integer/decimal/bool/string；`none` 返回降 void（CGType::Void）；实参/返回值仅 integer→decimal 隐式提升（与解释器 coerce 一致） |
| 函数体生成 | 现场保存/恢复（插入点/作用域栈/循环栈）；形参 entry 块 alloca+store 落栈槽；函数内仅形参+局部可见，引用顶层变量拒编（顶层变量住 @main 栈槽） |
| `return` | 求值 + coerce 后 CreateRet；void 函数仅裸 return；后续指令落 `ret.dead` 块 |
| 尾块收尾 | void 补 RetVoid（对齐解释器返 none）；非 void 不可达尾块补 unreachable；可达无 return 拒编；可达性用 entry 起 DFS 判定（dead 块被外层控制流补 br 后单看前驱会误判） |

**S6 降级补充（t53 实现）：collie_rt 运行时垫片**：

| 要点 | 说明 |
|------|------|
| 垫片库 | `runtime/collie_rt.c` 纯 C 静态库（clang 编链 .ll 默认只带 C 运行时，纯 C 免 C++ 标准库依赖）；Release /MT 与 clang 默认静态 CRT 对齐 |
| print 降级 | 从“编译期拼 printf 格式串”改为**逐参调用垫片**：`collie_rt_print_str/i64/f64/bool` + 参间 `print_sep`（空格）+ 末尾 `print_newline` |
| f64 四步格式 | 移植解释器 `Value::to_string`：①NaN→`NaN`；②±Inf→`+Infinity`/`-Infinity`；③整值且 |v|<1e15 按整数打（修复 `%g` 把 3000000 打成 3e+06）；④其余 `%g`（6 位有效，与 ostringstream defaultfloat 一致） |
| 链接定位 | colliec **运行期**从自身目录定位 `collie_rt.lib`（两目标同目录产出）；不用 CMake 烘焙绝对路径——构建树路径含非 ASCII 时宏值经编译器命令行会编码错乱 |

print 现已不直连 printf/puts；后续 string 方法/数组/none 格式随 collie_rt 扩展。

**S7 降级补充（t54 实现）：string 拼接与转串**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `a + b`（任一侧 string） | 非 string 侧先 `to_str` 转串，再 `call ptr @collie_rt_concat(ptr, ptr)`（malloc 出新串，不 free，缺口 CG6）；与解释器“任一侧 string 即拼接、另一侧隐式转串”对齐 |
| `toString(x)` 内建 | 按类型分发：Str 原样；Int → `collie_rt_i64_to_str`；Double → `collie_rt_f64_to_str`（与 print_f64 共享四步格式化，两路径输出一致）；Bool → zext i1→i32 后 `collie_rt_bool_to_str`（返静态串）；分发先于用户函数查表，与解释器一致 |
| 字符串插值 `@"a{x}b"` | 无 codegen 专门逻辑：parser 已脱糖为 `"a" + toString(x) + "b"` 左结合拼接链，拼接 + toString 两条路径即自然打通 |

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
| CG2 | print 标量格式已对齐解释器（t53 collie_rt 垫片）；数组/none/tuple 等复合值格式仍缺 | 随对应类型的 codegen 支持扩展 collie_rt 接口 |
| CG3 | 运行期类型校验（coerce_to_declared 五处）在编译产物中缺失 | 语义层静态保证覆盖的部分可省；动态部分（object/窄化）随 collie_rt 补 |
| CG4 | 仅支持 x86_64-pc-windows-msvc target | CI 矩阵起来后加 Linux target；LLVM 包已含全部 target 后端 |
| CG6 | 拼接/转串结果 malloc 后不 free，编译产物存在内存泄漏 | 短生命周期进程暂容忍；后续随 string 运行时成熟引入引用计数或 arena 分配器 |

## 八、构建方式速查

```bash
# 配置（一次）：
cmake -S compiler -B compiler/build -DCOLLIE_ENABLE_LLVM=ON ^
      -DLLVM_DIR=D:/Program/Development/Environment/llvm-21/lib/cmake/llvm
# 冒烟构建 + 运行：
compiler\build\t48_smoke_build.cmd
compiler\build\codegen\Release\llvm_smoke.exe
```
