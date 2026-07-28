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
| S8 | string 六种比较运算（strcmp 降级） | 字典序/相等比较程序编译执行，输出与解释器一致 **✅ t55** |
| S9 | string length 属性 + 索引 `s[i]`（UTF-8 码点） | 多字节码点 length/正负索引程序编译执行，输出与解释器一致 **✅ t56** |
| S10 | string 方法 trim 系列/subString + toString 方法形式 | 方法/链式调用程序编译执行，输出与解释器一致 **✅ t57** |
| S11 | 整数溢出陷阱（CG1 收窄）：i64 加/减/乘/负号溢出显式报错 | 边界内大数程序输出与解释器一致，溢出程序陷阱退出 **✅ t58** |
| S12 | array 最小闭环：同质字面量/索引读写/length/print/引用语义 | 数组程序编译执行，输出与解释器一致，越界陷阱退出 **✅ t59** |
| S13 | class 最小闭环：单类/字段/构造器/方法/this/引用语义 | 类程序编译执行，输出与解释器一致 **✅ t60** |
| S14 | class 二期：继承/覆写/base/模板方法/实例作函数参数返回值 | 继承程序编译执行，输出与解释器一致 **✅ t61** |
| S15 | number tagged 双表示（CG5 收窄）：算术/比较/转串下沉 collie_rt | number 程序编译执行，整数/小数两态输出与解释器一致 **✅ t62** |
| S16 | toNumber 内建（函数/方法形式）：string 解析下沉 collie_rt | 解析/失败 NaN/透传加宽程序编译执行，输出与解释器一致 **✅ t63** |
| S17 | `==?` 多路匹配：级联比较块链 + PHI（首命中 + 惰性求值） | 多路匹配程序编译执行，输出与解释器一致 **✅ t64** |
| S18 | tribool 三态布尔：i8 三态编码 + Kleene min/max + 三分支三元 | 三态逻辑/短路副作用/穷尽匹配程序编译执行，输出与解释器一致 **✅ t65** |
| S19 | switch 语句：级联比较块链（首命中 + 无 fallthrough，default 位置无关） | switch 程序编译执行，输出与解释器一致 **✅ t66** |
| S20 | number 专属方法：abs/integerPart/decimalPart + 7 个 is* 谓词（三路降级） | 三类数值接收者方法程序编译执行，输出与解释器一致 **✅ t67** |
| S21 | tuple 静态展开：字面量/常量索引/命名字段/get/length/print（无运行时对象） | tuple 程序编译执行，输出与解释器一致 **✅ t68** |
| S22 | char/byte/word + 位运算：char 走 Str、byte/word i64+赋值点范围陷阱、& \| ^ ~ << >>（移位 0-63 检查） | 位运算/位类型程序编译执行，输出与解释器一致 **✅ t69** |
| S23 | 数组进函数签名：参数/返回值放行（顶层函数 + 类方法），elem 动态化为 Num 哨兵（运行时 kind 驱动） | 排序/累加/跨边界引用语义程序编译执行，输出与解释器一致 **✅ t70** |
| S24 | 数组作类字段：字段声明/初始值/读写放行，字段读出即动态域（elem 恒 Num 哨兵，t70 机制全套复用） | 字段数组初始值/索引读写/引用语义/跨签名边界程序编译执行，输出与解释器一致 **✅ t71** |
| 后续 | BigInt 运行时化 | 逐任务扩展 |

不在第一期范围：异常语义（tuple 已于 S21 t68 以静态展开解锁，动态索引/动态键/进函数签名/进数组/相等比较仍拒编）。
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
| `integer` | `i64` | **妥协点**：SPEC §3.2 规定 integer 为任意精度 BigInt；第一期降为 i64，溢出时显式陷阱报错退出（t58 收窄，不静默回绕；任意精度对齐仍属缺口 CG1，后续接 BigInt 运行时库） |
| `decimal` | `double` | IEEE 754，与解释器一致 |
| `number` | `{i64 tag, i64 bits}` first-class struct | tagged 双表示（t62，CG5 收窄）：tag 0=整数（bits 即 i64）/1=小数（bits 为 double bitcast 位模式）；单 SSA 值流转（alloca 槽/函数签名/PHI 直用该 struct），仅 collie_rt 边界拆散标量 + out 指针 |
| `bool` | `i1` | |
| `string` | `ptr`（指向常量串或 collie_rt malloc 串） | 字面量 = `private unnamed_addr constant [N x i8]`；拼接结果 = `collie_rt_concat` malloc 串（不 free，缺口 CG6） |
| `array` | `ptr`（指向 collie_rt 数组对象） | **妥协点**：解释器数组元素动态异质；codegen 限同质数组（元素类型由字面量推断，另记于 CGValue/CGVar 的 elem 字段），异质/嵌套拒编；指针拷贝即引用语义（对齐解释器 shared_ptr） |
| 类实例（`Point p = new Point()`） | `ptr`（指向 malloc 零初始化块，按 `collie.class.<类名>` StructType GEP 访问） | 每类一个 StructType（字段按声明顺序布局，继承时父链字段 base-first 合并）；类名另记于 CGValue/CGVar 的 cls 字段；指针拷贝即引用语义；实例可作函数参数/返回值（签名处类名，严格同类）；不可进数组/元组（拒编） |
| `none` / `void` | `void` | |
| 其余（tribool/tuple/char...） | 不支持，显式报错 | |

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
| 变量声明 `integer/decimal/bool/string x = init` | entry 块头部 `alloca`（利于 mem2reg）+ `store`；无初始化拒编（解释器绑 none 无静态对应）；`number` 变量已于 S15（t62）解锁 |
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

**S8 降级补充（t55 实现）：string 六种比较**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `a == b` / `!=` / `<` / `<=` / `>` / `>=`（双侧 string） | `call i32 @collie_rt_strcmp(ptr, ptr)` 后与 0 做对应 icmp（EQ/NE/SLT/SLE/SGT/SGE）；逐字节字典序与解释器 std::string 比较一致，无 UTF-8 特殊处理 |
| Str × 非 Str 混型比较 | 维持 require_numeric 拒编（解释器运行期同样报错，无合法程序受影响） |

**S9 降级补充（t56 实现）：string length + 索引**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `s.length`（string） | `call i64 @collie_rt_str_len(ptr)`：UTF-8 首字节步进计码点数（照抄解释器 utf8_length），结果为 Int |
| `s[i]`（string × Int） | `call ptr @collie_rt_str_index(ptr, i64)`：负索引归一化（-1 为最后码点），越界 stderr 报错后 exit(1)，返 malloc 单码点子串（CG6 同样不 free），结果为 Str |
| 非 string 接收者 / 非 Int 索引 | 拒编（array/tuple 待对应类型 codegen 支持；Double 索引解释器运行期也报错） |

**S10 降级补充（t57 实现）：string 方法**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `s.trim()` / `trimLeft()` / `trimRight()` | `call ptr @collie_rt_str_trim(ptr, i32 mode)`（mode 0=两端/1=左/2=右）：只剥空格与 Tab（对齐解释器 is_blank），返 malloc 新串（CG6 不 free） |
| `s.subString(start[, end])` | `call ptr @collie_rt_str_substring(ptr, i64, i64)`：UTF-8 码点区间 [start,end)，缺省 end 传 -1 运行时取 length，越界 clamp、start>=end 得空串；参数限 Int（Double/NaN 特例拒编，解释器 NaN 特判属 Double 域） |
| `x.toString()`（任意标量接收者） | 复用 `to_str` 降级（与内建 `toString(x)` 同一路径），结果为 Str |
| `toNumber()` / number/tuple 方法 | toNumber() 已于 S16（t63）解锁；tribool 方法（isTrue/isFalse/isUnset）已于 S18（t65）解锁；number 专属方法（abs/integerPart 等）已于 S20（t67）解锁；tuple.get("字面量键")已于 S21（t68）静态解析 |

**S12 降级补充（t59 实现）：array 最小闭环**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 数组字面量 `[a, b, c]` | 逐元素求值后同质推断（Int/Double 混合整体提升 Double，提升后输出仍与解释器一致；其余混合/嵌套拒编；空数组 elem 记 Int）→ `call ptr @collie_rt_arr_new(i64 len, i64 kind)` 单块 malloc 数组对象（kind：0=Int/1=Double/2=Bool/3=Str）→ 逐元素 `elem_to_bits` 转 8 字节位模式后 `collie_rt_arr_set` 写槽 |
| `a[i]` 读 / `a[i] = v` 写 | `collie_rt_arr_get/set(ptr, i64[, i64 bits])`：负索引归一化（-1 为最后一个元素），越界 stderr 报错后 exit(1)（消息格式同 str_index）；读结果 `bits_to_elem` 按 elem 还原；写入仅允许 Int→Double 提升否则拒编；求值顺序 object→index→value 对齐解释器 |
| `a.length` / `len(a)` | `call i64 @collie_rt_arr_len(ptr)`（len 内建同时支持 string 走 str_len） |
| `print(a)` / `toString(a)` / 拼接 | `call ptr @collie_rt_arr_to_str(ptr)` 整体转 `[1, 2, 3]` 格式串（对齐 Value::to_string：元素递归格式化、字符串不加引号）后走 print_str/Str 路径 |
| 赋值/三元中的数组 | 指针拷贝即引用语义；两侧 elem 不一致拒编（解释器动态异质无此限制，同质表示无法承载 → 拒编不错编） |
| array 函数参数/返回值 | 拒编（`array` 声明无元素类型标注，跨函数签名无法定 elem；待带元素类型的声明语法或动态 kind 方案） |

**S13 降级补充（t60 实现）：class 最小闭环**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class C { ... }` 声明 | 注册遍（与函数原型同属第一遍）建 `collie.class.C` StructType：字段按声明顺序布局（下标即 GEP 索引）；方法/构造器降为 `collie.C.<方法名>` InternalLinkage 独立函数，this 作隐藏首参 ptr；第二遍生成方法体（现场保存/还原同 visitFunction，this 直持 SSA 值不落栈槽） |
| `new C(args)` | `call ptr @collie_rt_obj_new(i64 size)`（malloc + memset 零初始化，size = 8×字段数上界，与 DataLayout 解耦）→ 字段初始值按声明顺序求值写入（仅 Int→Double 隐式提升，同 coerce 四处）→ 构造器实参求值 → 构造器调用（与解释器 visitNew 三段顺序一致）；无构造器带实参拒编 |
| `obj.field` 读 / `obj.field = v` 写 | `CreateStructGEP` + load/store；写入仅允许 Int→Double 提升否则拒编；求值顺序 object→value 对齐解释器 |
| `obj.m(args)` / `this.m(args)` | 类方法表优先命中 → `call @collie.C.m(ptr this, args...)`；未命中且 `toString()` 无参 → 固定串 `"<object>"` 兜底（分派顺序对齐解释器）；否则拒编 |
| `print(obj)` / `toString(obj)` | 固定输出 `"<object>"`（对齐 Value::to_string Instance 分支） |
| 赋值/三元中的实例 | 指针拷贝即引用语义；两侧类名不一致拒编 |
| 范围外拒编 | 无初值字段（解释器落 none 无静态表示）、number/tribool/tuple/array 字段、实例相等比较、实例进数组/元组、`object` 声明类型、方法重载（extends/base/实例作参数返回值已于 S14 t61 解锁） |

**S14 降级补充（t61 实现）：class 二期——继承/base/实例作函数参数返回值**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class D extends B` 布局 | 字段 = 父链 base-first 合并 + 自身追加（GEP 索引前缀不变）；父类须声明在前，同名字段遮蔽拒编 |
| 方法单态化 | 对每个类 C 沿链每个 (定义类 D, 方法 m) 生成独立函数 `collie.C.D.m`（分派上下文 = C、base 解析上下文 = D，与解释器 `call_class_method(instance, method, defining_class)` 同构）；类内分派表 dispatch 存覆写解析后的映射，体内 `this.m()` 按 C 的分派表解析——模板方法模式（父类方法调子类覆写）与解释器动态分派等价（向上转型拒编保证静态 cls 即动态类） |
| `: base(args)` 构造器委托 | 按定义类的父类解析（current_defining_class_），调父类构造器在当前分派类下的单态化实例；父类无构造器时 0 实参为空操作（对齐解释器 visitBaseCall） |
| `base.m(args)` | 从定义类的父类起静态查首个定义者（绕过子类覆写，C# 语义），调该定义者在当前分派类下的单态化实例 |
| 实例作函数参数/返回值 | 签名处 IDENTIFIER 类名 → ptr + cls；实参/return 处 cls 严格相等否则拒编（语义层同步支持类名签名→object 动态放行，t61） |
| `@override` | 纯语义层校验（t40），codegen 忽略注解位 |
| 范围外拒编（继承相关） | 向上转型（声明/赋值/三元/实参/返回均严格同类）、子类同名字段遮蔽、父类声明晚于子类、构造器/方法重载 |

**S15 降级补充（t62 实现）：number tagged 双表示（CG5 收窄）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `number` 值表示 | `{i64 tag, i64 bits}` first-class literal struct：tag 0=整数（bits 即 i64）/1=小数（bits 为 double bitcast 位模式）；变量单 alloca 槽、函数签名/PHI 直用该 struct（LLVM 自动处理 ABI 降级），仅 collie_rt 边界 extractvalue 拆散标量传参 + out 指针写回（规避 MSVC x64 16 字节 struct 传参隐藏指针 ABI 错配） |
| 算术 `+ - * / %` / 一元 `-`（任一侧 Num） | `call void @collie_rt_num_arith(i64 op, i64 atag, i64 abits, i64 btag, i64 bbits, ptr otag, ptr obits)`（op：0=+ 1=- 2=* 3=/ 4=% 5=一元负号）：双整数精确 + - * 与 floor 取模（i64 溢出复用 CG1 陷阱报错退出）、`/` 恒 double、混合走 double、除零 IEEE 754；另一侧 Int/Double 先 to_num 加宽 |
| 比较 `== != < <= > >=`（任一侧 Num） | `call i32 @collie_rt_num_cmp(i64 op, i64 atag, i64 abits, i64 btag, i64 bbits)`（op：0..5 对应 == != < <= > >=）后与 0 做 icmp ne 返 i1：双整数精确、混合 double 视图（5 == 5.0 为 true）、NaN 语义对齐解释器 |
| `print(n)` / `toString(n)` | `collie_rt_print_num(i64 tag, i64 bits)` / `collie_rt_num_to_str(i64, i64)`：整数态按整数打、小数态走 f64 四步格式（与 print_f64 共享，对齐 Value::to_string） |
| integer/decimal → number 加宽 | 声明/赋值/实参/return 四路径 to_num：保持原表示打 tag（对齐解释器 coerce_to_declared 的 KW_NUMBER 分支）；反向 number→integer/decimal 窄化拒编（静态无法判定 tag） |
| 三元分支混型 | 任一分支 Num → 两分支块尾统一 to_num，merge 处 PHI 用 struct 类型 |
| 范围外拒编 | number 类字段/数组元素（维持 S12/S13）、任意精度自动扩容（BigInt 运行时化留远期，超 i64 整数运算陷阱退出）（toNumber 内建已于 S16 t63 解锁） |

**S16 降级补充（t63 实现）：toNumber 内建**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `toNumber(s)`（string）/ `s.toNumber()` | `call void @collie_rt_str_to_num(ptr s, ptr otag, ptr obits)`（出参写回同 num_arith 的 ABI 规避）：复刻解释器 to_number_value 的 string 分支——剥两端空白 → 严格大小写 `Infinity`/`+Infinity`/`-Infinity` → 纯整数串（可带单个 +/- 前缀）精确整数表示 → strtod 等价 std::stod（须整串消费且结果有限，`"1.5f"` 残留/`"infinity"` 宽松拼写均失败）→ 一切失败返 NaN 不报错；超 i64 纯整数串 strtoll ERANGE 走 CG1 陷阱（解释器 BigInt 精确，不静默错编） |
| `toNumber(b)`（bool）/ `b.toNumber()` | 纯 IR：`zext i1 → i64` 后打 tag 0（整数表示 0/1，对齐解释器） |
| `toNumber(x)`（integer/decimal/number） | 纯 IR：复用 to_num 加宽（保持原表示打 tag）/ number 透传 |
| 内建与方法形式 | visitCall（分发插在 len 之后、用户函数查表之前）与 visitMethodCall 共用 to_number_num 降级；结果恒为 Num |
| 范围外拒编 | none/array/tuple/实例参数（解释器此处为运行期报错 "toNumber() cannot convert ..."） |

**S17 降级补充（t64 实现）：`==?` 多路匹配**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `target ==? v1, v2: r1, v3: r2, default` | 级联比较块链：目标只求值一次，按分支序/候选序生成「比较→命中跳分支结果块/未中顺延下一候选」，链末端即默认块；块链天然对齐解释器首命中 + 惰性求值（未命中分支的候选/结果不求值）；各分支结果块尾对齐统一类型后跳 merge 块，N+1 入口 PHI 收拢 |
| 候选相等比较 | 复用 == 四路降级出 i1（gen_match_eq）：Str×Str `collie_rt_strcmp`==0、任一 Num 走 `collie_rt_num_cmp` op 0（双整数精确/混合 double 视图）、Bool×Bool icmp、Int/Double icmp/fcmp 含混型提升（5 == 5.0，对齐解释器 values_equal）；零新增 collie_rt 接口 |
| 结果混型统一 | 沿用 gen_ternary 规则扩展到 N+1 支：同型直用（含 Arr elem/Obj cls 一致性校验）；数值混型任一 Num 统一 Num 否则 Double |
| 范围外拒编 | 无默认分支形式（tribool 穷尽三态省默认已于 S18 t65 解锁，其余目标类型仍要求默认分支）；object 动态比较；数组/元组深比较候选 |

**S18 降级补充（t65 实现）：tribool 三态布尔**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| tribool 值表示 | `CGType::Tri` → `i8`，三态编码 False=0 < Unset=1 < True=2（沿用解释器编码，Kleene AND/OR 退化 min/max）；`unset` 字面量 → `i8 1` |
| bool→tribool 加宽 | `to_tri`：Tri 透传 / Bool `select i1, 2, 0`；赋值（coerce_for_slot）/传参（coerce_call_arg）/返回值（visitReturn）/函数签名（declared_cgtype KW_TRIBOOL）四处一致 |
| `&&` / `\|\|`（任一侧 tribool） | gen_logical 统一 i8 域：短路条件「AND 左==0 / OR 左==2」CondBr，右支 `llvm.umin`/`llvm.umax` intrinsic 合并，merge PHI i8；纯 bool 输入 `icmp eq 2` 收窄回 i1（短路边静态 widen 与解释器输出等价，差分实证）；惰性求值语义保持（false/true 短路不求右侧，unset 不短路） |
| `!t` | `sub i8 2, t`（true↔false，unset 不变） |
| `==` / `!=` 三态判等 | 双方限 Tri/Bool，统一 to_tri 后 `icmp eq/ne i8` 出 Bool（gen_match_eq 同路径） |
| 两分支三元 Tri 条件 | `icmp eq 2` 判真（unset 走 false 分支，对齐解释器）；分支混 Tri/Bool 统一 Tri |
| 三分支三元 `c ? a : b : u` | 条件限 Tri：entry `icmp eq 2` → then/rest，rest 块 `icmp eq 0` → else/unset，merge 三入口 PHI |
| `isTrue()` / `isFalse()` / `isUnset()` | 接收者限 Tri，`icmp eq i8 2/0/1` 出 Bool |
| print / `toString` | 双 select 三常量串（is_true ? "true" : (is_false ? "false" : "unset")），零新增 collie_rt 接口 |
| `==?` tribool 目标穷尽省默认 | 默认检查移至 target 求值后（仅 Tri 目标可省），无默认链尾 `unreachable`（i8 值域严格 {0,1,2} + 语义层保证穷尽） |
| 范围外拒编 | tribool 进数组元素/元组；object 动态路径三态；`if/while/for/do-while` 条件 tribool（语义层已拦） |

**S19 降级补充（t66 实现）：switch 语句**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `switch (expr) { v1, v2 { … } default { … } }` | 级联比较块链（gen_multi_match 的语句版，无结果 PHI）：条件只求值一次，按 case 序/候选序生成「比较→命中跳 switch.body/未中顺延 switch.next」，候选惰性求值、首命中即停（对齐解释器 values_equal 匹配即 return） |
| 命中后控制流 | body（BlockStmt 自带作用域）执行完跳 switch.end，**无 fallthrough**；body 尾已含终结器（return/break/continue）时不补 br |
| default 分支 | 位置无关最后兜底：非 default 分支优先比较，链尾跳 switch.default（无 default 或其 body 为空则直接跳 switch.end） |
| 候选相等比较 | 复用 gen_match_eq（Str×Str strcmp==0、任一 Num 走 num_cmp op 0、Bool/Tri icmp、Int/Double 含混型提升），零新增 collie_rt 接口 |
| body 内 break/continue | 维持绑定外层循环（解释器 switch 不捕获 BreakSignal，loop 栈不动） |
| 范围外拒编 | object 动态比较目标/候选；数组/元组深比较候选（同 `==?` 拒编面，gen_match_eq 内自然拒编） |

**S20 降级补充（t67 实现）：number 专属方法**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| Int 接收者（i64） | 纯 IR：`abs` = checked `ssub(0,n)` + `select(n<0)`（INT64_MIN 取负走溢出陷阱对齐 CG1——解释器 BigInt 可精确表示、i64 不可，拒错编从陷阱）；`integerPart` 恒等、`decimalPart` 恒 0；`isInteger`/`isFinite` 恒真、`isDecimal`/`isNaN`/`isInfinity` 恒假（常量折叠）；`isPositive`/`isNegative` → `icmp sgt/slt 0` |
| Double 接收者（double） | intrinsic + fcmp：`abs` → `llvm.fabs`；`integerPart` → `llvm.trunc`（向零取整）；`decimalPart` → `fsub(a, trunc(a))`（保留符号）；`isNaN` → `fcmp uno a,a`（自反）；`isFinite`/`isInfinity` → `fcmp olt/oeq(fabs(a), +inf)`（NaN 天然 false）；`isInteger`/`isDecimal` → `and(finite, fcmp oeq/one(a, floor(a)))`；`isPositive`/`isNegative` → `fcmp ogt/olt 0`（NaN 均 false，对齐解释器） |
| Num 接收者（{i64,i64}） | `icmp eq(tag,0)` 分支 nummeth.int / nummeth.dbl 两路各走上述降级，nummeth.merge PHI 合并；数值结果重新 `make_num` 打 tag（整数态保持整数态，对齐解释器 BigInt/double 双路分发） |
| 接口面 | 零新增 collie_rt 接口（仅复用 `collie_rt_trap_int_overflow`）；10 个方法均 0 参（语义层已校验，codegen 防御拒编） |

**S21 降级补充（t68 实现）：tuple 静态展开**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 元组字面量 `(1, name: v)` | 无运行时对象：逐元素求值后连同名字表登记 `tuple_values_` 注册表，表达式值为 CGType::Tup 虚值（llvm value 恒 nullptr，`tup` 存注册表下标）；元素类型/个数/名字编译期全可知（语义层零追踪，codegen 自建） |
| `Tuple t = …` 声明 / 重赋值 | 解构为逐元素独立 alloca 槽（槽名 `t.0`/`t.1`，嵌套元组递归子条目，形状入 `tuple_vars_`，取自初始值）；变量读逐槽 load 重组新虚值；重赋值要求同形状（元素数 + 名字表一致）逐槽写，否则拒编 |
| `t[常量 i]` | 编译期解析成对应元素：`const_int_of` AST 层模式匹配（整数字面量或一元负号包字面量，避开 emit 的溢出检查产非常量指令）；负索引归一化对齐解释器 normalize_index；越界/动态索引拒编 |
| `t.length` / `t.name` / `t.get("键")` | length 常量折叠 `i64 n`（优先于同名字段，对齐解释器分支顺序）；命名字段线性扫名字表（不排除空名）、get 限字符串字面量键（排除空名）——两处匹配规则分别对齐解释器 visitProperty/get；未命中/动态键拒编 |
| print / toString / 插值 | `tuple_to_str` 静态展开："("、", "、"name: "、")" 常量段编译期合并，元素经 `to_str` 降级（嵌套 Tup 递归），`collie_rt_concat` 链拼接；格式对齐 Value::to_string Tuple 分支（string 元素不加引号，空元组 `()`） |
| 防错编守卫 | 三元/`==?` 分支产 tuple 显式拒编（虚值 nullptr 进 PHI 会静默错编）；`llvm_type_of(Tup)` 拒编（类字段等位置）；`declared_signature_type` 对 KW_TUPLE 拒编（形状跨函数边界不可知）；比较/算术/len/进数组等其余触点经既有 default/else 分支自然拒编 |
| 接口面 | 零新增 collie_rt 接口（仅复用 `collie_rt_concat`） |

**S22 降级补充（t69 实现）：char/byte/word + 位运算**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `char`/`character` 字面量与声明 | CGType::Str 承载：字面量 `CreateGlobalString(lexeme)`、`declared_cgtype` KW_CHAR/KW_CHARACTER→Str（解释器运行期 char 即 string：打印裸字符/字典序比较 strcmp/可拼接，零新触点复用既有 Str 路径） |
| `byte b = e;` / `word w = e;` | i64 承载零类型扩散（表达式域即 CGType::Int）：visitVarDecl 前置分支特判（初始值须 Int，Num/Double 拒编），`check_bit_range` 插 `icmp ugt v, 255/65535` → bitrange.trap 调 `collie_rt_trap_bit_range(name,max,got)` + unreachable；CGVar.bit_max 记录上限，visitAssign 赋值点同样插检查（对齐解释器 coerce_to_declared 只在赋值点校验、表达式域无截断加宽 integer）；`declared_cgtype` 不映射 KW_BYTE/KW_WORD（类字段/函数签名维持拒编，不静默丢范围校验） |
| `a & b` / `a \| b` / `a ^ b` | 双侧限 CGType::Int（Num tag 静态不可判/Double 拒编不错编）：`and`/`or`/`xor` |
| `a << n` / `a >> n` | 移位量检查 `icmp ugt n, 63`（无符号比较一次覆盖负数与超上限）→ shift.trap 调 `collie_rt_trap_shift_count()` + unreachable，回避 shl/ashr 移位量越界的 poison；通过后 `shl`（位模式 = 解释器无符号域回绕）/`ashr`（算术移位 = 解释器符号扩展） |
| `~a` | `CreateNot`（xor -1）：i64 域 `~x = -x-1` 与解释器 BigInt 精确取反在 i64 范围内一致；仅 Int 操作数 |
| 接口面 | collie_rt 新增陷阱 2 个：`collie_rt_trap_bit_range` / `collie_rt_trap_shift_count`（t58 风格 stderr+exit(1)，文案对齐解释器措辞）；hex 字面量既有 strtoll base16 支持零工作 |

**S23 降级补充（t70 实现）：数组进函数签名**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `function f(a array) …` / `… f(…) array` | 签名承载零成本（`llvm_type_of(Arr)` 本就是不透明 ptr）；卡点纯在元素类型跨不过边界——**elem 动态化为 Num 哨兵**（关键洞察：collie_rt 数组 kind 0=int/1=double 与 t62 Num tag 0/1 编码完全重合）；形参落槽/调用返回点 CGValue.elem 记 Num（顶层函数 + 类方法 + base 调用共 5 处） |
| 动态域不变量 | 进动态域的数组 elem 限 {Int, Double, Num}：bool/str 数组（kind 2/3 无 number 对应）作实参（coerce_call_arg）/返回值（visitReturn）静态拒编，保证动态域运行期 kind ∈ {0,1} |
| `a[i]` 读（elem==Num） | `rt_arr_get` bits + 新接口 `collie_rt_arr_kind` 直接拼 Num（kind 即 tag，零转换）；后续算术/比较/打印走既有 Num 路径 |
| `a[i] = v` 写（elem==Num） | v 限数值系转 Num 表示，下沉新接口 `collie_rt_arr_set_num(arr,i,tag,bits)`：tag==kind 直存 / int 写 double 数组提升（对齐静态路径 Int→Double）/ decimal 写 int 数组陷阱退出（解释器动态异质可容、同质表示不可，拒错编从陷阱，新缺口 CG7） |
| 数组赋值规则 | Num 槽 ← Int/Double/Num 来源放行（不变量内）；静态槽 ← Num 来源拒编（元素类型静态不可知）；三元/==?/tuple 槽的 elem 不一致既有拒编守卫维持（Num vs 静态自然拒编） |
| length/len/print/toString | 运行时 kind 驱动（rt_arr_len/rt_arr_to_str），零改动天然支持动态域 |
| 接口面 | collie_rt 新增 2 个：`collie_rt_arr_kind`（读 kind）/ `collie_rt_arr_set_num`（kind 感知写，含 CG7 陷阱）；范围外：嵌套/异质数组（数组类字段已于 t71 解锁，见 S24） |

**S24 降级补充（t71 实现）：数组作类字段**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `public array f = […];` | register_class_layout 放行（删 t59 时代拒编）；字段槽即 opaque ptr（llvm_type_of(Arr)），struct 建型/8 字节 × 字段数 malloc 上界零改动 |
| 字段读 `obj.f` | CGField 无元素类型伴随，读出即动态域：visitProperty 置 CGValue.elem = Num 哨兵（同 t70 形参机制），下游索引读写/print/传参/返回全走 t70 动态路径，零新 rt 接口 |
| 字段写 `obj.f = v` / 字段初始值 | 守卫下沉 coerce_for_slot 相等分支（一处覆盖 visitPropertyAssign + visitNew 两入口）：右值 elem 限 {Int, Double, Num}，bool/str 数组拒编（kind 2/3 无 number 对应），维持动态域 kind ∈ {0,1} 不变量；变量/tuple 槽的 Arr 另有前置分支，下沉零回归 |
| 引用语义 | 字段槽存指针，读出/写入均指针拷贝共享底层存储（对齐解释器 shared_ptr） |
| 范围外 | Num 字段（16 字节 tagged 装不进 8 字节槽，t62 拍板不变）、Obj 字段（CGField 无 cls 伴随，另立任务）、嵌套/异质数组 |

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
| CG1 | integer 降为 i64，非任意精度；溢出已改显式陷阱报错退出（t58：s{add,sub,mul}.with.overflow + collie_rt_trap_int_overflow，含一元负号；INT64_MIN % -1 硬件陷阱边缘 select 安全除数得 0 对齐解释器），不再静默回绕；超 i64 字面量仍编译期拒编 | 任意精度对齐：BigInt 运行时库（collie_rt）远期 |
| CG2 | print 标量格式已对齐解释器（t53 collie_rt 垫片）；数组格式已对齐（t59 arr_to_str）；tuple 格式已对齐（t68 tuple_to_str 静态展开）；none 等复合值格式仍缺 | 随对应类型的 codegen 支持扩展 collie_rt 接口 |
| CG3 | 运行期类型校验（coerce_to_declared 五处）在编译产物中缺失 | 语义层静态保证覆盖的部分可省；动态部分（object/窄化）随 collie_rt 补 |
| CG4 | 仅支持 x86_64-pc-windows-msvc target | CI 矩阵起来后加 Linux target；LLVM 包已含全部 target 后端 |
| CG6 | 拼接/转串结果 malloc 后不 free，编译产物存在内存泄漏 | 短生命周期进程暂容忍；后续随 string 运行时成熟引入引用计数或 arena 分配器 |
| CG7 | 动态域（数组经函数签名边界）decimal 写 integer 数组陷阱退出（t70：解释器数组动态异质可容，编译产物同质 8 字节槽无法承载，拒错编从陷阱不静默错值） | 异质数组降级支持（元素 tagged 表示）时一并消除 |

## 八、构建方式速查

```bash
# 配置（一次）：
cmake -S compiler -B compiler/build -DCOLLIE_ENABLE_LLVM=ON ^
      -DLLVM_DIR=D:/Program/Development/Environment/llvm-21/lib/cmake/llvm
# 冒烟构建 + 运行：
compiler\build\t48_smoke_build.cmd
compiler\build\codegen\Release\llvm_smoke.exe
```
