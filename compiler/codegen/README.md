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
| S25 | 类实例作类字段：CGField 加 cls 伴随，字段声明识 IDENTIFIER 类名，读写严格同类（属性链/引用语义/整体替换/继承） | 字段实例属性链读写/深链写/跨签名/继承程序编译执行，输出与解释器一致 **✅ t72** |
| S26 | 顶层变量提升全局槽：CGVar.slot 改型 Value*，顶层声明建零初始化 GlobalVariable（初始值仍在 @main 按源序 store），函数/方法体以顶层层拷贝为作用域链底（Tup 剔除，t76 已放行见 S29） | 函数/方法读写全局、全局数组/实例引用语义、遮蔽程序编译执行，输出与解释器一致 **✅ t73** |
| S27 | number 作类字段：删 t62 拒编守卫（StructType 按 llvm_type_of 拼装，{i64,i64} 自动占位），malloc 上界按字段类型累计（Num 16、其余 8） | 字段两态初始值/读写/混合布局/跨签名/继承程序编译执行，输出与解释器一致 **✅ t74** |
| S28 | tuple 相等比较：==/!= 静态展开逐元素深比较（形状不一致编译期常量 false，标量四路降级 And 链合并，嵌套递归） | 无名/命名/嵌套/混型/空元组比较程序编译执行，输出与解释器一致 **✅ t75** |
| S29 | 顶层 tuple 全局化：create_tuple_var 建槽经 create_var_slot（顶层解构槽组升 GlobalVariable），链底拷贝取消 Tup 剔除（tuple_vars_ 注册表本为成员跨函数存活） | 函数/方法读写全局 tuple、嵌套/重赋值可见性/遮蔽程序编译执行，输出与解释器一致 **✅ t76** |
| S30 | print 求值序修复（CG8）：gen_print 两阶段化——先求值全部实参再统一输出，实参副作用输出次序对齐解释器 | 副作用实参（函数/方法体内 print）程序编译执行，输出次序与解释器一致 **✅ t77** |
| S31 | ==?/switch 的 tuple 候选：gen_match_eq 加 Tup 分支单点复用 gen_tuple_eq（静态展开深比较），Tup×非 Tup 恒 false | tuple 目标/候选命中/归组/默认/嵌套/命名程序编译执行，输出与解释器一致 **✅ t78** |
| S32 | 数组相等比较：新接口 collie_rt_arr_eq C 层深比较（先比 len 再逐元素按运行时 kind，数值系混合 double 视图、string strcmp），==/!=、tuple 含数组元素、==?/switch 数组候选三触点同时解锁 | 数组 ==/!=、tuple 含数组元素、==?/switch 数组候选、跨签名动态域数组比较程序编译执行，输出与解释器一致 **✅ t79** |
| S33 | 小数取模：decimal 参与的 `%`（Double×Double / Int×Double / Double×Int）FRem + floor 修正（符号随除数，select 无分支），除零 FRem 天然 NaN | 小数取模程序（四象限符号/混合类型/除零/Infinity/%=/跨签名/数组元素）编译执行，输出与解释器一致 **✅ t80** |
| S34 | none 值（CG2 缺口收敛）：gen_print Void 打常量串 "none"、to_str Void 返 "none"（覆盖 toString/插值）、visitBinary ==/!= 双 Void 恒 true/false，零新增 rt 接口 | none 函数调用结果 print/toString/插值/相等比较/逻辑运算/条件/方法体内程序编译执行，输出与解释器一致 **✅ t81** |
| S35 | 实例（Obj）相等：visitBinary ==/!= 与 gen_match_eq（==?/switch）对任一侧 Obj 恒 false 常量折叠（对齐解释器 values_equal 无 Instance 分支落 default，含同一实例），零新增 rt 接口 | 实例 ==/!=（不同/同一实例/引用别名）、==? / switch 目标为实例、函数内实例相等、结果进逻辑/条件程序编译执行，输出与解释器一致 **✅ t82** |
| S36 | 非常量 tuple 索引：同质 tuple（元素同 CGType 且 ∈ {Int/Double/Bool/Str}）的 `t[i]`（i 变量/表达式）物化运行时数组后 rt_arr_get(动态 idx) 取值，复用负索引归一化 + 越界陷阱（消息与解释器一致），零新增 rt 接口 | 同质 tuple 变量/表达式/负索引变量、命名同质 tuple、for 循环遍历、嵌套索引、函数内局部 tuple 程序编译执行，输出与解释器一致 **✅ t83** |
| S37 | 非常量 tuple get() 动态键：同质命名 tuple（≥1 非空名）的 `t.get(k)`（k 运行期字符串）物化 names+values 数组后新接口 collie_rt_tuple_get 按非空名 strcmp 查找，未命中陷阱消息与解释器核心消息一致 | 四类同质命名 tuple 变量键/常量键回归/混合命名+无名/拼接键/循环动态键/函数内局部 tuple 程序编译执行，输出与解释器一致 **✅ t84** |
| S38 | 嵌套数组：新增数组 kind 4（槽存内层数组 ptr 位模式），限两层且内层数值系（elem ∈ {Int/Double/Num}）——内层读出记 Num 动态域哨兵复用 t70 机制，rt_arr_to_str/rt_arr_eq kind 4 递归，零新增 rt 接口 | 嵌套字面量/print/逐层索引读写/负索引/别名联动/整槽替换/内层混合提升/深比较/length/len/toString 程序编译执行，输出与解释器一致 **✅ t85** |
| S39 | 类继承向上转型（upcast）：对象 struct 头部加 i64 类 id（注册序），字段 GEP 下标+1；实参/返回值/变量与字段槽/赋值五触点放行子类实例进父类静态类型（is_subclass_of 真后代判定）；方法调用点动态分派——静态类无后代直调零开销，有后代读头部 id 后 switch（default=静态类，case=各后代类按 id 排序），各 case 调既有单态化实例，PHI 合流，零新增 rt 接口 | 三级继承链子类传父类形参/中层静态类/返回父类装子类/父类槽覆写与继承方法混调/类字段 upcast/无后代直调程序编译执行，输出与解释器一致 **✅ t86** |
| S40 | byte/word 类字段：CGField 加 bit_max（255/65535），register_class_layout 前置分支 i64 槽承载；visitNew 字段初始化与 visitPropertyAssign 赋值两触点 coerce_for_slot 后插 check_bit_range 范围陷阱（t69 机制复用，对齐解释器 coerce_to_declared），读出恒 Int 表达式域无截断，零新增 rt 接口 | byte/word 字段初始化/读取/赋值/边界值 255与65535/表达式域无截断/构造器与方法体内 this.field 赋值/继承字段范围保持程序编译执行，输出与解释器一致 **✅ t87** |
| S41 | bool/string 数组动态域透传：解除 t70/t71 四守卫（实参/返回值/字段槽/动态槽赋值）——任意元素数组（含嵌套 kind 4）指针透传，kind 随对象自带；print/len/==/toString rt 侧全 kind 覆盖零改动；动态域索引读插 kind>1 运行时陷阱（新缺口 CG9，拒错编从陷阱）；动态域索引写 Bool/Str 值打对应 kind tag 直写（rt_arr_set_num 天然覆盖，mismatch 落 CG7 消息泛化） | bool/str 数组作实参/返回值透传/类字段存取/==（同内容/异内容/跨 kind）/动态域写 bool-str 值引用联动与负索引/数值域读写回归/嵌套数组透传 print-len-== 程序编译执行，输出与解释器一致 **✅ t88** |
| S42 | 嵌套数组放宽：visitArrayLiteral 两守卫解除（≥3 层/内层 bool-str），任意 elem 内层数组进 kind 4 槽；visitIndexAssign 整槽替换放宽为任意元素内层数组（非数组值仍拒编）；rt 侧零改动（elem_to_bits/rt_arr_to_str/rt_arr_eq 全 kind 递归已就绪）；内层经动态域索引读出 kind ≥ 2 落 t88 既有 CG9 陷阱 | 内层 bool/str 字面量/print/length/len/整槽替换/深比较（同异内容与跨 kind）/三层嵌套 print-==-整槽替换/混合内层 kind/别名联动/数值内层逐层读写回归程序编译执行，输出与解释器一致 **✅ t89** |
| S43 | Num 元素数组字面量：visitArrayLiteral 数值系同质判定扩展含 Num（互混或全 Num 统一提升 Double 视图，rt format_f64 整数值省 .0 与解释器混合表示输出对齐）；to_double 加 Num 分支（tag select 免分支）；visitIndexAssign 静态数值槽收 Num 值下沉既有 rt_arr_set_num（tag==kind 直存/0→1 提升/1→0 落 CG7 陷阱）；零新增 rt 接口 | Num（整/小数态）与 Int/Double 混合字面量/全 Num 字面量/print/索引读（含负索引）/length/len/Num 值写 int-double 槽/深比较/函数内局部 Num 数组循环遍历程序编译执行，输出与解释器一致 **✅ t90** |
| S44 | 嵌套函数声明：受限雷姆达提升——declare_function 加 prefix 改编键（outer.inner，符号 collie.outer.inner），尾部 declare_nested_in 递归下探 Block/If/While/For/DoWhile/Switch 建原型进 nested_fns_ 注册表；visitFunction 嵌套路径声明处向 scopes_ 登记 fn_key 绑定（对齐解释器"执行到声明处 env_.define"——声明前不可见/块退出失效），嵌套体链底拷入外层链函数绑定（自身递归/前置兄弟可见），生成现场 in_function_/返回类型保存恢复；visitCall 作用域链函数绑定优先于顶层 functions_；函数名作值/被赋值拒编；零新增 rt 接口 | 基本嵌套/嵌套读全局/自身递归 fac(5)/前置兄弟嵌套/带参字符串嵌套程序编译执行，输出与解释器一致 **✅ t91** |
| S45 | 无初始化变量声明：四静态类型 {integer,decimal,bool,string} 放行——槽照常创建（顶层零初始化全局槽/函数内 alloca 不预存），CGVar 加 uninit + decl_depth；读 uninit 槽拒编（语义层流不敏感放行的分支/循环内赋值后读，解释器运行期仍 none，零初始化槽会错值——拒编不错编）；同块（scopes_ 同深度）赋值清 uninit 放行后续读，深层块赋值存值不清标记；零新增 rt 接口 | 顶层声明后隔句赋值再读/四静态类型/函数内局部/顶层全局函数体内读/循环体内声明+同块赋值程序编译执行，输出与解释器一致 **✅ t92** |
| S46 | 三元/==? 分支实例类型统一到最近公共祖先：新增 nearest_common_ancestor 沿 super 链求 NCA（含自身端点），gen_ternary 与 match 合流两触点 Obj cls 不等改求 NCA——有则 result cls 取祖先（Obj 的 LLVM 表示统一指针，PHI 无关 cls；t86 对象头类 id + 动态分派保证合流值按运行期真实类解析方法、字段走父类前缀布局），无公共祖先维持拒编不错编；零新增 rt 接口 | 子类/父类、兄弟类、孙类/兄弟类三元合流，孙类/父类统一非根祖先字段读，==? 三支合流，合流值直调方法与进函数形参程序编译执行，输出与解释器一致 **✅ t93** |
| S47 | 三元/==? 分支数组元素类型合流统一动态域：gen_ternary 与 match 合流两触点 Arr elem 不等改统一 elem=Num 动态域哨兵（t70/t88 既有机制）——数组值不透明 ptr，PHI 无关 elem；合流值为新鲜值元数据自诞生即动态无程序序失配，kind 随数组对象运行期自带（print/len/== rt 侧全 kind 覆盖，索引读数值系正常、kind ≥ 2 落既有 CG9 陷阱不错值）；两触点 result_elem 逐支累计（同 t93 result_cls 模式）；零新增 rt 接口 | Int/Double 三元合流索引读（两向）、合流值存动态槽整组 print/len、Str/Int 合流、合流值相等比较、==? 三支合流、动态域值再进三元程序编译执行，输出与解释器一致 **✅ t94** |
| S48 | 三元/==? 分支 tuple 合流静态展开：新增 merge_tuple_arms 三阶段——元数据递归校验形状（元素数+名字表+嵌套位置）并合并各叶位类型（复用标量合流规则：数值提升/Tri 加宽/Arr elem 降 Num 哨兵/Obj cls 求 NCA），逐支叶值对齐指令落各支末块（DFS 序展平），merge 块逐叶 PHI 自底向上重建新鲜 CGTuple；形状/名字不一致维持拒编不错编；零新增 rt 接口 | 无名/命名/字面量支/元素数值混型/嵌套 tuple 三元合流，==? 三支合流，合流值索引/字段/get/length/相等比较/再进三元，tribool 三分支形式，数组元素两支 elem 不同程序编译执行，输出与解释器一致 **✅ t95** |
| S49 | 无初始化变量声明放行面扩展：visitVarDecl 从四静态类型（t92）扩展 number/tribool/char/character/byte/word——Num（struct{i64,i64}）/Tri（i8）/Str（ptr）零初始化槽合法，byte/word 沿用 i64 承载 + bit_max（赋值走 visitAssign 通用路径，既有赋值点 check_bit_range 陷阱自动生效）；uninit/decl_depth/同块清除机制复用；array/Tuple/类维持拒编；零新增 rt 接口 | number 整/小数态、tribool 三态、byte/word 范围内赋值、char/character、函数内局部、顶层全局函数体内读、循环体内声明+同块赋值程序编译执行，输出与解释器一致 **✅ t96** |
| S50 | byte/word 函数返回类型：declare_function 顶层函数返回类型 KW_BYTE/KW_WORD 前置识别为 CGType::Int 承载 + CGFunction.ret_bit_max（255/65535），visitFunction 保存/设置/恢复 current_ret_bit_max_，visitReturn 值对齐 Int 后插 check_bit_range（复用 t69 陷阱，越界调 rt_trap_bit_range，对齐解释器 coerce_to_declared 返回值校验）；返回值作普通 Int 表达式域消费；~~byte/word 参数~~（S51 t98 已放行）/类方法返回维持拒编；零新增 rt 接口 | byte/word 返回值 print/参与算术/存 byte-word 变量（声明与赋值点范围陷阱）/多返回路径 clamp/循环内反复调用/word 返回值比较程序编译执行，输出与解释器一致 **✅ t97** |
| S51 | byte/word 函数参数：declare_function 参数循环 KW_BYTE/KW_WORD 前置识别为 CGType::Int 承载（llvm 形参 i64），visitFunction 形参绑定置 CGVar.bit_max（255/65535）——体内重赋走既有赋值点 check_bit_range 陷阱、返回越界走 visitReturn t97 陷阱；调用点零改动——重载解析要求实参类型即 byte/word（整数字面量/算术实参双端语义错非差分面），实参恒为来源处已校验值；~~类方法/构造器参数维持拒编~~（S52 t99 已放行）；零新增 rt 接口 | byte 形参 identity/clamp 多返回/word 形参/嵌套调用/byte 函数返回作实参/循环内反复调用/形参返回值比较程序编译执行，输出与解释器一致 **✅ t98** |
| S52 | byte/word 类方法/构造器参数与返回：方法注册循环返回类型/参数 KW_BYTE/KW_WORD 前置识别为 CGType::Int 承载（返回位复用 CGMethod 继承的 ret_bit_max），gen_method_body 设/复位 current_ret_bit_max_（返回走 t97 陷阱）+ 形参绑定点读 AST 形参类型插 check_bit_range 范围陷阱（方法单签名按名解析、实参可为整数字面量、覆盖方法/构造器/base 全路径）+ 置 CGVar.bit_max；coerce_call_arg 零改动；零新增 rt 接口 | 构造器 byte 参数字面量实参/方法 byte 参数字面量+变量实参/方法 byte 返回 print-算术-存变量/byte 参数+byte 返回多路径/word 参数+word 返回多路径与循环累计/方法返回值作方法实参/byte 返回值有序比较程序编译执行，输出与解释器一致 **✅ t99** |
| S53 | 类实例进数组（同类，kind 5）：arr_kind_of(Obj)→5、elem_to_bits/bits_to_elem 加 PtrToInt/IntToPtr；visitArrayLiteral 追踪元素类名（复用 CGValue.cls）+ 混合类守卫，visitVarDecl/visitAssign array 分支传播 cls + 异类守卫，visitIndex 静态读出传播 cls（arr[i].field/method() + 动态分派），visitIndexAssign Obj 分支 cls 兼容守卫（子类 upcast 放行）；rt 三触点加 kind 5——arr_to_str `<object>` / arr_eq 恒不等 / trap_arr_kind `object`；本地静态读出全解锁，动态域 obj 读出落 CG9 陷阱 | 同类实例数组 build/len/print/toString/本地静态读出字段与方法调用/负索引/整槽写同类/子类 upcast 整槽写后动态分派/== 实例恒不等程序编译执行，输出与解释器一致 **✅ t100** |
| S54 | 无初始化 array/类类型变量声明：visitVarDecl 无初始化分支加 KW_ARRAY（建 opaque ptr 槽 + elem=Num 动态域哨兵 t70）/IDENTIFIER 已注册类名（建 Obj 槽 + cls）两分支，沿用 uninit + decl_depth（t92）；visitAssign Arr/Obj 两分支原早退不经通用路径故各补同块 uninit 清除；读 uninit 拒编、深层块赋值不清标记全复用 t92 机制 | 无初始化 array 隔句赋值读写/别名引用语义/无初始化类变量赋值后字段读+方法调用+upcast 动态分派/函数内局部/循环体内同块赋值程序编译执行，输出与解释器一致 **✅ t101** |
| 后续 | BigInt 运行时化 | 逐任务扩展 |

不在第一期范围：异常语义（tuple 已于 S21 t68 以静态展开解锁、相等比较已于 S28 t75 解锁、同质 tuple 非常量索引已于 S36 t83 解锁、同质命名 tuple get() 动态键已于 S37 t84 解锁，异质 tuple 非常量索引/动态键/进函数签名/进数组仍拒编；两层数值系嵌套数组已于 S38 t85 解锁，≥3 层与内层 bool/str 已于 S42 t89 解锁——内层元素经动态域索引读出 kind ≥ 2 落 CG9 陷阱不错值；类继承向上转型已于 S39 t86 解锁——限覆写同签名，downcast/无关类/父类静态类型调子类特有方法仍拒编；byte/word 类字段已于 S40 t87 解锁，byte/word 返回类型已于 S50 t97 解锁——返回值经 check_bit_range 校验，byte/word 函数参数已于 S51 t98 解锁——形参绑定置 bit_max 复用赋值点陷阱（调用点无需陷阱：重载解析保证实参恒为已校验 byte/word 值），byte/word 类方法/构造器参数与返回已于 S52 t99 解锁——方法单签名按名解析，形参绑定点插 check_bit_range 范围陷阱（实参可为整数字面量，覆盖方法/构造器/base 全路径），返回走 t97 陷阱（方法调用结果参与 ==/!= 比较、word→byte 返回属解释器语义边界非 codegen 拒编面）；bool/string/嵌套数组动态域透传已于 S41 t88 解锁——print/len/== 全 kind 安全，动态域索引读出 bool/str/嵌套元素运行期陷阱不错值，缺口 CG9；嵌套函数声明已于 S44 t91 解锁——限函数体内嵌套（受限雷姆达提升），嵌套体引用外层局部（捕获）/类方法体内嵌套/函数名作值仍拒编；无初始化变量声明已于 S45 t92 解锁——限四静态类型且同块赋值后读，分支/循环块内赋值后读仍拒编（number/tribool/char/character/byte/word 已于 S49 t96 一并放行，array/类类型已于 S54 t101 放行——array 建 opaque ptr 槽 + elem=Num 动态域哨兵、类类型建 Obj 槽 + cls，visitAssign Arr/Obj 分支补同块 uninit 清除，无初始化 Tuple 仍拒编——形状无从推断）；三元/==? 分支不同类实例已于 S46 t93 统一到最近公共祖先——无公共祖先的两类合流仍拒编；三元/==? 分支不同 elem 数组已于 S47 t94 统一动态域——数组变量再赋不同 elem 仍拒编；三元/==? 分支 tuple 已于 S48 t95 静态展开合流——限同形状（元素数+名字表递归一致），形状/名字不一致仍拒编；类实例进数组已于 S53 t100 解锁——限同类（kind 5，复用 CGValue.cls 记元素类名），本地静态读出/字段/方法调用/整槽写同类或子类 upcast 全支持，混合类字面量/整槽写异类仍拒编，动态域（数组过签名/字段/返回值）obj 元素读出落 CG9 陷阱不错值）。
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
| 类实例（`Point p = new Point()`） | `ptr`（指向 malloc 零初始化块，按 `collie.class.<类名>` StructType GEP 访问） | 每类一个 StructType（字段按声明顺序布局，继承时父链字段 base-first 合并）；类名另记于 CGValue/CGVar 的 cls 字段；指针拷贝即引用语义；实例可作函数参数/返回值（签名处类名，严格同类）；~~不可进数组/元组（拒编）~~（同类实例进数组已于 S53 t100 解锁，进元组仍拒编） |
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
| `a % b`（整数） | **floor 取模**（SPEC §4，Python 风格）：`r = srem a, b`；`if (r != 0 && (r < 0) != (b < 0)) r += b`（select 实现，无分支）；decimal 参与已于 S33 t80 解锁（FRem 路径，见 S33 补充节） |
| 一元 `-a` | `sub i64 0, %a`（整数）/ `fneg`（小数） |
| 顶层语句序列 | 依序生成进 `@main` entry 起始的基本块链 |

**S3 降级补充（t50 实现）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 变量声明 `integer/decimal/bool/string x = init` | entry 块头部 `alloca`（利于 mem2reg）+ `store`；~~无初始化拒编~~（S45 t92 解锁四静态类型，见 S45 补充节）；`number` 变量已于 S15（t62）解锁 |
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
| `s.subString(start[, end])` | `call ptr @collie_rt_str_substring(ptr, i64, i64)`：UTF-8 码点区间 [start,end)，缺省 end 传 -1 运行时取 length，越界 clamp、start>=end 得空串；参数收 Int/Double/Num（~~限 Int，Double/NaN 特例拒编~~ t105 解锁）——Double/Num 对齐解释器：end 在 floor 前判 NaN/精确 -1.0 取 length（floor(-0.9) 为 -1 不特判、clamp 到 0 得空串，顺序敏感），NaN start 归 0，其余 llvm.floor 后 double 域 clamp [0,4e18]（防 fptosi poison，±Infinity/超大值由 rt 垫片按 length 收口）转 i64；零新增 rt 接口 |
| `x.toString()`（任意标量接收者） | 复用 `to_str` 降级（与内建 `toString(x)` 同一路径），结果为 Str |
| `toNumber()` / number/tuple 方法 | toNumber() 已于 S16（t63）解锁；tribool 方法（isTrue/isFalse/isUnset）已于 S18（t65）解锁；number 专属方法（abs/integerPart 等）已于 S20（t67）解锁；tuple.get("字面量键")已于 S21（t68）静态解析 |

**S12 降级补充（t59 实现）：array 最小闭环**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 数组字面量 `[a, b, c]` | 逐元素求值后同质推断（Int/Double 混合整体提升 Double，提升后输出仍与解释器一致；其余混合/嵌套拒编；空数组 elem 记 Int）→ `call ptr @collie_rt_arr_new(i64 len, i64 kind)` 单块 malloc 数组对象（kind：0=Int/1=Double/2=Bool/3=Str）→ 逐元素 `elem_to_bits` 转 8 字节位模式后 `collie_rt_arr_set` 写槽 |
| `a[i]` 读 / `a[i] = v` 写 | `collie_rt_arr_get/set(ptr, i64[, i64 bits])`：负索引归一化（-1 为最后一个元素），越界 stderr 报错后 exit(1)（消息格式同 str_index）；读结果 `bits_to_elem` 按 elem 还原；写入仅允许 Int→Double 提升否则拒编；求值顺序 object→index→value 对齐解释器 |
| `a.length` / `len(a)` | `call i64 @collie_rt_arr_len(ptr)`（len 内建同时支持 string 走 str_len） |
| `print(a)` / `toString(a)` / 拼接 | `call ptr @collie_rt_arr_to_str(ptr)` 整体转 `[1, 2, 3]` 格式串（对齐 Value::to_string：元素递归格式化、字符串不加引号）后走 print_str/Str 路径 |
| 赋值/三元中的数组 | 指针拷贝即引用语义；~~两侧 elem 不一致拒编~~（三元/==? 合流已于 S47 t94 统一 elem=Num 动态域；~~数组变量再赋不同 elem 仍拒编~~ t106 解锁：槽 elem 降级 Num 动态域哨兵，循环回边/全局槽跨函数快照两面守卫拒编不错编，见 S55） |
| array 函数参数/返回值 | 拒编（`array` 声明无元素类型标注，跨函数签名无法定 elem；待带元素类型的声明语法或动态 kind 方案） |

**S13 降级补充（t60 实现）：class 最小闭环**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class C { ... }` 声明 | 注册遍（与函数原型同属第一遍）建 `collie.class.C` StructType：字段按声明顺序布局（下标即 GEP 索引）；方法/构造器降为 `collie.C.<方法名>` InternalLinkage 独立函数，this 作隐藏首参 ptr；第二遍生成方法体（现场保存/还原同 visitFunction，this 直持 SSA 值不落栈槽） |
| `new C(args)` | `call ptr @collie_rt_obj_new(i64 size)`（malloc + memset 零初始化，size = 8×字段数上界，与 DataLayout 解耦）→ 字段初始值按声明顺序求值写入（仅 Int→Double 隐式提升，同 coerce 四处）→ 构造器实参求值 → 构造器调用（与解释器 visitNew 三段顺序一致）；无构造器带实参拒编 |
| `obj.field` 读 / `obj.field = v` 写 | `CreateStructGEP` + load/store；写入仅允许 Int→Double 提升否则拒编；求值顺序 object→value 对齐解释器 |
| `obj.m(args)` / `this.m(args)` | 类方法表优先命中 → `call @collie.C.m(ptr this, args...)`；未命中且 `toString()` 无参 → 固定串 `"<object>"` 兜底（分派顺序对齐解释器）；否则拒编 |
| `print(obj)` / `toString(obj)` | 固定输出 `"<object>"`（对齐 Value::to_string Instance 分支） |
| 赋值/三元中的实例 | 指针拷贝即引用语义；~~两侧类名不一致拒编~~（赋值收后代类 S39 t86、三元/==? 分支统一最近公共祖先 S46 t93 陆续放宽，无继承关系仍拒编） |
| 范围外拒编 | 无初值字段（解释器落 none 无静态表示）、tuple 字段、`object` 声明类型、方法重载（extends/base/实例作参数返回值已于 S14 t61 解锁；array 字段 S24 t71、实例字段 S25 t72、number 字段 S27 t74 陆续解锁；tribool 字段随 S18 tribool 支持自然放行，t74 实测确认；实例相等比较已于 S35 t82 解锁恒 false、~~实例进数组/元组仍拒编~~（同类实例进数组已于 S53 t100 解锁见 S53，进元组仍拒编）） |

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
| 范围外拒编 | number 数组元素表示不变（S12 同质 kind 拍板，动态域见 S23）、任意精度自动扩容（BigInt 运行时化留远期，超 i64 整数运算陷阱退出）（toNumber 内建已于 S16 t63、number 类字段已于 S27 t74 解锁） |

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
| 范围外拒编 | 无默认分支形式（tribool 穷尽三态省默认已于 S18 t65 解锁，其余目标类型仍要求默认分支）；object 相等比较目标/候选已于 S35 t82 解锁（恒 false 常量折叠，全部不命中）；数组/元组深比较候选（元组已于 S31 t78、数组已于 S32 t79 解锁） |

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
| 范围外拒编 | object 相等比较目标/候选已于 S35 t82 解锁（恒 false 常量折叠，全部不命中）；数组/元组深比较候选（同 `==?` 拒编面，元组已于 S31 t78、数组已于 S32 t79 解锁） |

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
| 防错编守卫 | ~~三元/`==?` 分支产 tuple 显式拒编（虚值 nullptr 进 PHI 会静默错编）~~（同形状 tuple 合流已于 S48 t95 静态展开解锁，形状/名字不一致仍拒编）；`llvm_type_of(Tup)` 拒编（类字段等位置）；`declared_signature_type` 对 KW_TUPLE 拒编（形状跨函数边界不可知）；算术/len/进数组等其余触点经既有 default/else 分支自然拒编（相等比较已于 S28 t75 解锁） |
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
| 动态域不变量 | ~~进动态域的数组 elem 限 {Int, Double, Num}：bool/str 数组（kind 2/3 无 number 对应）作实参（coerce_call_arg）/返回值（visitReturn）静态拒编~~（t88 解除：任意 kind 透传进动态域，索引读 kind≥2 运行期 CG9 陷阱，见 S41） |
| `a[i]` 读（elem==Num） | `rt_arr_get` bits + 新接口 `collie_rt_arr_kind` 直接拼 Num（kind 即 tag，零转换）；后续算术/比较/打印走既有 Num 路径 |
| `a[i] = v` 写（elem==Num） | v 限数值系转 Num 表示，下沉新接口 `collie_rt_arr_set_num(arr,i,tag,bits)`：tag==kind 直存 / int 写 double 数组提升（对齐静态路径 Int→Double）/ decimal 写 int 数组陷阱退出（解释器动态异质可容、同质表示不可，拒错编从陷阱，新缺口 CG7） |
| 数组赋值规则 | Num 槽 ← Int/Double/Num 来源放行（不变量内）；静态槽 ← Num 来源拒编（元素类型静态不可知）；~~三元/==?/tuple 槽的 elem 不一致既有拒编守卫维持~~（三元/==? 合流已于 S47 t94 统一动态域；变量再赋已于 S55 t106 降级解锁；tuple 槽维持拒编） |
| length/len/print/toString | 运行时 kind 驱动（rt_arr_len/rt_arr_to_str），零改动天然支持动态域 |
| 接口面 | collie_rt 新增 2 个：`collie_rt_arr_kind`（读 kind）/ `collie_rt_arr_set_num`（kind 感知写，含 CG7 陷阱）；范围外：嵌套/异质数组（数组类字段已于 t71 解锁，见 S24） |

**S24 降级补充（t71 实现）：数组作类字段**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `public array f = […];` | register_class_layout 放行（删 t59 时代拒编）；字段槽即 opaque ptr（llvm_type_of(Arr)），struct 建型/8 字节 × 字段数 malloc 上界零改动 |
| 字段读 `obj.f` | CGField 无元素类型伴随，读出即动态域：visitProperty 置 CGValue.elem = Num 哨兵（同 t70 形参机制），下游索引读写/print/传参/返回全走 t70 动态路径，零新 rt 接口 |
| 字段写 `obj.f = v` / 字段初始值 | 守卫下沉 coerce_for_slot 相等分支（一处覆盖 visitPropertyAssign + visitNew 两入口）：~~右值 elem 限 {Int, Double, Num}，bool/str 数组拒编~~（t88 解除：任意元素数组透传，见 S41）；变量/tuple 槽的 Arr 另有前置分支，下沉零回归 |
| 引用语义 | 字段槽存指针，读出/写入均指针拷贝共享底层存储（对齐解释器 shared_ptr） |
| 范围外 | 嵌套/异质数组（Obj 字段已于 t72 解锁见 S25，Num 字段已于 t74 解锁见 S27） |

**S25 降级补充（t72 实现）：类实例作类字段**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `public Engine e = new Engine();` | register_class_layout 加 IDENTIFIER 前置分支：类名须已注册（声明在前，同父类/签名要求；前向引用实为语义层更早拦截 "Invalid type"，classes_ 查询是防御性双保险），CGField 加 cls 伴随；字段槽即 opaque ptr（llvm_type_of(Obj)），struct 建型/malloc 上界零改动 |
| 字段读 `obj.e` | visitProperty 读出带 CGField.cls，下游属性链/方法调用（单态化分派）/传参/返回全走 t61 既有 Obj 路径 |
| 字段写 `obj.e = v` / 字段初始值 | coerce_for_slot 加 slot_cls 参数（默认空串），相等分支 Obj 严格同类校验（t61 拍板：静态 cls 即动态类是单态化分派前提，向上转型拒编不错编）；变量/tuple 槽的 Obj 另有前置分支，加参零回归 |
| 引用语义 | 字段槽存实例指针，读出/写入均指针拷贝共享底层存储（对齐解释器 shared_ptr）；深层属性链写（`g.car.engine.power = v`）沿 cls 伴随逐级定位布局 |
| 范围外 | 向上转型字段（同 t61）、相互/自引用类字段（声明序不可达，语义层已拦）、Tup 字段（既有拒编不变；Num 字段已于 t74 解锁见 S27） |

**S26 降级补充（t73 实现）：顶层变量提升全局槽**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 顶层 `integer counter = 10;` | create_var_slot 统一建槽：顶层（`!in_function_ && scopes_.size()==1`）建零初始化 GlobalVariable（InternalLinkage + `collie.g.` 前缀防符号冲突），初始值仍在 @main 当前位置按源序 store；块内（for/if 等，深度 >1）声明维持 entry alloca |
| 函数/方法体读写全局 | visitFunction/gen_method_body 重建作用域栈时以顶层层（saved_scopes.front()）拷贝为链底，再压参数层；lookup_var 零改动，形参/局部声明天然遮蔽全局 |
| 顺序安全 | 语义层在函数声明处分析函数体（只见此前声明的顶层变量）+ 前向调用为语义/运行期错误 ⇒ 变量 store 必先于任何函数内读，零初始化值不可被观察 |
| CGVar.slot 改型 | `AllocaInst*` → `Value*`（全部使用点仅 CreateLoad/CreateStore，纯声明面改动零风险），GlobalVariable/alloca 同型共用 |
| 范围外 | 顶层 tuple 跨函数（原剔除 Tup 条目函数内拒编，已于 S29 t76 解锁：解构槽组升全局槽 + 链底一并拷贝） |

**S27 降级补充（t74 实现）：number 作类字段**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `public number v = 10;` | register_class_layout 删 t62 拒编守卫即放行：StructType 按 llvm_type_of 逐字段拼装，Num 自动占位 {i64,i64}（复评推翻 t62「8 字节槽装不下」理由——类布局本非固定 8 字节槽）；GEP 字段读写由字段类型驱动，零特判 |
| `new` 字段块分配 | visitNew malloc 上界从 `8 × 字段数` 改为按字段类型累计（Num 记 16、其余 8）；对齐 ≤ 8 前提下累计值即精确尺寸；rt_obj_new 零改动 |
| 字段初始值 / 字段写 | coerce_for_slot 既有 Num 槽分支：integer/decimal 来源 to_num 加宽（保持原表示打 tag，对齐解释器 coerce_to_declared KW_NUMBER 原样通过）、number 来源相等直通，零新增转换 |
| 字段参与运算/方法/跨签名 | 字段读出即 CGValue{Num}，算术/比较/print/插值/abs 等内建方法/函数传参返回全走 t62 既有 Num 路径（16 字节按值流转先例：变量/全局槽、tuple 解构槽、签名传参） |
| 范围外 | Tup 字段（既有拒编不变）、number 字段读出赋窄化静态槽（走既有 Num→静态槽拒编）、字段上直接一元负号/相等比较（语义层对属性访问的既有限制，经 number 局部变量可用） |

**S28 降级补充（t75 实现）：tuple 相等比较**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t1 == t2` / `t1 != t2` | visitBinary ==/!= 前置分支 `gen_tuple_eq` 静态展开（先于 require_numeric）：元素数/名字表编译期全可知，形状不一致直接常量 `i1 false`（对齐解释器 values_equal 先比 size 再比 names）；`!=` 对结果 CreateNot 取反 |
| 逐元素比较 | 复用四路标量降级出 i1 后 And 链合并：Str×Str→rt_strcmp==0、任一 Tri（另一侧限 tribool/bool）→to_tri 后 icmp、任一 Num→rt_num_cmp op 0（双整数精确、混合 double 视图）、Int/Double→icmp/fcmp OEQ（5 == 5.0 混型提升） |
| 嵌套 tuple 元素 | gen_tuple_eq 递归（注册表按值拷贝不留引用，防 register_tuple 扩容失效） |
| 恒 false 配对 | Tup × 非 Tup 整体/元素（kind 不等）、任一元素 Obj（解释器 values_equal 无 Instance 分支，同一实例也 false）、异型标量配对（Str×Int 等）——均编译期常量 false，对齐解释器 |
| 防错编守卫 | 元素含 Arr 曾拒编不错编（"tuple equality with array element"，已于 S32 t79 下沉 rt_arr_eq 深比较解锁）；Tup × 非 Tup 整体比较实为语义层更早拦截（"Incomparable operand types"），codegen 分支是防御性双保险 |
| 范围外 | `==?`/switch 的 tuple 候选（gen_match_eq 另一路径，已于 S31 t78 解锁）、tuple 关系比较 < <= > >=（解释器同样不支持）、数组深比较（已于 S32 t79 解锁）；接口面零新增 collie_rt 接口 |

**S29 降级补充（t76 实现）：顶层 tuple 全局化**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 顶层 `Tuple t = (10, 20.5, "x");` | create_tuple_var 建槽从 create_entry_alloca 换 create_var_slot：顶层（`!in_function_ && scopes_.size()==1`）逐元素解构槽升零初始化 GlobalVariable（`collie.g.` 前缀，t73 机制复用），初始值仍当前位置 store；嵌套子槽组递归同条件天然覆盖；块内/函数内声明维持 entry alloca |
| 函数/方法体读写全局 tuple | visitFunction/gen_method_body 链底拷贝取消 Tup 条目剔除（剔除循环换整层拷贝）；tuple_vars_ 注册表本为成员跨函数存活，lookup_var/load_tuple_var/store_tuple_var 零改动 |
| 重赋值可见性 | 函数内同形状整体重赋值逐槽 store 全局槽，跨函数可见（对齐解释器）；换形状重赋值走既有拒编（"assigning tuple of different shape"） |
| 遮蔽 | 函数内声明同名局部 tuple 压新层天然遮蔽全局，函数结束弹层不影响全局 |
| 范围外 | tuple 进函数签名（形状跨边界不可知，既有拒编不变）、顶层块内声明 tuple（非顶层层，维持 alloca 不入链底，与 t73 标量一致） |

**S30 降级补充（t77 实现）：print 求值序修复（CG8 消除）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `print(a, f(), b)` 实参含副作用输出 | gen_print 两阶段化：第一循环 emit 全部实参按值收集 vector\<CGValue\>（副作用调用按源序发生），第二循环统一打印 sep+值+换行——对齐解释器 call_builtin_print 先求值全部实参再打印的次序 |
| 打印阶段安全性 | to_str/tuple_to_str/arr_to_str 仅做转串无输出副作用；Tup 注册表只追加不重排，按值收集的 CGValue 下标跨阶段有效；零新增 collie_rt 接口 |
| 范围外 | CG2（none 等复合值打印格式）不动；toString/插值拼接链单实参无交错问题不涉及 |

**S31 降级补充（t78 实现）：==?/switch 的 tuple 候选**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t ==? (1, "x"): a, def` / `switch (t) { (1, "x") {...} }` | gen_match_eq 末尾 unsupported 前加 Tup 分支：双 Tup 走 gen_tuple_eq（t75 静态展开深比较单点复用——形状不一致编译期常量 false、逐元素四路降级 And 链、嵌套递归）；`==?`（级联比较块链）与 switch 两调用点同时解锁，零新增接口 |
| Tup × 非 Tup 候选 | 恒 false（对齐解释器 values_equal kind 不等）；实测语义层更早拦截（"Incomparable candidate value type in '==?'"），codegen 分支是防御性双保险 |
| 防错编守卫 | 候选/目标 tuple 含 Arr 元素曾由 gen_tuple_eq 递归内拒编覆盖（"tuple equality with array element"，已于 S32 t79 下沉 rt_arr_eq 深比较解锁） |
| 范围外 | 数组目标/候选（已于 S32 t79 解锁）；tuple 关系比较不涉及 |

**S32 降级补充（t79 实现）：数组相等比较**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `a1 == a2` / `a1 != a2` | visitBinary ==/!= 前置 Arr×Arr 分支：call 新接口 `collie_rt_arr_eq(ptr, ptr) -> i64`，与 0 icmp ne 出 i1，`!=` CreateNot 取反；数组动态长度静态展开不可达，深比较单点下沉 C 层 |
| rt_arr_eq 语义 | 对齐解释器 values_equal Array 分支：先比 len 再逐元素按运行时 kind——同 kind integer/bool i64 直比、decimal 位模式还原 double 按值比较（NaN != NaN 一致）、string strcmp 内容比较；kind {0,1} 混合按 double 视图（`[1,2,3] == [1.0,2.0,3.0]` 为 true）；bool/string 与其它 kind 配对恒不等；len==0 天然相等；运行时 kind 判定天然覆盖 t70 动态域（elem=Num）数组 |
| tuple 含数组元素 | gen_tuple_eq 元素分支：双 Arr 下沉 rt_arr_eq（替换 t75 既有拒编）、Arr × 非 Arr 元素恒 false（kind 不等） |
| `==?`/switch 数组候选 | gen_match_eq 加 Arr 分支：双 Arr 下沉 rt_arr_eq、Arr × 非 Arr 恒 false（语义层通常更早拦截，防御性双保险）；两调用点同时解锁 |
| 范围外 | 数组关系比较 < <= > >=（语义层两端一致拦截 "Invalid operands for comparison"）；Arr × 非 Arr 整体 ==（语义层两端一致拦截 "Incomparable operand types"）；嵌套数组元素（字面量层已拒编不存在）；接口面新增 1 个 collie_rt 接口（arr_eq） |

**S33 降级补充（t80 实现）：小数取模**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `a % b`（decimal 参与） | visitBinary OP_MODULO 加 Double 分支（Num 路径之后、Int×Int 之前）：两侧 to_double 后 `frem double`（语义即 fmod 截断取余），floor 修正——`r 非零且与 b 异号时 r += b`（FCmp ONE/OLT + select 无分支，仿 Int 路径既有写法），结果 Double |
| 除零与特殊值 | `x % 0.0` FRem 天然 NaN，且 NaN 使 ONE 比较为 false 不触发修正（与解释器 b==0.0 提前返 NaN 殊途同归）；`-0.0 == 0.0` 使 nonzero 为 false，同解释器 `r != 0.0` 判定；`-7.5 % Infinity` 修正为 +Infinity 两端一致 |
| 覆盖类型组合 | Double×Double / Int×Double / Double×Int；`%=` 复合赋值天然复用（parser 脱糖）；数组 double 元素索引读出参与 |
| 范围外 | number 参与的 `%`（S15 t62 已下沉 rt_num_arith op 4，路径不动）；BigInt 超 i64 整数取模（既有 CG1 陷阱域）；非数值参与（语义层两端一致拦截 "Numeric operands expected for arithmetic operation"）；零新增 collie_rt 接口 |

**S34 降级补充（t81 实现）：none 值的 print/toString/==**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `print(f())`（f 返回 none） | gen_print Void case 打常量串 "none"（`CreateGlobalString("none")` → rt_print_str），对齐解释器 Value::to_string None 分支；两阶段收集（t77）中 Void 值 value=nullptr 无碍，副作用求值保序 |
| `toString(f())` / `@"...{f()}..."` | to_str Void case 返回 "none" 常量串（覆盖显式 toString 与插值脱糖 `"..." + toString(f()) + "..."`）；结果 Str，`.length` 得 4 |
| `f() == g()` / `f() != g()` | visitBinary 比较分支链首加 Void 分支：双 Void 恒 true（对齐解释器 values_equal None 分支）、Void × 非 Void 恒 false（语义层已拦 "Incomparable operand types"，此为防御性双保险）；两侧已 emit 副作用保序 |
| 范围外 | none 拼接 `"v=" + f()`（语义层两端一致拦截 "Invalid operands for string concatenation"）；`f() == 1` Void×Int（语义层拦 "Incomparable operand types"）；`f() ==? ...`（语义层拦 "Incomparable candidate value type in '==?'"）；`print(none)`/`none` 字面量（parser 两端一致 Parse error，none 非表达式）；`none n = f()` 变量声明（codegen 维持拒编 "variable type 'none'"）；tuple/数组含 none 元素、三元 none 分支（维持既有拒编）；零新增 collie_rt 接口 |

**S35 降级补充（t82 实现）：实例（Obj）相等比较**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `a == b` / `a != b`（任一侧实例） | visitBinary 比较分支在 Arr 分支之后加 Obj 分支：任一侧 Obj 且 ==/!= 时 eq = `i1 false` 常量，!= 取反（`CreateNot`）；对齐解释器 values_equal 无 Instance 分支落 default 恒 false——不同实例、同一实例 `a == a`、引用别名 `Box c = a; a == c` 全 false（值相等语义即引用恒不等价） |
| `a ==? cand: r, default` / `switch (a) {…}`（实例目标） | gen_match_eq 末尾拒编前加 Obj 分支：任一侧 Obj → `i1 false`；==? 全部候选不命中走默认结果、switch 全部 case 不命中走 default（无 default 静默跳过）；与 gen_tuple_eq 早有"任一 Obj 元素恒 false"先例一致 |
| 范围外 | 实例关系比较 `<`/`<=`/`>`/`>=`（落 require_numeric 拒编 "non-numeric operand of '<'"，解释器 runtime 亦报 "Comparison operands must be both numbers or both strings"）；tribool 混型相等（语义层两端一致拦截 "Incomparable operand types"）；实例作 tuple/数组元素相等（gen_tuple_eq/rt_arr_eq 早已恒 false）；零新增 collie_rt 接口、零结构改动 |

**S36 降级补充（t83 实现）：非常量 tuple 索引**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t[i]`（i 变量/表达式，t 同质 tuple） | visitIndex Tup 分支：`const_int_of` 失败进非常量路径——三重守卫（空 tuple / 元素非 {Int/Double/Bool/Str} / 异质分别拒编），过守卫后 rt_arr_new(n, kind) + 逐元素 elem_to_bits/rt_arr_set 物化为运行时数组、rt_arr_get(动态 idx) 取 i64 bits、bits_to_elem 还原为元素类型；负索引归一化 + 越界陷阱（消息 "Index N out of range (size M)"）由 collie_rt_arr_norm_index 复用，与解释器 normalize_index 完全一致 |
| 常量索引 `t[0]` / `t[-1]`（回归） | 保持编译期解析（`const_int_of` 成功路径内联不变），负索引归一化 + 静态越界拒编不错编 |
| 关键修复 | `const CGTuple& t` 改按值拷贝 `const CGTuple t`——非常量路径 `emit(index)` 可能触发 register_tuple 扩容 tuple_values_ 使引用悬垂（`t[idxs[0]]` 嵌套索引实测 0xC0000005），与 gen_tuple_eq 同一防护 |
| 范围外 | ~~异质 tuple 非常量索引（结果类型静态不可定，拒编 "non-constant index on heterogeneous tuple"，解释器动态可求值）；Num/嵌套(Tup/Arr/Obj)元素同质 tuple（数组槽 elem_to_bits 仅 4 类，拒编 "non-constant tuple index on this element type"）~~（数值系异质与全 Num 同质已于 S56 t107 解锁；含 Bool/Str/嵌套的异质与嵌套元素同质维持拒编）；空 tuple 非常量索引；`(t[i]==literal)`/`&&` 混逻辑（语义层不追踪 tuple 元素类型，两端一致 "Incomparable operand types"）；tuple 作函数形参/实参（"tuple in function signature"）；tuple `get()` 动态键（已于 S37 t84 解锁）；零新增 collie_rt 接口 |

**S37 降级补充（t84 实现）：非常量 tuple get() 动态键**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t.get(k)`（k 运行期字符串，t 同质命名 tuple） | visitMethodCall Tup+get 分支：键非字符串字面量进动态键路径——四守卫（空 tuple / 元素非 {Int/Double/Bool/Str} / 异质 / 无命名字段分别拒编）后 emit(key) 校验 Str，rt_arr_new 物化 names 数组（kind 3 string，无名元素存空串 ""）+ values 数组（elem kind，逐元素 elem_to_bits），新接口 `collie_rt_tuple_get(names, vals, key)` 按非空名 strcmp 扫描：命中返对应 values 槽 i64 bits（bits_to_elem 还原元素类型），未命中打 "Undefined tuple field '<key>'" + exit(1)（核心消息对齐解释器 RuntimeError，位置前缀缺失同 S36 越界陷阱既定分歧）；空名槽 rt 侧 `name[0] != '\0'` 天然跳过（对齐解释器非空名匹配） |
| 常量字符串键 `t.get("a")`（回归） | 保持编译期解析（t68 路径不变），未命中静态拒编 "undefined tuple field 'X'" 不错编 |
| 悬垂防护 | `const CGTuple t` 按值拷贝——动态键路径 `emit(key)` 可能触发 register_tuple 扩容 tuple_values_ 使引用悬垂，与 S36 visitIndex/gen_tuple_eq 同一防护 |
| 范围外 | 异质 tuple 动态键（拒编 "non-constant get() on heterogeneous tuple"）；无命名字段 tuple（"non-constant get() on tuple with no named fields"）；空 tuple（"non-constant get() on empty tuple"）；Num/嵌套(Tup/Arr/Obj)元素（"non-constant tuple get() on this element type"）；非 string 键（"non-string tuple get() key"，解释器亦运行期报错）；新增 1 个 collie_rt 接口 |

**S38 降级补充（t85 实现）：嵌套数组**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `[[1, 2], [3, 4]]` 嵌套字面量 | visitArrayLiteral：全 Arr 元素放行为 kind 4 数组（槽存内层数组 ptr 位模式，elem_to_bits PtrToInt 同 Str kind 3 先例）；~~两守卫——内层 elem==Arr 拒编 "array nesting deeper than two levels"（限两层）、内层 elem ∉ {Int/Double/Num} 拒编 "nested array with non-numeric inner elements"（保动态域不变量 kind∈{0,1}）~~（t89 解除：任意 elem 内层数组放行，见 S42）；混合 Arr/非 Arr 元素落既有异质拒编 |
| `m[i]` 内层读 / `m[i][j]` 逐层读写 | visitIndex object.elem==Arr 分支：rt_arr_get 取 bits 后 IntToPtr 还原内层数组 ptr，elem 记 **Num 动态域哨兵**——内层索引读写/print/len 全走 t70 既有动态域机制（rt_arr_kind + make_num 读、rt_arr_set_num 写）零新码；`m[0].length` 链式天然可用 |
| `m[i] = [5, 6]` 整槽替换 | visitIndexAssign object.elem==Arr 分支：~~值限数值系内层数组（v.type==Arr 且 v.elem ∈ {Int/Double/Num}）~~（t89 放宽：任意元素内层数组，见 S42），elem_to_bits PtrToInt 写 rt_arr_set；非数组值拒编 "array element type mismatch in index assignment" |
| print / toString / == | rt_arr_to_str 加 case 4（指针位模式还原后递归转串，string 改显式 case 3）；rt_arr_eq 同 kind 分支加 kind 4 递归深比较（kind 4 × 其它 kind 落既有恒不等） |
| 范围外 | ~~≥3 层嵌套与内层 bool/str 数组（拒编不错编）~~（t89 解锁，见 S42）；~~嵌套数组进函数签名/类字段/返回值（t70/t71 既有守卫 elem 限数值系天然拒编）~~（t88 透传解锁，索引读落 CG9 陷阱，见 S41）；嵌套数组元素进 tuple（物化/gen_tuple_eq 既有拒编）；零新增 collie_rt 接口 |

**S39 降级补充（t86 实现）：类继承向上转型（upcast）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 对象布局 | register_class_layout：`cls.id = classes_.size()`（注册序分配类 id），StructType 元素 0 固定 i64 类 id 头部，字段 GEP 下标 = 逻辑下标 + 1（visitNew/visitProperty/visitPropertyAssign 三处 +1）；visitNew malloc 尺寸从 8 起算并写入 `store i64 cls.id`；子类字段布局 = 父类前缀 + 追加（t61 既有），GEP 前缀兼容使父类静态类型直接读子类实例字段天然正确 |
| `f(dog)` / `return dog` / `Animal a = dog` / 字段槽 | coerce_call_arg / visitReturn / coerce_for_slot / visitVarDecl(IDENTIFIER 初始化) / visitAssign(Obj) 五触点：`cls 不等 && !is_subclass_of(实际, 声明) && !is_subclass_of(声明, 实际)` 才拒编（t103 扩）——同一继承树内 upcast/downcast 均放行（解释器 coerce_to_declared 对类不校验），槽静态 cls 记声明类；跨树维持拒编；is_subclass_of 沿 super 链上溯判真后代（自身不算） |
| `a.speak()`（a 静态类 Animal） | visitMethodCall Obj 分支（t103 重构）：收集与静态 cls 同一继承树且 dispatch 含该方法的全部定义者（含静态类/祖先/旁支，同树判定用 nearest_common_ancestor 非空；按 id 排序保 IR 确定性）——单类树直调既有单态化实例（零开销，行为不变）；多类树先防御校验各定义者签名一致（ret/param 的 type+cls 全等，否则拒编 "overriding method with a different signature"），再 `load i64` 读头部 id 后 `switch`（case=各定义者 arm，**default=运行期陷阱**——t103 后动态类可为无此方法的祖先/旁支，t86 期 default 直走静态类臂的前提不再成立；toString 且签名 () string 时 default 改返 "<object>" 内建兜底，对齐解释器 find_method 优先顺序），每 arm `call collie.<分派类>.<定义类>.<方法名>` 后 br 合流块，非 Void 返回值 PHI 合并；单态化副本使模板方法体内 this.m() 经分派后天然按动态类解析，base.m() 静态绑定不受影响 |
| 调用点返回值 | visitCall/visitMethodCall 结果 cls 记 info.ret_cls（声明类）——返回父类装子类后再调用仍走动态分派，天然正确 |
| `a == b`（Obj 相等） | S35 恒 false 常量折叠不动——解释器 values_equal 无 Instance 分支恒 false（含 a==a），upcast 不改变该语义 |
| `a.fetch()`（静态类无此方法但树内其他类有） | 未命中路径（t102，S39 残余面解锁；t103 收集面扩至全树）：收集 dispatch 含该方法的同树定义者（含祖先/旁支，按 id 排序），空时 toString 无参走内建兜底、其余维持拒编；非空先防御各定义者副本签名一致（同 t86 模式），arity 检查 + 实参 coerce 同命中路径，读头部 id switch（case 各定义者调单态化实例、**default=运行期陷阱** `collie_rt_trap_undefined_method(name)` + unreachable，动态类实例无此方法时 exit(1)，消息核心对齐解释器 "Undefined method 'X' on object"；name==toString 且签名 () string 时 default 改返 "<object>" 内建兜底），非 Void 返回值 PHI 合流 |
| `a.breed` 读 / `a.breed = v` 写（字段访问） | 读写两路径统一重构（t102/t103）：收集 field_index 含该字段的同树定义者（含静态类/祖先/旁支，各定义者按自身 field_index 下标 GEP），空维持拒编；单类树直 GEP+load/store 零开销（现状保持）；多类树先防御各定义者字段类型一致（type/cls/bit_max 全等，否则拒编 "inherited field with a different type"），读路径 switch 各定义者字段槽 + default 陷阱 `collie_rt_trap_undefined_property(name)`，PHI 合流；写路径值在 switch 前求值/coerce/bit 陷阱（求值顺序 object→value 不变），各 arm store，default 陷阱 |
| 范围外 | 跨树互赋（无公共祖先，拒编 "initializing 'B' variable with incompatible value" 等五触点消息）；~~downcast（父类实例赋子类变量）~~（t103 解锁：同树五触点放行，成员访问动态分派，动态类无该成员落运行期陷阱）；~~父类静态类型调子类特有方法（拒编不错编陷阱面）~~（t102 解锁）；覆写变签名（防御拒编）；collie_rt 陷阱接口 `collie_rt_trap_undefined_method` / `collie_rt_trap_undefined_property`（t102 增，t103 零新增复用） |

**S40 降级补充（t87 实现）：byte/word 类字段**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `public byte level = 16;` 字段声明 | register_class_layout 字段类型 KW_BYTE/KW_WORD 前置分支（IDENTIFIER 分支后、declared_cgtype 之前）：ftype = Int（i64 槽 8 字节，visitNew 尺寸累计不变）+ CGField.bit_max 记 255/65535（0 即非位类型）；继承字段随父类前缀拷贝，bit_max 天然保持 |
| `new Pixel()` 字段初始化 | visitNew 字段初始化循环：coerce_for_slot 后 `field.bit_max > 0` 插 check_bit_range（t69 机制复用，无符号比较一次覆盖负数与超上限，越界 rt_trap_bit_range 报 "Value out of range for 'byte' (expected 0-255, got N)"，与解释器 coerce_to_declared 消息一致） |
| `p.level = v` / `this.level = v` 字段赋值 | visitPropertyAssign：coerce_for_slot 后同插 check_bit_range（构造器/方法体内 this.field 赋值同路径） |
| `p.level` 字段读出 | visitProperty 恒 Int 无需改——表达式域无截断（`p.level + 100` 可越过声明范围），对齐 t69 变量语义 |
| 范围外 | byte/word 进函数签名（语义层 "No matching overload" 拦截，两端一致非差分面）；非 integer 初始值/赋值（语义层双端同拦）；零新增 collie_rt 接口 |

**S41 降级补充（t88 实现）：bool/string 数组动态域透传**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `f(flags)` / `return flags` / `obj.f = flags` / 动态槽赋值 | t70/t71 四守卫全部解除（coerce_call_arg / visitReturn / coerce_for_slot / visitAssign 动态槽来源限制）：任意元素数组（bool kind 2 / str kind 3 / 嵌套 kind 4）指针透传，kind 随数组对象自带（对齐解释器 coerce_to_declared KW_ARRAY 只查"是数组"不查元素类型） |
| print / length / len / == / toString | 零改动天然安全：rt_arr_to_str（case 2/3/4 已覆盖）、rt_arr_eq（kind 2/3/4 已覆盖，跨 kind 恒不等）、rt_arr_len（kind 无关） |
| `a[i]` 动态域读（elem==Num） | kind 0/1 即 number tag 直拼 Num 零转换（t70 路径不变）；kind ≥ 2 元素静态类型不可定——`icmp ugt kind, 1` → dynkind.trap 调新接口 `collie_rt_trap_arr_kind(kind)` + unreachable（**新缺口 CG9**：解释器动态类型可行、编译产物陷阱退出不错值，消息 "reading bool/string/nested/object array element in dynamic context"，kind 5=object 于 S53 t100 加入） |
| `a[i] = v` 动态域写（elem==Num） | v 为 Bool/Str 直写放行：打对应 kind tag（arr_kind_of 2/3）+ elem_to_bits 下沉 rt_arr_set_num——tag==kind 直存天然覆盖、0→1 提升保留、其余 mismatch 落 CG7 陷阱（消息泛化为 "array element type mismatch"，原 "decimal element in integer array" 是其子集） |
| 范围外 | ~~嵌套数组字面量内层 bool/str（visitArrayLiteral 守卫不动，拒编）~~（t89 放宽，见 S42）；动态域索引读出 bool/string/嵌套元素（CG9 陷阱面，候选后续任务）；collie_rt 新增 1 个陷阱接口 `collie_rt_trap_arr_kind` |

**S42 降级补充（t89 实现）：嵌套数组放宽（内层 bool/str + ≥3 层）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `[[true], ["a", "b"]]` / 三层以上嵌套字面量 | visitArrayLiteral 两守卫解除（"array nesting deeper than two levels" 与 "nested array with non-numeric inner elements" 删除）——任意 elem 内层数组（Bool/Str/Arr）放行进 kind 4 槽，kind 随内层对象自带（arr_kind_of），elem_to_bits Bool zext / Str·Arr PtrToInt 全覆盖 |
| `m[i] = [任意内层]` 整槽替换 | visitIndexAssign elem==Arr 分支条件放宽为仅 `v.type != Arr` 拒编（"array element type mismatch in index assignment"）——非数组值写 kind 4 槽拒编不错编（解释器可行，活跃拒编面）；任意元素内层数组放行 |
| `bs[i][j]` 内层元素动态域索引读 | visitIndex 零代码改动：内层读出记 Num 动态域哨兵（t85 机制），内层 kind ≥ 2（bool/str/更深嵌套）索引读落 t88 既有 CG9 陷阱（collie_rt_trap_arr_kind）不错值；数值系内层逐层读写照常 |
| print / len / == / 深比较 | rt 侧零改动：rt_arr_to_str case 4 递归、rt_arr_eq kind 3 strcmp / kind 4 递归 / 跨 kind 恒不等，深度无关天然覆盖 |
| 范围外 | 内层元素经动态域索引读出（CG9 陷阱面，候选后续任务）；~~Num 元素数组字面量（候选后置）~~（t90 解锁，见 S43）；零新增 collie_rt 接口 |

**S43 降级补充（t90 实现）：Num 元素数组字面量**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `[x, y, 3]`（x/y 为 number）字面量 | visitArrayLiteral 数值系同质判定扩展：元素 CGType ∈ {Int/Double/Num} 互混时 elem 统一提升 Double（Num 运行期 tag 静态不可判，double 视图承载）；全 Num 字面量循环后兜底 elem=Num→Double；存槽循环 Int/Num 元素 to_double 提升——print 输出与解释器混合表示一致（rt format_f64 整数值省 .0） |
| Num → double 转换 | to_double 加 Num 分支：tag==0 ? SIToFP(bits) : BitCast(bits)，两分支无副作用 select 免分支 |
| `a[i] = n`（n 为 Num，a 槽 Int/Double） | visitIndexAssign 静态数值槽收 Num 值：tag 运行期定，下沉既有 rt_arr_set_num 按槽 kind 对齐——tag==kind 直存、0→1 提升、1→0 失配落 CG7 陷阱（小数态 Num 写 int 槽解释器动态异质可容 vs 产物陷阱退出不错值，陷阱实证面） |
| 范围外 | 超大整数 Num 进数组 double 视图丢精度（Int→Double 混合提升既有同域先例，CG1 缺口范畴）；无初始化变量绑 none（候选 D 后置）；位运算 Num 整数态非差分面（语义层 "Bit operands expected" 两端一致拦截，预检实证排除）；零新增 collie_rt 接口 |

**S44 降级补充（t91 实现）：嵌套函数声明（受限雷姆达提升）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `function outer() { function inner() {...} ... }` | 雷姆达提升为模块级函数：declare_function 加 prefix 缺省参，注册键改编 `outer.inner`（用户标识符无 '.' 天然防冲突），符号名 `collie.outer.inner`；尾部 declare_nested_in 递归下探 Block/If/While/For/DoWhile/Switch 全语句子树，命中 FunctionStmt 建原型并登记 nested_fns_（FunctionStmt* → 改编键） |
| 声明处可见性 | visitFunction 嵌套路径：nested_fns_ 查表命中即在声明处向 scopes_.back() 登记 CGVar{fn_key=改编键}（对齐解释器"执行到声明处 env_.define"——声明前调用不可见、所在块退出即失效）；生成现场 in_function_/current_ret_type_/current_ret_cls_ 保存恢复（嵌套生成完还原外层函数上下文） |
| 嵌套体内可见集 | 链底 = 全局层拷贝 + 外层链上全部 fn_key 绑定拷入（自身递归/前置兄弟嵌套可见，对齐动态作用域"声明先于调用即可见"）；变量槽不拷——引用外层局部即标识符不可见拒编（捕获面范围外，实证：解释器 42 vs 拒编 "identifier 'captured'"） |
| `inner()` 调用解析 | visitCall 三级顺序：内建 → 作用域链函数绑定（lookup_var fn_key 非空 → functions_[fn_key]）→ 顶层 functions_[fname]（嵌套绑定遮蔽顶层同名，对齐解释器 env 由内向外解析） |
| 范围外 | 嵌套体引用外层局部（捕获，拒编不错编）；~~类方法体内嵌套函数（declare-pass 不下探方法体，维持 "nested function declaration" 拒编）~~（t104 解锁，见下方补充）；函数名作值/被赋值（"function 'f' used as a value" / "assignment to function"，非一等公民）；同外层同名嵌套（语义层 "already defined" 双端拦截非差分面）；零新增 collie_rt 接口 |

**S44 降级补充（t104 实现）：类方法体内嵌套函数**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class C { function m() { function inner() {...} ... } }` | register_class_methods 自身方法循环（含构造器）尾部对方法体逐语句 declare_nested_in，改编键前缀 `<定义类>.<方法名>`（如 `Calc.fact.f`，符号 `collie.Calc.fact.f`，同 t91 规则；与顶层同名在语义层即拒绝）；继承副本共享同一 FunctionStmt，仅定义类注册一次 |
| 单态化副本重访 | 同一方法体按分派类多次生成，visitFunction 嵌套路径加重访守卫：`info.fn` 非空（首个副本已生成完整函数体）时仅登记可见性绑定即返——嵌套体是独立函数，行为与分派类无关 |
| this 上下文隔离 | visitFunction 生成现场保存/清空/恢复 current_this_/current_class_name_/current_defining_class_：嵌套体内 this/base 落 "'this' outside class method" 拒编不错编（同 t91 外层局部捕获面；不清空会跨函数引用方法 this 实参生成非法 IR，解释器动态作用域可运行为既定拒编差分） |
| 范围外 | 嵌套体引用 this/字段/方法形参/方法局部（捕获面，拒编不错编，同 t91）；零新增 collie_rt 接口 |

**S45 降级补充（t92 实现）：无初始化变量声明**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `integer/decimal/bool/string x;` | 槽照常创建（顶层零初始化 GlobalVariable / 函数内 alloca 不预存），CGVar 记 `uninit=true` + `decl_depth=scopes_.size()`；解释器语义为绑 none，但读被静态拒编故槽初值不可观测 |
| 读 uninit 变量 | visitIdentifier 拒编 "use of uninitialized variable"——语义层 use-before-init 检查流不敏感（分支/循环内赋值后读会放行），解释器该场景运行期输出 none，零初始化槽会错出 0——拒编不错编 |
| `x = v` 后续读 | visitAssign 通用路径 store 后仅当 `scopes_.size() == decl_depth`（同块直线区域，块内顺序执行保证运行期先于后续读）清 uninit 放行；深层块（分支/循环体）赋值存值但不清标记 |
| 全局无初始化 + 函数体内读 | 函数体链底快照拷贝全局层时 uninit 状态随 CGVar 拷贝：声明后已同块赋值的全局在函数内可读，未赋值的保守拒编 |
| 范围外 | ~~byte/word/number/tribool~~（S49 t96 一并放行）/~~数组/类类型~~（S54 t101 放行）/Tuple 无初始化维持拒编；分支/循环块内赋值后读拒编不错编（实证：if(false)/while(false) 内赋值后读解释器 none vs 拒编）；零新增 collie_rt 接口 |

**S46 降级补充（t93 实现）：三元/==? 分支实例类型统一到最近公共祖先**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `c ? new B() : new A()`（B extends A） | gen_ternary 分支类型统一：Obj cls 不等改求 nearest_common_ancestor（沿 super 链，含自身端点——a 即 b 祖先返 a、反之返 b、否则自 a 父链找首个 b 的祖先），result cls 累计取 NCA；Obj 的 LLVM 表示统一指针，PHI 无关 cls，值本身零转换 |
| `k ==? 1: new B(), new A()` 多支合流 | match 合流同规则扩展到 N+1 支（result_cls 逐支累计 NCA，兄弟类→公共父、孙类/兄弟类→更高祖先） |
| 合流值方法调用/字段读 | t86 机制天然正确：对象头类 id + visitMethodCall 动态分派按运行期真实类解析覆写方法；字段走父类前缀布局，祖先静态类型读偏移一致 |
| 无公共祖先两类合流 | 维持拒编 "ternary/'==?' branches yield instances of different classes"（解释器动态类型同签名方法可行，实证：解释器 X vs 拒编——拒编不错编） |
| 范围外 | ~~Arr elem 不同维持拒编~~（S47 t94 统一动态域）；其余混型统一规则（数值系/Tri-Bool）不动；零新增 collie_rt 接口 |

**S47 降级补充（t94 实现）：三元/==? 分支数组元素类型合流统一动态域**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `c ? ints : decs`（elem 不同数组） | gen_ternary 分支类型统一：Arr elem 不等改统一 elem=Num 动态域哨兵（t70/t88 既有机制）——数组值同为不透明 ptr，PHI 无关 elem，值本身零转换；result_elem 逐支累计（同 t93 result_cls 模式） |
| `k ==? 1: a, 2: b, c` 多支合流 | match 合流同规则扩展到 N+1 支（任一支 elem 不等即降 Num 动态域） |
| 合流值消费 | 动态域机制天然正确：kind 随数组对象运行期自带——print/toString/len/== rt 侧全 kind 覆盖（rt_arr_to_str/rt_arr_len/rt_arr_eq）；索引读数值系拼 Num 正常、kind ≥ 2（str/bool/嵌套）落既有 CG9 陷阱不错值（实证：Str/Int 合流索引读解释器 a vs 产物陷阱退出）；合流值为新鲜值，元数据自诞生即动态，无程序序失配 |
| 数组变量再赋不同 elem | 维持拒编 "assigning array with different element type"（活跃差分面但后置：程序序提升 var->elem 在循环回边——先读后赋再回读——与函数全局快照静态解码下会错编，需循环深度守卫+捕获跟踪） |
| 范围外 | 数组变量/类字段/tuple 槽再赋不同 elem 维持拒编；零新增 collie_rt 接口 |

**S48 降级补充（t95 实现）：三元/==? 分支 tuple 合流静态展开**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `c ? t1 : t2`（同形状 tuple） | gen_ternary Tup 路径改走 merge_tuple_arms 三阶段：①元数据递归校验形状（元素数+名字表+嵌套位置全支同为 tuple）并合并各叶位类型（复用标量合流规则——数值提升 Num/Double、Tri/Bool 加宽、Arr elem 不等降 Num 哨兵 t94、Obj cls 求 NCA t93）；②逐支把叶值对齐指令落该支末块内（DFS 序展平）+ Br；③merge 块按同一 DFS 序逐叶 PHI，自底向上 register_tuple 重建新鲜 CGTuple |
| `k ==? 1: t1, 2: t2, t0` 多支合流 | match 合流同规则扩展到 N+1 支（tribool 三分支三元同路径） |
| 合流值消费 | 新鲜 CGTuple 与字面量产物同构——常量索引/命名字段/get/length/print/相等比较/存 Tuple 槽/再合流全部复用既有静态展开链路，零新增消费面 |
| 嵌套 tuple 合流 | 嵌套位递归合流（子 tuple 先 PHI 重建，父元素指向新子条目）；任一支嵌套位非 tuple 即形状不一致拒编 |
| 形状/名字不一致 | 维持拒编 "ternary/'==?' branches yield tuples of different shapes"（实证：元素数 2 vs 3、名字 name vs age——解释器可跑 vs 拒编不错编）；元素不可合并（如 Str×Int 同位）拒编 "yield tuples with incompatible elements" |
| 范围外 | tuple 进函数签名/进数组维持拒编（形状跨调用点不定，需按形状特化）；异质数组字面量维持拒编，~~实例进数组维持拒编~~（同类实例进数组已于 S53 t100 解锁）（预检活跃差分面，后置候选）；零新增 collie_rt 接口 |

**S49 降级补充（t96 实现）：无初始化变量声明放行面扩展**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `number n;` | 槽照常创建（Num 承载 struct{i64,i64}），零初始化 GlobalVariable/alloca 合法常量；CGVar 记 uninit + decl_depth，读拒编保证零值不可观测——沿用 S45 t92 机制 |
| `tribool tb;` / `char c;` / `character d;` | Tri（i8）/Str（ptr）同理零初始化槽合法；char/character 经 declared_cgtype 映射 Str 承载 |
| `byte by;` / `word wd;` | i64 承载零初始化 + CGVar.bit_max（255/65535）；赋值走 visitAssign 通用路径——既有赋值点 check_bit_range 陷阱自动生效，不丢范围校验（实证：范围内赋值两端一致） |
| 赋值后读 / 全局函数体内读 / 循环体内声明 | 复用 S45 t92 的 uninit/decl_depth/同块清除与链底快照机制，六新类型零新增控制流 |
| 范围外 | ~~array（elem 无从推断）/类类型~~（S54 t101 放行——array 建 opaque ptr 槽 + elem=Num 动态域哨兵、类类型建 Obj 槽 + cls）/Tuple（形状无从推断）无初始化维持拒编；分支/循环块内赋值后读维持拒编不错编（实证：if 块内赋值 byte 后读——解释器输出值 vs 拒编 "use of uninitialized variable"）；零新增 collie_rt 接口 |

**S50 降级补充（t97 实现）：byte/word 函数返回类型**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `function makeWord() word { return 40000; }` | declare_function 顶层函数返回类型识别 KW_BYTE/KW_WORD → CGType::Int 承载（i64） + CGFunction.ret_bit_max（255/65535，缺省 0 保兼容非位返回）；LLVM 函数返回 i64，值层面与 integer 返回同构 |
| 返回值范围校验 | visitFunction 保存/设置/恢复 current_ret_bit_max_（同 current_ret_type_ 保存恢复模式，嵌套函数上下文安全）；visitReturn 值对齐 Int 后 `ret_bit_max > 0` 时插 check_bit_range——越界调 rt_trap_bit_range 报 "Value out of range for 'byte/word' (expected 0-max, got N)" + unreachable（复用 t69 机制，对齐解释器 coerce_to_declared 返回值校验，核心消息一致，位置前缀缺失同既定 CG 陷阱分歧）；多返回路径逐 return 各插一次 |
| 返回值消费 | 结果为普通 Int 表达式域（无 bit_max）：参与算术/比较照常；存 byte/word 变量再走声明点或赋值点 check_bit_range；print 直接整数输出 |
| 范围外 | ~~byte/word 作函数参数维持拒编~~（S51 t98 已放行——预检证伪原"调用点范围陷阱"假设：重载解析保证实参恒为已校验 byte/word 值，coerce_call_arg 零改动）；~~类方法返回 byte/word 走 declared_signature_type 维持拒编~~（S52 t99 已放行）；零新增 collie_rt 接口 |

**S51 降级补充（t98 实现）：byte/word 函数参数**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `function inc(b byte) byte { ... }` | declare_function 参数循环识别 KW_BYTE/KW_WORD → CGType::Int 承载（绕过 declared_signature_type 的 "variable type 'byte/word'" 拒编）；LLVM 形参 i64，签名层面与 integer 形参同构 |
| 形参绑定 | visitFunction 落槽绑定改用具名 CGVar binding，byte/word 形参置 bit_max=255/65535——体内重赋 `b = b + 100` 越界走既有 visitAssign 赋值点 check_bit_range 陷阱（实证：实参 200 时 codegen 产物运行期 trap "Value out of range for 'byte' (expected 0-255, got 300)" 核心消息对齐解释器），返回 `b + 1` 越界走 visitReturn t97 陷阱 |
| 调用点实参 | coerce_call_arg 零改动——重载解析要求实参类型即 byte/word（整数字面量 `addb(200)`/byte 算术 `addb(x+100)` 双端语义错 "No matching overload"，非差分面），实参只能是 byte/word 变量（声明/赋值点已校验）或 byte/word 函数返回（visitReturn t97 已校验），恒在范围内无需调用点陷阱（类型系统天然保证，t97 遗留"调用点范围陷阱"假设经预检证伪） |
| 范围外 | ~~类方法/构造器 byte/word 参数走 declared_signature_type 维持拒编~~（S52 t99 已放行）；零新增 collie_rt 接口 |

**S52 降级补充（t99 实现）：byte/word 类方法/构造器参数与返回**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `function getLevel() byte { ... }`（类方法返回位） | 方法注册循环返回类型识别 KW_BYTE/KW_WORD → CGType::Int 承载 + info.ret_bit_max=255/65535（复用 CGMethod 继承自 CGFunction 的 t97 ret_bit_max 字段，绕过 declared_signature_type）；gen_method_body 现场保存/复位 current_ret_bit_max_（设 method.ret_bit_max、复位 0），visitReturn t97 陷阱自动生效覆盖方法/构造器返回越界 |
| `function bump(delta byte) none { ... }` / `Meter(start byte) { ... }`（参数位） | 方法注册循环参数识别 KW_BYTE/KW_WORD → CGType::Int（llvm 形参 i64）；gen_method_body 形参落槽绑定处读 AST 形参声明类型（stmt.parameters()[i-1].type），byte/word 时先 check_bit_range 再 store 并置 CGVar.bit_max（体内重赋走赋值点陷阱） |
| 绑定点范围陷阱（对照 t98 顶层函数） | 方法/构造器为单签名按名解析无重载拦截，整数字面量实参（`c.setb(300)`/`new C(300)`）可达绑定点——解释器绑定时 coerce_to_declared 校验、越界运行期 trap，故形参必须在绑定点插 check_bit_range（统一覆盖方法/构造器/base 全调用路径）；实证：越界字面量实参 codegen 产物运行期 trap "Value out of range for 'byte' (expected 0-255, got 300)" 核心消息对齐解释器 |
| 调用点实参 | coerce_call_arg 零改动（实参 Int 直传，范围校验在被调方绑定点） |
| 范围外 | 方法调用结果静态类型为 object（is_comparable_type 无 object 放行）不能直接参与 ==/!= 比较、word 值不能 return 给 byte 返回类型——属解释器语义边界（非 codegen 拒编面）；异质数组字面量/实例进数组/tuple 进函数签名维持拒编（活跃差分面后置候选）；零新增 collie_rt 接口 |

**S53 降级补充（t100 实现）：类实例进数组（同类，kind 5）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `array zoo = [a1, a2]`（元素同类实例） | visitArrayLiteral 全 Obj 元素放行为 kind 5 数组（槽存实例指针位模式，elem_to_bits PtrToInt 同 Str kind 3 先例）；局部 elem_cls 追踪首元素类名（复用 CGValue.cls，Arr 时表元素类），混合类守卫 `v.cls != elem_cls` 拒编 "heterogeneous array literal (mixed classes)"（后置）；last_value_ 携 elem_cls |
| `zoo[i]` 本地静态读出 | visitIndex object.elem==Obj 分支：rt_arr_get 取 bits 后 IntToPtr 还原实例 ptr，last_value_ 追加 object.cls 传播（identifier load 已携 var->cls）——`zoo[i].name` 字段读 / `zoo[0].sound()` 方法调用（按对象头类 id 动态分派 t86）全走 t61 既有 Obj 路径，本地 array 变量保留 elem=Obj 走静态路径（非动态陷阱） |
| `zoo[i] = v` 整槽写 | visitIndexAssign 静态写路径末补 Obj cls 兼容守卫：`v.cls != object.cls && !is_subclass_of(v.cls, object.cls)` 拒编 "array element class mismatch in index assignment"——同类或子类 upcast 放行（同 coerce_call_arg），异类拒编；子类 upcast 整槽写后读出方法按运行期真实类动态分派 |
| 变量声明/重赋值 | visitVarDecl array 分支拷贝 init.cls 入 CGVar；visitAssign 整数组重赋值补 Obj 异类守卫 `v.cls != var->cls` 拒编 "assigning array with different element class" + last_value_ 携 cls |
| print / toString / == | rt 三触点加 kind 5——arr_to_str case 5 `<object>`（对齐 Value::to_string Instance）、arr_eq 同 kind 5 恒返 0（对齐解释器 values_equal 无 Instance 分支实例恒不等，含同实例/同数组自比较）、trap_arr_kind kind 5→`object`（动态域读出兜底）；`==`/print 按 Arr 类型泛化下沉无 elem 守卫天然覆盖 |
| 范围外 | 混合类实例数组字面量/整槽写异类维持拒编；动态域（数组过签名/字段/返回值）obj 元素读出维持 CG9 陷阱不错编（元素类型静态不可定，trap_arr_kind kind 5→object）；零新增 collie_rt 接口 |

**S54 降级补充（t101 实现）：无初始化 array/类类型变量声明**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `array a;` 无初始化声明 | visitVarDecl 无初始化分支加 KW_ARRAY 前置分支（静态类型放行块后、终点 unsupported 前）：槽照常创建（opaque ptr，顶层零初始化 GlobalVariable / 函数内 alloca），CGVar 记 elem=Num 动态域哨兵（t70——elem 无从静态推断，后续赋值走 visitAssign Arr 动态域透传）+ uninit + decl_depth；解释器语义绑 none，读被静态拒编故槽初值不可观测 |
| `Cat c;` 无初始化声明（c 为已注册类名） | visitVarDecl 加 IDENTIFIER 分支（`classes_.count(类名)` 守卫）：Obj 槽照常创建，CGVar 记 cls=声明类名 + uninit + decl_depth；后续赋值走 visitAssign Obj 分支，读出属性链/方法调用/upcast 动态分派全走 t61/t86 既有路径 |
| `a = […]` / `c = new Cat(...)` 同块赋值 | visitAssign 的 Arr 分支与 Obj 分支原 CreateStore 后即 `return` 早退，**不经通用路径 t92 的 uninit 清除逻辑**——故两分支各补同块清除 `if (var->uninit && scopes_.size()==var->decl_depth) var->uninit=false`；深层块（分支/循环体）赋值存值不清标记（流不敏感保守，同 t92） |
| 读 uninit / 深层块赋值后读 | visitIdentifier 读 uninit 拒编 "use of uninitialized variable"（复用 t92）；分支/循环块内赋值后读拒编不错编（实证：无初始化 array 仅 if 块内赋值后读——解释器 `[1,2,3]` vs 拒编 deep-block 保守） |
| 范围外 | 无初始化 Tuple（形状无从推断——元素数/名字表静态不可知，解构槽组无从建，维持拒编 "variable declaration without initializer"，实证）；array 动态域索引读 kind ≥ 2 落既有 CG9 陷阱（elem=Num 哨兵，同 t70/t88）；零新增 collie_rt 接口 |

**S55 降级补充（t106 实现）：数组变量槽异型互赋**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `array a = [1]; a = ["x"];` 异型互赋 | visitAssign Arr 分支：v.elem != var->elem 时槽 **elem 降级 Num 动态域哨兵**（t94 三元合流先例）——整数组指针替换，kind 随新对象自带，后续读写/print/len/== 走 t70/t88 动态路径；降级同时清 var->cls（Obj 元素类伴随失效） |
| 循环回边守卫 | CGVar.loop_depth 记录声明时 `loops_.size()`（仅 while/for/do-while 压栈），赋值时层级不等拒编 "inside a loop"——否则赋值点之前已按旧 elem 生成的静态读代码在第二轮迭代解码错值（程序序提升 var->elem 的固有盲区） |
| 全局槽跨函数快照守卫 | fn_gen_count_ 计数器（visitFunction/gen_method_body 各 ++）+ CGVar.fn_gen_at 声明时基准：GlobalVariable 槽且（in_function_ 或计数增长）拒编 "across functions"——函数体生成时链底按值快照声明在先的全局槽静态 elem，降级不回溯/不外传；声明在全部函数之后的全局槽不受影响 |
| 降级后索引读 | 数值 kind 走动态路径；kind ≥ 2（bool/str/嵌套/obj）落既有 CG9 陷阱（拒错编从陷阱，整体 print/len/== rt 全 kind 覆盖不受限） |
| 范围外 | Obj 元素类不匹配面维持拒编（t100 同类约束）；tuple 槽 elem 不一致维持拒编；零新增 collie_rt 接口 |

**S56 降级补充（t107 实现）：数值系异质 tuple 非常量索引**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t[i]`（i 变量/表达式，t 元素全 ∈ {Int/Double/Num}） | visitIndex Tup 非常量路径新增数值系分支（含全 Num 同质——原 "this element type" 拒编面一并解锁）：逐元素 to_num 后物化 **tags+bits 双 int 数组**（均 kind 0），同一动态索引两次 rt_arr_get 取回 make_num 拼 Num 动态值；负索引归一化/越界陷阱在首次 get（消息同 t83 既定分歧：核心一致、位置前缀缺失）；结果型 Num，下游算术/比较/print/toString 走既有 Num 路径 |
| 同质 4 类（回归） | 保持 t83 静态元素类型路径（单数组 elem_to_bits/bits_to_elem）不变；常量索引编译期解析不受影响 |
| 守卫重构 | 原三重守卫改为 homogeneous/all_numeric 双标志一次扫描：同质非 4 类且非数值系→"this element type"；异质且非全数值系→"heterogeneous tuple"；两条既有消息分工保留 |
| 范围外 | 含 Bool/Str/嵌套(Tup/Arr/Obj)的异质与嵌套元素同质维持拒编（结果类型静态不可定且无统一表示）；空 tuple 维持拒编；get() 动态键的同一面（数值系异质/全 Num 同质）未同步解锁（待后续任务，机制同源可复用 tags+bits 物化）；零新增 collie_rt 接口 |

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
| CG2 | print 标量格式已对齐解释器（t53 collie_rt 垫片）；数组格式已对齐（t59 arr_to_str）；tuple 格式已对齐（t68 tuple_to_str 静态展开）；none 值 print/toString/插值已对齐（t81 常量串 "none"）；对象仍固定 "\<object\>" | 随对应类型的 codegen 支持扩展 collie_rt 接口 |
| CG3 | 运行期类型校验（coerce_to_declared 五处）在编译产物中缺失 | 语义层静态保证覆盖的部分可省；动态部分（object/窄化）随 collie_rt 补 |
| CG4 | 仅支持 x86_64-pc-windows-msvc target | CI 矩阵起来后加 Linux target；LLVM 包已含全部 target 后端 |
| CG6 | 拼接/转串结果 malloc 后不 free，编译产物存在内存泄漏 | 短生命周期进程暂容忍；后续随 string 运行时成熟引入引用计数或 arena 分配器 |
| CG7 | 动态域（数组经函数签名边界）decimal 写 integer 数组陷阱退出（t70：解释器数组动态异质可容，编译产物同质 8 字节槽无法承载，拒错编从陷阱不静默错值） | 异质数组降级支持（元素 tagged 表示）时一并消除 |
| ~~CG8~~ | ~~print 逐参求值边打边走~~（t76 发现，**t77 已修复**：gen_print 两阶段化先求值全部实参再统一输出，见 S30） | 已消除 |

## 八、构建方式速查

```bash
# 配置（一次）：
cmake -S compiler -B compiler/build -DCOLLIE_ENABLE_LLVM=ON ^
      -DLLVM_DIR=D:/Program/Development/Environment/llvm-21/lib/cmake/llvm
# 冒烟构建 + 运行：
compiler\build\t48_smoke_build.cmd
compiler\build\codegen\Release\llvm_smoke.exe
```
