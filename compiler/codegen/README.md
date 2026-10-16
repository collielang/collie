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
| S32 | 数组相等比较：新接口 collie_rt_arr_eq C 层深比较（先比 len 再逐元素按运行时 kind，数值系混合 ~~double 视图~~（S106 t159 起按元素保型落 kind 7，逐元素交 `collie_rt_dyn_eq`）、string strcmp），==/!=、tuple 含数组元素、==?/switch 数组候选三触点同时解锁 | 数组 ==/!=、tuple 含数组元素、==?/switch 数组候选、跨签名动态域数组比较程序编译执行，输出与解释器一致 **✅ t79** |
| S33 | 小数取模：decimal 参与的 `%`（Double×Double / Int×Double / Double×Int）FRem + floor 修正（符号随除数，select 无分支），除零 FRem 天然 NaN | 小数取模程序（四象限符号/混合类型/除零/Infinity/%=/跨签名/数组元素）编译执行，输出与解释器一致 **✅ t80** |
| S34 | none 值（CG2 缺口收敛）：gen_print Void 打常量串 "none"、to_str Void 返 "none"（覆盖 toString/插值）、visitBinary ==/!= 双 Void 恒 true/false，零新增 rt 接口 | none 函数调用结果 print/toString/插值/相等比较/逻辑运算/条件/方法体内程序编译执行，输出与解释器一致 **✅ t81** |
| S35 | 实例（Obj）相等：visitBinary ==/!= 与 gen_match_eq（==?/switch）对任一侧 Obj 恒 false 常量折叠（对齐解释器 values_equal 无 Instance 分支落 default，含同一实例），零新增 rt 接口 | 实例 ==/!=（不同/同一实例/引用别名）、==? / switch 目标为实例、函数内实例相等、结果进逻辑/条件程序编译执行，输出与解释器一致 **✅ t82** |
| S36 | 非常量 tuple 索引：同质 tuple（元素同 CGType 且 ∈ {Int/Double/Bool/Str}）的 `t[i]`（i 变量/表达式）物化运行时数组后 rt_arr_get(动态 idx) 取值，复用负索引归一化 + 越界陷阱（消息与解释器一致），零新增 rt 接口 | 同质 tuple 变量/表达式/负索引变量、命名同质 tuple、for 循环遍历、嵌套索引、函数内局部 tuple 程序编译执行，输出与解释器一致 **✅ t83** |
| S37 | 非常量 tuple get() 动态键：同质命名 tuple（≥1 非空名）的 `t.get(k)`（k 运行期字符串）物化 names+values 数组后新接口 collie_rt_tuple_get 按非空名 strcmp 查找，未命中陷阱消息与解释器核心消息一致 | 四类同质命名 tuple 变量键/常量键回归/混合命名+无名/拼接键/循环动态键/函数内局部 tuple 程序编译执行，输出与解释器一致 **✅ t84** |
| S38 | 嵌套数组：新增数组 kind 4（槽存内层数组 ptr 位模式），限两层且内层数值系（elem ∈ {Int/Double/Num}）——内层读出记 Num 动态域哨兵复用 t70 机制，rt_arr_to_str/rt_arr_eq kind 4 递归，零新增 rt 接口 | 嵌套字面量/print/逐层索引读写/负索引/别名联动/整槽替换/内层混合提升/深比较/length/len/toString 程序编译执行，输出与解释器一致 **✅ t85** |
| S39 | 类继承向上转型（upcast）：对象 struct 头部加 i64 类 id（注册序），字段 GEP 下标+1；实参/返回值/变量与字段槽/赋值五触点放行子类实例进父类静态类型（is_subclass_of 真后代判定）；方法调用点动态分派——静态类无后代直调零开销，有后代读头部 id 后 switch（default=静态类，case=各后代类按 id 排序），各 case 调既有单态化实例，PHI 合流，零新增 rt 接口 | 三级继承链子类传父类形参/中层静态类/返回父类装子类/父类槽覆写与继承方法混调/类字段 upcast/无后代直调程序编译执行，输出与解释器一致 **✅ t86** |
| S40 | byte/word 类字段：CGField 加 bit_max（255/65535），register_class_layout 前置分支 i64 槽承载；visitNew 字段初始化与 visitPropertyAssign 赋值两触点 coerce_for_slot 后插 check_bit_range 范围陷阱（t69 机制复用，对齐解释器 coerce_to_declared），读出恒 Int 表达式域无截断，零新增 rt 接口 | byte/word 字段初始化/读取/赋值/边界值 255与65535/表达式域无截断/构造器与方法体内 this.field 赋值/继承字段范围保持程序编译执行，输出与解释器一致 **✅ t87** |
| S41 | bool/string 数组动态域透传：解除 t70/t71 四守卫（实参/返回值/字段槽/动态槽赋值）——任意元素数组（含嵌套 kind 4）指针透传，kind 随对象自带；print/len/==/toString rt 侧全 kind 覆盖零改动；~~动态域索引读插 kind>1 运行时陷阱（新缺口 CG9，拒错编从陷阱）~~（CG9 已于 S98 t151 消除：动态域索引读按运行期 kind 产出 Dyn）；动态域索引写 Bool/Str 值打对应 kind tag 直写（rt_arr_set_num 天然覆盖，mismatch 落 CG7 消息泛化） | bool/str 数组作实参/返回值透传/类字段存取/==（同内容/异内容/跨 kind）/动态域写 bool-str 值引用联动与负索引/数值域读写回归/嵌套数组透传 print-len-== 程序编译执行，输出与解释器一致 **✅ t88** |
| S42 | 嵌套数组放宽：visitArrayLiteral 两守卫解除（≥3 层/内层 bool-str），任意 elem 内层数组进 kind 4 槽；visitIndexAssign 整槽替换放宽为任意元素内层数组（非数组值仍拒编）；rt 侧零改动（elem_to_bits/rt_arr_to_str/rt_arr_eq 全 kind 递归已就绪）；~~内层经动态域索引读出 kind ≥ 2 落 t88 既有 CG9 陷阱~~（已于 S98 t151 消除） | 内层 bool/str 字面量/print/length/len/整槽替换/深比较（同异内容与跨 kind）/三层嵌套 print-==-整槽替换/混合内层 kind/别名联动/数值内层逐层读写回归程序编译执行，输出与解释器一致 **✅ t89** |
| S43 | Num 元素数组字面量：visitArrayLiteral 数值系同质判定扩展含 Num（~~互混或全 Num 统一提升 Double 视图，rt format_f64 整数值省 .0 与解释器混合表示输出对齐~~（已于 S106 t159 改为按元素保型落 kind 7 双步长槽，整数位不再定宽为小数——print 仍由 rt 侧按 tag 格式化，整数值省 .0 不变））；to_double 加 Num 分支（tag select 免分支）；visitIndexAssign 静态数值槽收 Num 值下沉既有 rt_arr_set_num（tag==kind 直存/0→1 提升/1→0 落 CG7 陷阱）；零新增 rt 接口 | Num（整/小数态）与 Int/Double 混合字面量/全 Num 字面量/print/索引读（含负索引）/length/len/Num 值写 int-double 槽/深比较/函数内局部 Num 数组循环遍历程序编译执行，输出与解释器一致 **✅ t90** |
| S44 | 嵌套函数声明：受限雷姆达提升——declare_function 加 prefix 改编键（outer.inner，符号 collie.outer.inner），尾部 declare_nested_in 递归下探 Block/If/While/For/DoWhile/Switch 建原型进 nested_fns_ 注册表；visitFunction 嵌套路径声明处向 scopes_ 登记 fn_key 绑定（对齐解释器"执行到声明处 env_.define"——声明前不可见/块退出失效），嵌套体链底拷入外层链函数绑定（自身递归/前置兄弟可见），生成现场 in_function_/返回类型保存恢复；visitCall 作用域链函数绑定优先于顶层 functions_；函数名作值/被赋值拒编；零新增 rt 接口 | 基本嵌套/嵌套读全局/自身递归 fac(5)/前置兄弟嵌套/带参字符串嵌套程序编译执行，输出与解释器一致 **✅ t91** |
| S45 | 无初始化变量声明：四静态类型 {integer,decimal,bool,string} 放行——槽照常创建（顶层零初始化全局槽/函数内 alloca 不预存），CGVar 加 uninit ~~+ decl_depth~~（S59 t110 删除，改定赋区域跟踪）；读 uninit 槽拒编（语义层流不敏感放行的分支/循环内赋值后读，解释器运行期仍 none，零初始化槽会错值——拒编不错编）；~~同块（scopes_ 同深度）赋值清 uninit 放行后续读，深层块赋值存值不清标记~~（S59 t110 定赋区域跟踪替代）；零新增 rt 接口 | 顶层声明后隔句赋值再读/四静态类型/函数内局部/顶层全局函数体内读/循环体内声明+同块赋值程序编译执行，输出与解释器一致 **✅ t92** |
| S46 | 三元/==? 分支实例类型统一到最近公共祖先：新增 nearest_common_ancestor 沿 super 链求 NCA（含自身端点），gen_ternary 与 match 合流两触点 Obj cls 不等改求 NCA——有则 result cls 取祖先（Obj 的 LLVM 表示统一指针，PHI 无关 cls；t86 对象头类 id + 动态分派保证合流值按运行期真实类解析方法、字段走父类前缀布局），无公共祖先维持拒编不错编；零新增 rt 接口 | 子类/父类、兄弟类、孙类/兄弟类三元合流，孙类/父类统一非根祖先字段读，==? 三支合流，合流值直调方法与进函数形参程序编译执行，输出与解释器一致 **✅ t93** |
| S47 | 三元/==? 分支数组元素类型合流统一动态域：gen_ternary 与 match 合流两触点 Arr elem 不等改统一 elem=Num 动态域哨兵（t70/t88 既有机制）——数组值不透明 ptr，PHI 无关 elem；合流值为新鲜值元数据自诞生即动态无程序序失配，kind 随数组对象运行期自带（print/len/== rt 侧全 kind 覆盖，索引读~~数值系正常、kind ≥ 2 落既有 CG9 陷阱不错值~~（已于 S98 t151 按运行期 kind 产出 Dyn））；两触点 result_elem 逐支累计（同 t93 result_cls 模式）；零新增 rt 接口 | Int/Double 三元合流索引读（两向）、合流值存动态槽整组 print/len、Str/Int 合流、合流值相等比较、==? 三支合流、动态域值再进三元程序编译执行，输出与解释器一致 **✅ t94** |
| S48 | 三元/==? 分支 tuple 合流静态展开：新增 merge_tuple_arms 三阶段——元数据递归校验形状（元素数+名字表+嵌套位置）并合并各叶位类型（复用标量合流规则：数值提升/Tri 加宽/Arr elem 降 Num 哨兵/Obj cls 求 NCA），逐支叶值对齐指令落各支末块（DFS 序展平），merge 块逐叶 PHI 自底向上重建新鲜 CGTuple；形状/名字不一致维持拒编不错编；零新增 rt 接口 | 无名/命名/字面量支/元素数值混型/嵌套 tuple 三元合流，==? 三支合流，合流值索引/字段/get/length/相等比较/再进三元，tribool 三分支形式，数组元素两支 elem 不同程序编译执行，输出与解释器一致 **✅ t95** |
| S49 | 无初始化变量声明放行面扩展：visitVarDecl 从四静态类型（t92）扩展 number/tribool/char/character/byte/word——Num（struct{i64,i64}）/Tri（i8）/Str（ptr）零初始化槽合法，byte/word 沿用 i64 承载 + bit_max（赋值走 visitAssign 通用路径，既有赋值点 check_bit_range 陷阱自动生效）；uninit/~~decl_depth/同块清除~~（S59 t110 定赋区域跟踪替代）机制复用；~~array/Tuple/类维持拒编~~（array/类 S54 t101、Tuple S61 t112 陆续解锁）；零新增 rt 接口 | number 整/小数态、tribool 三态、byte/word 范围内赋值、char/character、函数内局部、顶层全局函数体内读、循环体内声明+同块赋值程序编译执行，输出与解释器一致 **✅ t96** |
| S50 | byte/word 函数返回类型：declare_function 顶层函数返回类型 KW_BYTE/KW_WORD 前置识别为 CGType::Int 承载 + CGFunction.ret_bit_max（255/65535），visitFunction 保存/设置/恢复 current_ret_bit_max_，visitReturn 值对齐 Int 后插 check_bit_range（复用 t69 陷阱，越界调 rt_trap_bit_range，对齐解释器 coerce_to_declared 返回值校验）；返回值作普通 Int 表达式域消费；~~byte/word 参数~~（S51 t98 已放行）/类方法返回维持拒编；零新增 rt 接口 | byte/word 返回值 print/参与算术/存 byte-word 变量（声明与赋值点范围陷阱）/多返回路径 clamp/循环内反复调用/word 返回值比较程序编译执行，输出与解释器一致 **✅ t97** |
| S51 | byte/word 函数参数：declare_function 参数循环 KW_BYTE/KW_WORD 前置识别为 CGType::Int 承载（llvm 形参 i64），visitFunction 形参绑定置 CGVar.bit_max（255/65535）——体内重赋走既有赋值点 check_bit_range 陷阱、返回越界走 visitReturn t97 陷阱；调用点零改动——重载解析要求实参类型即 byte/word（整数字面量/算术实参双端语义错非差分面），实参恒为来源处已校验值；~~类方法/构造器参数维持拒编~~（S52 t99 已放行）；零新增 rt 接口 | byte 形参 identity/clamp 多返回/word 形参/嵌套调用/byte 函数返回作实参/循环内反复调用/形参返回值比较程序编译执行，输出与解释器一致 **✅ t98** |
| S52 | byte/word 类方法/构造器参数与返回：方法注册循环返回类型/参数 KW_BYTE/KW_WORD 前置识别为 CGType::Int 承载（返回位复用 CGMethod 继承的 ret_bit_max），gen_method_body 设/复位 current_ret_bit_max_（返回走 t97 陷阱）+ 形参绑定点读 AST 形参类型插 check_bit_range 范围陷阱（方法单签名按名解析、实参可为整数字面量、覆盖方法/构造器/base 全路径）+ 置 CGVar.bit_max；coerce_call_arg 零改动；零新增 rt 接口 | 构造器 byte 参数字面量实参/方法 byte 参数字面量+变量实参/方法 byte 返回 print-算术-存变量/byte 参数+byte 返回多路径/word 参数+word 返回多路径与循环累计/方法返回值作方法实参/byte 返回值有序比较程序编译执行，输出与解释器一致 **✅ t99** |
| S53 | 类实例进数组（同类，kind 5）：arr_kind_of(Obj)→5、elem_to_bits/bits_to_elem 加 PtrToInt/IntToPtr；visitArrayLiteral 追踪元素类名（复用 CGValue.cls）+ 混合类守卫，visitVarDecl/visitAssign array 分支传播 cls + 异类守卫，visitIndex 静态读出传播 cls（arr[i].field/method() + 动态分派），visitIndexAssign Obj 分支 cls 兼容守卫（子类 upcast 放行）；rt 三触点加 kind 5——arr_to_str `<object>` / arr_eq 恒不等 / trap_arr_kind `object`；本地静态读出全解锁，~~动态域 obj 读出落 CG9 陷阱~~（已于 S98 t151 解锁） | 同类实例数组 build/len/print/toString/本地静态读出字段与方法调用/负索引/整槽写同类/子类 upcast 整槽写后动态分派/== 实例恒不等程序编译执行，输出与解释器一致 **✅ t100** |
| S54 | 无初始化 array/类类型变量声明：visitVarDecl 无初始化分支加 KW_ARRAY（建 opaque ptr 槽 + elem=Num 动态域哨兵 t70）/IDENTIFIER 已注册类名（建 Obj 槽 + cls）两分支，沿用 uninit ~~+ decl_depth~~（t92，S59 t110 后为定赋区域跟踪）；visitAssign Arr/Obj 两分支原早退不经通用路径故各补 uninit 清除；读 uninit 拒编~~、深层块赋值不清标记~~全复用 t92 机制 | 无初始化 array 隔句赋值读写/别名引用语义/无初始化类变量赋值后字段读+方法调用+upcast 动态分派/函数内局部/循环体内同块赋值程序编译执行，输出与解释器一致 **✅ t101** |
| S59 | 无初始化变量定赋区域跟踪：t92 同块深度代理（decl_depth）升级为定赋区域跟踪——赋值无条件清 uninit（发射顺序即支配顺序），八个条件发射位点（if 分支/while/for 条件体增量/do-while/switch 候选与 body/三目臂/multi-match 臂/短路 RHS）uninit_snapshot/restore 快照隔离区内清除不外泄（句柄为 scopes_ 下标+变量名防 vector 重分配悬垂），visitIf 汇合点两分支清除集取交集（终止支不约束交集，死块判 hasNPredecessors(0)）——if/else 全路径定赋后读放行；顺带修复无大括号 if 分支/三目臂内赋值同深度清除泄漏两处既有错编洞 | integer/decimal/string/bool/number/array/类类型 if-else 全路径定赋、分支内读+汇合读、嵌套 if/else、无大括号分支、裸块外泄、终止支 return、双 return、多变量交集程序编译执行，输出与解释器一致 **✅ t110** |
| S60 | number → integer/decimal 窄化：新增 num_to_int_checked（tag 1 小数态分支调 collie_rt_trap_num_narrow 陷阱退出，对齐解释器 coerce_to_declared "cannot assign decimal value to 'integer' variable"；整数态返 bits 即 i64）；三触点接入——coerce_call_arg（函数/方法/构造器/base 全六调用点共用）/coerce_for_slot（变量与字段槽）/visitReturn；Num→Double 双态皆合法复用 to_double 既有 Num 分支（t90 tag select 免分支，无陷阱）；新增 1 个 rt 接口 | 整数态 number → integer 形参/变量初始化与赋值/返回值/方法与构造器实参/integer 字段槽，双态 number → decimal 形参/变量/返回值/方法实参，number 表达式实参程序编译执行，输出与解释器一致 **✅ t111** |
| S61 | 无初始化 Tuple 变量声明：visitVarDecl 无初始化分支加 KW_TUPLE——形状（元素数/名字表）声明处无从推断，不建槽，CGVar 仅记 type=Tup + uninit（tup 保持 -1 哨兵）；visitAssign Tup 分支 tup<0 识别首赋——按 RHS 形状 create_tuple_var 建解构槽组（t68/t76 既有机制）、形状自此固化、清 uninit；重赋路径补无条件清 uninit（t110 快照隔离下安全）；换形状重赋走既有 store_tuple_var 拒编不错编；零新增 rt 接口 | 顶层无初始化 Tuple 首赋后 print/索引/负索引/length、同形重赋、命名元组字段/get、嵌套 tuple 元素、函数内局部 + if/else 全路径定赋程序编译执行，输出与解释器一致 **✅ t112** |
| S62 | do-while 体定赋外泄：体必执行一次——体内**无本层 break/continue** 时体末清除集支配循环出口，外泄（不 restore）；visitDoWhile 先调 has_loop_escape 递归扫描体（下探 block/if、不下探嵌套循环/switch——其内 break/continue 绑定内层），无逃逸则体末不 restore（嵌套 if/else 部分定赋已由 visitIf 交集处理，体末 uninit 状态即到达出口必定赋集）；含逃逸保守隔离（同 t110）；零新增 rt 接口 | do-while 体内无条件定赋后区外读、体内 if/else 全路径定赋、多变量同时外泄、嵌套 do-while、函数内局部体定赋程序编译执行，输出与解释器一致 **✅ t113** |
| S63 | 同质 Arr/Obj 元素 tuple 非常量索引与动态键 get()：结果 CGType 恒为 Arr/Obj（静态可定），复用 t83 单数组物化 + rt kind 4/5 机制——visitIndex/visitMethodCall get() 非常量守卫放行 Arr/Obj（~~仍拒 Tup 无 LLVM 承载~~——Tup/Arr/Obj/Fn 容器位元素已于 S119 t172 入 tags+bits 物化路径）；新增 tuple_dynamic_result helper 回填结果元数据——Arr 结果扫描各内层 elem 统一（不一致降 Num 动态域哨兵 t94 同规则）、Obj 结果扫描各 cls 取最近公共祖先（t93/t100 同规则，~~无公共祖先拒编不错编~~——无公共祖先者已于 S119 t172 由 `tuple_obj_merged_cls` 判假回退 Dyn 承载路径，读出 tag 5 后按对象头类 id 分派）供下游索引/字段/方法解码；零新增 rt 接口 | 同质数组元素 tuple 非常量索引后再索引、命名 tuple 动态键 get()、内层 elem 不一致降 Num 动态域、同质实例元素方法动态分派+类不一致取 NCA+字段读、函数内局部程序编译执行，输出与解释器一致 **✅ t114** |
| S64 | 实例数组同继承树互赋（upcast/downcast）：visitAssign Arr 分支 Obj 元素异类守卫（原无条件拒）镜像标量 Obj 放行 `is_subclass_of` 任一方向——同树内元素类沿链前缀布局兼容，arr[i].field 按 var->cls 偏移解码（Animal 级字段偏移前缀一致），方法按对象头类 id 动态分派；var->cls 保持不变（不收敛 NCA，与标量 Obj 一致）；兄弟类（互不为子类）特有字段偏移错位可能错值，保守拒编不错编；零新增 rt 接口 | Animal 槽 ← Dog 数组上溯、Dog 槽 ← Animal 数组下溯、跨两级深链上溯、同树链内多轮互赋、引用别名、函数内局部槽、函数后全局槽程序编译执行，输出与解释器一致 **✅ t115** |
| S65 | tuple 内实例元素同继承树互赋（同形状重赋 upcast/downcast）：store_tuple_var Obj 元素守卫（原 `e.cls != var.cls` 无条件拒）镜像标量 Obj / 实例数组（t115）放行 `is_subclass_of` 任一方向——同树内元素类沿链前缀布局兼容，t[i].field 按槽声明类 var.cls 偏移解码（load_tuple_var 恒用 var.cls）、方法按对象头类 id 动态分派；槽 var.cls 保持不变（不收敛 NCA），stored 元数据也保留 var.cls 与槽解码一致；兄弟类（互不为子类）特有字段偏移错位可能错值，保守拒编不错编；零新增 rt 接口 | Animal 元素槽 ← Dog 上溯、Dog 槽 ← Animal 下溯、跨两级深链上溯、命名 tuple 元素 .name/.get、嵌套 tuple 内元素、全局 tuple 函数内重赋、函数内局部 tuple 程序编译执行，输出与解释器一致 **✅ t116** |
| S66 | tuple 变量换形状重赋（重建解构槽组）：visitAssign Tup 分支同形状重赋（t68 逐槽写）外新增换形状路径——tuple_shape_matches 递归同形判定（元素数+名字表逐层一致，嵌套递归，元素类型不参与），不一致则 create_tuple_var 重建槽组（递归建全部子槽、嵌套换形状一并解决，旧槽组成死存储不影响运行期输出），槽组名追加 "#N" 序号防全局符号冲突；双守卫拒编不错编：① 8 个条件发射区（t110 位点 + CondDepthGuard RAII 跟踪 cond_depth_）内换形状拒编——区外读可能按新形状解码旧槽组错值；② 全局槽组跨函数（has_global_slot 递归查嵌套子槽组任一 GlobalVariable + fn_gen_at 基准，t106 同规则）拒编——函数体链底快照旧槽组不回溯；循环回边无需单独守卫（循环体即条件发射区，且重建是新槽组、旧读旧槽互不污染，与 Arr 共享单 ptr 槽不同）；零新增 rt 接口 | 顶层直线换形状（扩容/缩容/整值打印）、函数内局部换形状（混型+单命名元素）、嵌套 tuple 内层扩容、命名 tuple 名字表变化、与 t 无关条件区后换形状（cond_depth 精确性）、无初始化 Tuple 首赋后换形状、多轮换形状程序编译执行，输出与解释器一致 **✅ t117** |
| S67 | 实例数组单槽写同继承树互赋（`arr[i] = x` upcast/downcast）：visitIndexAssign Obj 元素守卫（原仅放行 upcast `is_subclass_of(v.cls, object.cls)`，t100）镜像整体互赋（t115）追加 downcast `!is_subclass_of(object.cls, v.cls)` 才拒——同树任一方写入 rt_arr_set 直存（运行期写读，无静态形状错配，无需 t117 类守卫）；同树内元素类沿链前缀布局兼容，arr[i].field 按 object.cls（声明元素类）偏移解码、方法按对象头类 id 动态分派；兄弟类/无关类（互不为子类）特有字段偏移错位可能错值，保守拒编不错编；零新增 rt 接口 | 上溯单槽写回归（t100）、下溯单槽写（核心解锁）、同树链内多轮单槽写、引用别名单槽写、函数内局部（含 if 分支内）、整体互赋+单槽写混用、循环内交替单槽写程序编译执行，输出与解释器一致 **✅ t118** |
| S68 | 混合类实例数组字面量 NCA 收敛（`[new Dog(..), new Cat(..)]`）：visitArrayLiteral 元素类守卫（原要求全部同类，t100）改为最近公共祖先收敛——nearest_common_ancestor 有公共祖先则元素类收敛到 NCA（逐元素收敛；字段按 NCA 前缀偏移解码、方法按对象头类 id 动态分派，与 t115/t118 同规则），~~无公共祖先（跨树无公共字段前缀）维持拒编不错编~~（跨树无公共祖先已于 S84 t137 解锁——收敛 cls 空动态 object 元素，方法走 t133 分派、字段维持拒编）；零新增 rt 接口 | 下溯混合 [Animal,Dog]、兄弟混合 [Dog,Cat]（NCA=Animal）、深链混合 [Dog,Puppy]（NCA=Dog）、多元素逐级收敛、print 整数组/单元素 `<object>`/== 恒不等、整体互赋与单槽写协同、函数内局部、循环内访问程序编译执行，输出与解释器一致 **✅ t119** |
| S69 | object 动态类型声明 Obj 初始值（静态初始类名）：visitVarDecl 新增 KW_OBJECT 分支——解释器 coerce_to_declared default 不校验任意值直存；codegen 以"静态初始类名"近似解锁 Obj 初始值场景（槽记初始值类名：字段按该类前缀偏移解码、方法按对象头 id 动态分派、后续同树重赋走 visitAssign Obj 既有守卫 t86/t103，与类名声明同规则）；非 Obj 初始值（string/number/none 等需动态值表示）与无初始化声明维持拒编不错编（后者已于 S70 t121 解锁）；零新增 rt 接口 | object 声明 new Dog 后方法动态分派/字段读/继承方法、同树下溯重赋、同树链内多轮重赋、object 互初始化与类名变量初始化（引用别名+字段写可见）、函数内局部+if 分支重赋、循环内多轮重赋程序编译执行，输出与解释器一致 **✅ t120** |
| S70 | 无初始化 object 变量声明（首赋吸收 RHS 类名）：visitVarDecl 无初始化分支新增 KW_OBJECT（Obj 槽 cls 留空占位、uninit 标记，原落 "variable declaration without initializer" 拒编），visitAssign Obj 分支守卫前识别 cls 空占位首赋吸收 RHS 类名（镜像 t112 首赋建槽模式）——吸收后与类名声明同规则：字段按吸收类前缀偏移解码、方法按对象头 id 动态分派、后续同树重赋走既有守卫 t86/t103；非 Obj 值首赋与 uninit 期读（条件发射区快照隔离）维持拒编不错编；零新增 rt 接口 | 直线首赋（方法/字段/继承方法）、同树链内多轮重赋、if/else 双分支全路径首赋+汇合点读、函数内局部+if/else 全路径首赋后返回、循环体内声明+首赋+读、互初始化引用别名+字段写可见、全局首赋后函数内同树重赋+函数外读、与 t120 带初始化混用程序编译执行，输出与解释器一致 **✅ t121** |
| S71 | number/decimal 值作下标（数组读/索引赋值/字符串/tuple 非常量/嵌套）：三处 "non-integer index" 静态拒编守卫（visitIndex tuple 非常量臂、visitIndex 数组/字符串臂、visitIndexAssign）改经新增 index_to_int 归一——Int 透传零开销，Double/Num 镜像解释器 normalize_index：to_double 视图、floor 整数态判定（非整数态含 NaN 调新增 rt 陷阱 collie_rt_trap_index_integer "Index must be an integer" 退出不错值）、整数态 clamp ±4e18 防 FPToSI poison 后转 i64，负索引回绕/越界复用 rt 侧既有归一化；Bool/Str 等其余下标类型维持拒编不错编；新增 1 个 rt 接口 | number/decimal 变量作数组读下标、负 number 下标回绕、number 索引赋值写回、字符串 number 下标取字符、tuple 非常量 number 下标、number 循环计数遍历数组、嵌套数组 number 下标程序编译执行，输出与解释器一致 **✅ t122** |
| S72 | 类方法重载（同名不同参）first-wins：register_class_methods 原 "method overloading" 拒编改为本类内同名键首个登记后 `continue` 跳过其余同名成员，visitClass 体生成遍加 `stmt` 指针守卫（仅首个登记成员生成体，避免同一 llvm::Function 重复建 entry 块）——镜像解释器 find_method 沿声明序首个同名者胜、call_class_method 仅对首个做元数检查，后续重载从不被调用；继承副本/覆写沿用既有单态化机制不受影响；调用非首个重载的元数不符面两端同为报错（rc=1）维持拒编不错编；零新增 rt 接口 | 本类内多重载组首个生效、体内 this.m() 自调首个重载、父类多重载+子类覆写首个的分派交互程序编译执行，输出与解释器一致 **✅ t123** |
| S73 | 顶层控制流块内函数声明（if/while/for/do-while/switch/裸块）：generate 阶段三原仅直登 FunctionStmt 改为——遇非函数顶层语句以顶层语句序号作合成前缀调 declare_nested_in 递归登记为块作用域嵌套函数，复用 t91/t104 嵌套机制；数字前缀非法标识符首字符天然不与用户键冲突，同一顶层语句内兄弟同名（then/else）走 "function overloading" 拒编、兄弟顶层语句各自前缀互不干扰；可见性由 visitFunction 声明处按块作用域绑定（对齐解释器"执行到声明处 env_.define"，块退出即失效、块外调用两端同为语义层 Undefined 报错 rc=1）；递归经既有 fn_key 拷入链底支持；零新增 rt 接口 | 裸块/if/while/for/do-while/switch 内声明块内调用、嵌套块内声明、块内递归、块内调顶层函数、兄弟 if 块同名各自前缀程序编译执行，输出与解释器一致 **✅ t124** |
| S74 | 构造器无初值字段定赋守卫放宽（t109 前缀定赋 → 顶层直线定赋）：visitNew uninit 字段守卫原要求构造器体顶部**连续前缀**全为 `this.f=<不观察 this>` 定赋，遇任何非定赋语句即 break 拒编。改为遍历顶层语句——定赋语句照旧收集；非定赋的**表达式语句**若整棵不观察 this（无 this/base，实例未逃逸故无从触及任何 uninit 字段，如 print/无 this 的调用）则安全跳过继续扫描，一旦遇观察 this 的非定赋语句或非表达式语句（控制流/声明/return，可能在赋值前读 uninit 字段或条件赋值）保守 break；pending 空即提前 break（后续无从暴露 none）——镜像解释器字段先绑 none 后覆写、none 态全程不可观察；零新增 rt 接口 | 构造器体内先 print 后定赋、定赋语句间夹杂 print/无 this 顶层函数调用、多字段交错定赋程序编译执行，输出与解释器一致 **✅ t125** |
| S75 | 构造器无初值字段经控制流定赋（S74 顶层直线 → 递归下钻）：visitNew 守卫抽为文件局部递归助手 `ctor_definite_assign_stmt`（匿名命名空间，仅依赖 AST + `expr_observes_this`）——裸块 BlockStmt 原地递归（必执行等同直线）；双臂 if（`!expr_observes_this(condition)` 且 `else_branch()!=nullptr`）then/else 各在 pending 副本上递归后**求交**（仅两臂皆删的字段视为定赋）；继承字段同面覆盖。单臂 if / 一臂缺赋 / 条件观察 this / 循环 / switch / return / 声明 / 观察 this 的非定赋语句一律保守停止落拒编（某路径 none 可观察或非定赋）；零新增 rt 接口 | 构造器体内 if/else 双臂各赋同字段、嵌套裸块内定赋、继承字段经双臂 if 定赋、嵌套块含双臂 if 多字段程序编译执行，输出与解释器一致 **✅ t126** |
| S76 | 裸块内类声明（class-in-bare-block）：generate 第一遍原按顶层语句直收 ClassStmt 改为经新增 `collect_classes_in_order` 按执行序收集——顶层 ClassStmt 照收，裸块 BlockStmt 恒执行故递归下钻收（块内类名另记 block_class_names），if/while/for/do-while/switch 体内类声明**不收**（执行期条件性，编译期无条件注册会使 codegen 比解释器宽松：`if(false){class Foo{}} new Foo()` 解释器 Undefined class rc=1、codegen 却能跑即错编方向）；块内类注册后置 `CGClass.ready=false`，第二遍 visitClass 走到声明语句置 true（对齐解释器 `classes_[name]=&stmt` 执行到才登记），visitNew 在顶层语境（`!in_function_`）遇未 ready 类拒编 "used before its declaration"（函数/方法体内不检查：其 IR 在声明处生成而实例化在调用时）；零新增 rt 接口 | 裸块内声明类后块外 new 与字段/方法读、嵌套裸块内声明、裸块内基类 + 嵌套块内子类继承覆写 + upcast 分派、块内声明块内实例化程序编译执行，输出与解释器一致 **✅ t127** |
| S77 | 函数体内类声明（class-in-function）：`collect_classes_in_order` 增 FunctionStmt 分支递归下钻函数体（沿用直线 + 裸块规则，不下钻 if/while/for/switch），收集的非顶层类记入 `deferred_classes`（值 = 宿主函数指针，裸块内为 nullptr）→ 注册后置 `ready=false` + `owner_fn`；函数被调用即体内顺序执行、解释器类表全局持久，编译期按执行序注册与之等价。visitClass 的 `in_function_` 无条件拒编改为"未进类表才拒编"，并加 `bodies_generated` 重访保护（单态化副本不重建 entry）；gen_method_body 收尾由复位顶层改为保存恢复 `in_function_`/`current_fn_`/this/类上下文/返回类型（否则函数体内生成方法体会破坏外层语境）；新增 `current_fn_`（visitFunction 设、gen_method_body 保存恢复）与 visitNew 宿主守卫：函数内声明的类仅宿主函数体内可实例化；零新增 rt 接口 | 函数体内声明类后体内 new 与字段/方法读、体内类继承顶层类并覆写 + upcast 分派、体内嵌套裸块内声明与实例化、多次调用程序编译执行，输出与解释器一致 **✅ t128** |
| S78 | 类方法/构造器体内类声明（class-in-method）：`collect_classes_in_order` 在收得 ClassStmt 后递归其 `members()` 中的 FunctionStmt（方法/构造器）体，宿主记为该方法（沿用直线 + 裸块规则）；gen_method_body 生成体时置 `current_fn_ = &stmt`（并在收尾随 saved 一并还原——t128 只加了保存未加还原，会残留内层方法语境），使方法体内 new 与 owner 匹配；`bodies_generated` 保护使继承单态化副本重访不重建方法体 entry；零新增 rt 接口 | 方法体内声明类（含 uninit 字段构造器 + 方法）后体内 new 与字段/方法读、同方法多次调用、继承子类方法副本重访（bodies_generated 只生成一次）程序编译执行，输出与解释器一致 **✅ t129** |
| S79 | 控制流块内类声明（class-in-block）：把宿主维度从"函数/方法"推广为"作用域栈"——`CGClass::owner_fn`(FunctionStmt*) 改为 `owner_scope`(Stmt*)，新增 `scope_stack_`（进入函数/方法体、if/while/for/do-while 各分支、switch 各 case 体时压入对应 Stmt*，离开弹出，RAII `ScopePushGuard`）；`collect_classes_in_order` 增 If/While/For/DoWhile/Switch 分支递归收集（宿主记为各自块），`visitNew` 守卫改为 `owner_scope != nullptr && !scope_contains(owner_scope)`（仅声明所属作用域在生成栈中——含嵌套——时可见）；无初值字段守卫/重访保护/t128 现场保存恢复全部沿用；零新增 rt 接口 | `if/while/for` 块内与 `switch` case 块内声明类后同块内 new 与字段/方法读（循环体每轮重入、switch 多 case 各声明类均收集注册）程序编译执行，输出与解释器一致 **✅ t130** |
| S80 | 顶层函数重载（同名后定义遮蔽前定义 last-wins）+ 隐式 number/bool→string 转换：declare_function 同名键后者覆盖前者（eraseFromParent 删被遮蔽的死 llvm 函数），新增 winning_stmt_/fn_stmt_to_key_ 记录胜出节点、visitFunction 仅胜出定义生成体（前定义跳过体生成，避免重复建 entry）；对齐解释器 env_.define 覆盖语义——方法侧仍为 first-wins（S72 t123），两者不对称但各自对齐解释器；隐式 number/bool→string 经新增 coerce_to_string_value 复用 collie_rt_*_to_str，在 coerce_call_arg/coerce_for_slot/visitReturn 三处放行（对齐解释器 coerce_to_declared KW_STRING 分支），tri/arr/obj/tup 仍拒编；零新增 rt 接口 | 同名后定义胜出（pT 类 `f(1)` 调最后定义的 string 版、实参 coerce 成 "1!"）、被遮蔽前定义不生成、number/bool 实参/字段/return 隐式转串、pF/pV/pW 双端逐字节一致；新差分用例 s84_topfn_overload 程序编译执行，输出与解释器一致 **✅ t131** |
| S81 | object 动态类型作函数形参与返回值（~~限定实例值~~，对齐 codegen object 变量 S69/S70 语义；S108 t161 起签名表示改为 CGType::Dyn 的 {tag,bits}，形参/返回值可容标量与数组位，本面的实例承载路径经 `dyn_obj_guard` 并入同一动态分派故 s85 零回归）：declared_signature_type 新增 KW_OBJECT 分支（Obj + 空 cls），coerce_call_arg / visitReturn 的 Obj 子类校验在 want_cls/ret_cls 为空（动态 object）时跳过——任意类实例经空 cls 放行、~~非实例值维持拒编~~（S108 t161 解锁）；函数体内 object 形参/返回值的方法调用走运行期类 id 动态分派、字段读取与 object 变量同机制；零新增 rt 接口 | object 形参收不同类实例方法沿动态类分派、object 返回值按条件返回不同类实例、形参+方法组合、object 变量承接返回值后续分派，输出双端一致；新差分用例 s85_object_param 程序编译执行，输出与解释器一致 **✅ t132** |
| S82 | object 动态值（空 cls）方法调用运行期分派：visitMethodCall Obj 分支入口新增空 cls 路径——以**全体定义了该方法的类**（不限同树，object 可承载任意实例）为 switch case，按对象头类 id 分派到各定义者单态化实例；动态类未定义该方法落 default 陷阱臂（消息对齐解释器运行期 "Undefined method"），toString 兜底沿用 t102 规则（无用户定义落内建 "<object>"）；全体未定义报 "undefined method 'X' on object dynamic value" 拒编；各定义者签名一致性校验沿用 t86 防御；~~空 cls 无静态布局，字段访问维持拒编~~（动态 object 字段读/写已于 S89 t142 以同类 id switch 分派解锁，见 S89 行）；零新增 rt 接口 | object 形参/返回值/变量承载跨树实例的方法调用、覆写与继承方法混合、未定义方法运行期陷阱程序编译执行，输出与解释器一致；s85_object_param 全量动态分派场景双端逐字节一致 **✅ t133** |
| S83 | tribool 进数组（kind 6）：visitArrayLiteral 对 ~~bool/tribool 混排收敛 elem=Tri（bool 字面量经 to_tri 加宽 false→0/true→2 直存三态槽）~~（混排已于 S106 t159 改为按元素保型落 kind 7，全 Tri 字面量仍走本 kind 6 路径）；elem_to_bits/bits_to_elem 加 Tri 臂（i8 zext/trunc，0/1/2 无损）；arr_kind_of 新增 kind 6；rt 面 arr_to_str 新增 case 6（false/unset/true 对齐解释器 to_string）、~~arr_eq 新增 bool(kind2)×tribool(kind6) 跨 kind 配对（bool 槽 ×2 镜像解释器 values_equal tri_of(bool)，unset 与任何 bool 恒不等）、同 kind 6 直比复用整数位模式臂~~（已于 S106 t159 统一为逐元素归一化 {tag,bits} 交 `collie_rt_dyn_eq` 单点判定）；动态域整槽写放行 Bool/Str/Tri（rt_arr_set_num_ 带 kind tag）；~~动态域元素读出 kind≥2 落 CG9 陷阱面不变~~（已于 S98 t151 消除）；未定类型 array 的 `a[i]==字面量` 比较两端语义层同拒（非活跃差分面，经显式 tribool 变量承接放行）；~~bool×number 混排字面量维持拒编~~（已于 S86 t139 经 kind 7 收敛解锁）；零新增 rt 接口 | 全 Tri/混排字面量、print/toString/插值、索引读写与三态互写、数组深比较含跨 kind 配对、动态域透传 print/len、循环内读写程序编译执行，输出与解释器一致；新差分用例 s86_tribool_array + ~~拒编用例 s86_reject_bool_num_array/s86_reject_tribool_num_write（拒编门禁 codegen_reject_*）~~（s86_reject_bool_num_array 已于 S86 t139 退役、s86_reject_tribool_num_write 已于 S106 t159 退役为差分用例 s109_tribool_num_write）**✅ t136** |
| S84 | 跨树（无公共祖先）实例数组字面量：visitArrayLiteral 的 Obj 混合类分支 NCA 空时不再拒编，收敛 `elem_cls=""`（动态 object 元素，kind 5 复用）——对齐解释器数组动态存任意实例；索引读出落空 cls → 方法调用走 S82/t133 全体定义者动态分派（不限同树）、字段访问经 visitProperty 空 cls 守卫保守拒编（无公共字段前缀偏移不可静态解码，杜绝 `classes_.at("")` 崩溃）；整槽写放行任意实例（object.cls 空时跳过同树校验，槽 kind 5 承载 ptr 运行期对象头自解释）；print/len/== 经 rt kind 5 全承载（实例恒不等对齐解释器 values_equal 无 Instance 分支）；同树混合类仍收敛 NCA（S68 回归不变）；零新增 rt 接口 | 跨树字面量 print/len、索引读出方法动态分派（常量/非常量索引）、object 变量承接再分派、整槽写异类、数组深比较恒 false、整体重赋、动态域透传 print/len、同树 NCA 字段/方法回归程序编译执行，输出与解释器一致；新差分用例 s87_cross_tree_array + 拒编用例 s87_reject_dynamic_object_field **✅ t137** |
| S85 | 异质 tuple（含 Bool/Str/Tribool）非常量索引/动态键 get()：新增 CGType::Dyn 动态标量承载（与 Num 同构 {i64 tag, bits}，tag 复用数组 kind 编码 0=int/1=dec/2=bool/3=str/6=tribool）；visitIndex/get 异质 tuple 物化在数值系（全 ∈{Int,Double,Num}→Num 双数组，保算术回归）之外新增标量系判定（全 ∈{Int,Double,Num,Bool,Str,Tri,Dyn}）→ materialize_dyn_scalars 落 tags+bits 双 kind-0 数组（Dyn 元素透传自身 tag/bits 支持元组套元组）、同一索引/键两次 rt_arr_get/rt_tuple_get 拼 Dyn（负索引归一化/越界/未命中陷阱均在首次，零新增 rt 接口）；同质 tribool 与含 unset 混排亦落 Dyn；rt print_num/num_to_str 加 tag 2/3/6 臂（true/false、串内容、true/unset/false 对齐 Value::to_string）；Dyn 仅 print/toString/插值可消费，~~算术/比较落各消费点既有 default 拒编不错编~~（算术已于 S95 t148、比较已于 S96 t149 以运行期 tag 分派解锁；gen_print/llvm_type_of 补显式臂）；~~进类型化槽/进函数签名~~已于 S94 t147 以运行期 tag 拆槽解锁 | 异质 int/str/bool/tribool/number 混排 tuple 的变量索引/负索引/循环遍历/动态键 get()、toString/插值、Dyn 进新 tuple 字面量再动态读、数值系异质算术比较回归程序编译执行，输出与解释器一致；新差分用例 s88_hetero_tuple_dyn + 拒编用例 s88_reject_dyn_string_slot/s88_reject_dyn_arith **✅ t138** |
| S86 | 异质基本类型数组字面量 `[1,"two",true]`（tagged 元素槽，kind 7）：visitArrayLiteral 收敛在~~数值系（→Double）与 bool/tribool（→Tri）之外~~新增第三判定（S106 t159 起数值系互混/全 Num 与 bool×tribool 混排也归本判定，不再整体拍平为 Double 视图或加宽为 Tri）——元素全 ∈{Int,Double,Num,Bool,Str,Tri,Dyn} 且互不兼容 → `elem=CGType::Dyn`（运行时 kind 7，槽为 2×len 步长每元素 {tag,bits} 两 i64，tag 复用 kind 编码），~~Obj/Arr/Tup 混入落 is_dyn_scalar=false 维持 "heterogeneous array literal" 拒编~~（~~异质含实例/嵌套数组非本期面~~异质含实例已于 S91 t144 纳入 kind 7 tag 5，~~Arr/Tup 混入仍拒编~~嵌套数组已于 S107 t160 纳入 kind 7 tag 4（承载判定收束为成员 `is_dyn_carrier` = `is_dyn_scalar` ∪ {Obj, Arr}），~~Tup 混入仍拒编~~（Tup 位已由 S117 t170 打 tag 8、函数位由 S118 t171 打 tag 9 纳入 kind 7 承载））；materialize 循环 elem==Dyn 走新增 rt_arr_set_dyn（静态标量打常量 tag+elem_to_bits，Num/Dyn 取运行期 num_tag/num_bits）；visitIndex elem==Dyn 走 rt_arr_get_dyn 一次取回拼 CGType::Dyn（负索引归一化/越界陷阱在 rt 内），消费面同 t138 Dyn——print/toString/插值/再入异质数组写放行、算术/~~进类型化槽经既有 default 拒编~~（算术与进类型化槽分别已于 S95 t148、S94 t147 以运行期 tag 守卫/拆槽解锁；`a[i]==literal` 两端语义层同拒非差分面）；visitIndexAssign elem==Dyn 任意标量整槽写自带 tag，~~非标量拒编~~数组值写入常量 tag 4 + 内层数组 ptr 位模式（S107 t160），Tup 等仍拒编；rt collie_rt_arr_new kind==7 按 2×len 分配、arr_to_str 加 case 7 逐元素 {tag,bits} 格式化（串不加引号，对齐解释器 Array 分支）、arr_eq 加两侧 kind7 逐元素 {tag,bits} 深比较（同 tag 直比/数值系 0×1 double 视图/bool×tribool 加宽）~~与 kind7×非 kind7 恒不等分支（kind7 必含 ≥2 种基本类型，同质他数组逐元素至少一 kind 失配，对齐解释器按 Value::kind 分支恒 false）~~（已于 S106 t159 取消：保型字面量落 kind 7 后该前提不成立，arr_eq 改为逐元素归一化 {tag,bits} 交 `collie_rt_dyn_eq` 单点判定）；visitAssign Arr 异型降级保留 Dyn（`v.elem==Dyn` 时 var->elem=Dyn 而非 Num，重赋异质数组后索引读仍走 Dyn）；t136 曾保守拒编的 bool×number 混排 `[true,2]` 经此收敛 Dyn 自然解锁（s86_reject_bool_num_array 退役）；新增 2 个 rt 接口 | 异质 int/str/bool/tribool/decimal 混排字面量 print/len、常量/变量/负索引读、toString/插值、循环遍历、异质整槽写覆写、kind7×kind7 深比较含 int×decimal double 视图、Int 数组变量重赋异质数组后保持 Dyn 解码、bool×number 混排回归程序编译执行，输出与解释器一致；新差分用例 s89_hetero_scalar_array + 拒编用例 s89_reject_hetero_obj_array/s89_reject_dyn_string_slot/s89_reject_dyn_arith；退役 s86_reject_bool_num_array **✅ t139** |
| S87 | 动态 tuple 运行时（三元/==? 不同形状合流 + tuple 进函数签名）：新增 CGType::TupDyn 承载 ptr → `collie_rt_tuple {len, names(kind3 字符串数组，无名槽存空串 ""), values(kind7 {tag,bits} 数组)}`，复用 t139 kind-7 元素槽与 t84 names 数组设施；materialize_tuple_dynamic 把编译期静态展开的 CGType::Tup（侧表 tuple_values_）物化为堆上动态 tuple 对象（元素 !is_dyn_scalar——含 Arr/Obj/嵌套 Tup——落 "dynamic tuple with array/object/nested-tuple elements" 拒编不错编；~~本面拒编集~~已于 S111 t164 换 `is_dyn_carrier`——Arr/Obj 元素入承载、消息改 "dynamic tuple with nested-tuple or function elements"）；三元/==? 合流：all_static 且 tuple_arms_same_shape（递归比各支元素数+名字表+嵌套位全 Tup；~~本判定~~已于 S111 t164 更名 `tuple_arms_static_mergeable` 并加叶位类型可合流校验，异型叶位从拒编改为回退动态合流）保留原静态 merge_tuple_arms 展开路径不变（避免破坏 s48/s88 回归），否则逐支物化 ptr 后 PHI 合流为 TupDyn（新形状不一致自动降级动态）；declared_signature_type 的 KW_TUPLE 原 "tuple in function signature" 拒编改 `TupDyn`，coerce_call_arg/visitReturn 的 Tup→TupDyn 经物化放行（跨 ABI 面解锁）；visitVarDecl Tup 槽收 TupDyn 初始值转 TupDyn；消费面 visitIndex（rt_tuple_get_dyn 拼 Dyn，负索引/越界陷阱在 rt）、visitProperty（length→rt_tuple_len，~~其余具名字段直接访问拒编~~已于 S110 t163 解锁——常量键名与 `get(key)` 同源交 `rt_tuple_key_dyn` 扫描取 {tag,bits} 拼 Dyn）、visitMethod（get→rt_tuple_key_dyn 未命中 "Undefined tuple field" 陷阱）、== !=（含动态侧先物化再 rt_tuple_eq 深比较，tuple 族×非族恒 false）、print/toString/插值（rt_tuple_to_str，命名 `(name: v)`、string 元素不加引号、空 `()` 对齐解释器）；Dyn 索引结果~~进类型化槽/算术~~经既有 default 面拒编（进类型化槽与算术已分别于 S94 t147、S95 t148 以运行期 tag 守卫解锁）；新增 6 个 rt 接口 | 三元不同形状 (1,"two",true):(3,4) 命中/未命中两支、tuple 进函数形参与返回值、length/常量与负索引/toString/插值、命名 get() 键命中与未命中陷阱、== 深比较（动态×静态、动态×动态、!=）、==? 分支合流程序编译执行，输出与解释器一致；新差分用例 s90_dynamic_tuple + 拒编用例 ~~s90_reject_dynamic_tuple_field~~（已于 S110 t163 解锁退役）/s90_reject_dyn_tuple_string_slot/s90_reject_dyn_tuple_arith **✅ t140** |
| S88 | 闭包按引用捕获外层形参/局部（受限雷姆达提升 + 单元指针转发）：CGVar 新增 cell_id（每个真实存储槽建时自增取号，0=全局/函数绑定/元组解构等非单槽者），CGFunction 新增 captures（每项 name+宿主 cell_id+type/elem/cls/bit_max/uninit）与 build_closure_prototype——嵌套函数（宿主非类方法，`current_this_==nullptr`）体生成前收集体内自由标识符（新增文件局部递归 collect_free_idents_expr/stmt，含更深层嵌套体供逐层转发、不下探 ClassStmt 成员），逐个经宿主 scopes_ 解析：仅命中「有单槽 ∧ 非 GlobalVariable ∧ cell_id≠0 ∧ fn_key 空」者登记为捕获（顶层全局经链底槽拷贝直读、函数绑定/元组槽跳过）；捕获为空则保留第一遍无捕获原型走原路径（现有嵌套函数零改动），非空则 eraseFromParent 重建 llvm 函数为「原参数 LLVM 类型 + 等长尾随 ptr 捕获参数」；visitFunction 形参循环改按 nparams 下标 getArg、捕获绑定先入参数层（slot=收到的 ptr 形参、无 alloca、cell_id 透传、同名形参随后覆盖即遮蔽），形参/局部建槽处分配 cell_id；visitCall 正常实参后逐捕获 lookup_var 比对 cell_id 一致性——一致即同一单元（宿主 alloca / 自递归或内层转发所得 ptr）传其槽指针，不一致即该名被内层遮蔽无从取回登记槽 → 保守拒编不错编；体内读写经捕获 ptr 直达宿主槽（按引用共享：内层写外层可见、外层写内层迟绑定可见，对齐解释器闭包共享变量单元语义）；方法体内嵌套（单态化副本各帧重分配 cell_id、跨副本共享 ptr 悬垂）与 Tuple 捕获（解构无单槽）保守维持标识符不可见拒编（同 t104）；零新增 rt 接口 | 迟绑定读、内层写外层可见、捕获形参、多层逐层转发、自递归透传捕获、int/str/decimal/bool/number/byte/数组/类实例捕获、实例重赋后捕获、插值内引用捕获程序编译执行，输出与解释器一致；新差分用例 s91_closure_capture + 拒编用例 s91_reject_closure_tuple_capture/s91_reject_closure_method_capture/s91_reject_closure_shadowed_capture **✅ t141** |
| S89 | 动态 object（空 cls）字段读/写解锁：visitProperty / visitPropertyAssign 的 Obj 分支入口把原「空 cls 无静态布局→字段访问拒编」改为镜像 t133 方法动态分派——以**全体定义了该字段的类**（不限同树，object 可承载任意实例）为 switch case，按对象头类 id 运行期取值/存储；读路径各 arm 按定义者 struct 布局 GEP+load、字段声明 type/cls/bit_max 全等守卫后 PHI 合流，写路径值在 switch 前求值/coerce/bit 陷阱后各 arm store，default 走既有 `collie_rt_trap_undefined_property`（动态类实例无此字段，解释器同为运行期报错非 stdout 差分面）；lone_tree 快路径仅非空 cls 可达（真实类可能不在 defs 中，动态面恒走 switch 留 default 兜底）；各定义者同名字段声明类型不一致保守拒编 "inherited field 'X' with a different type"（解释器按运行期实际类可输出，PHI 无从统一，拒编不错编）；visitPropertyAssign 原空 cls 直落 classes_.at("") 抛 out_of_range 使编译进程崩溃，现纳入动态面修复；S87 拒编用例 s87_reject_dynamic_object_field 经此真实解锁退役；零新增 collie_rt 接口 | 跨树数组元素字段读、object 形参字段读+写回、object 变量承载实例字段读写、数组元素字段写（崩溃面修复）、单一类定义字段 case 命中、整数字段读程序编译执行，输出与解释器一致；新差分用例 s92_dynamic_object_field + 拒编用例 s92_reject_dynamic_object_field_type_mismatch；退役 s87_reject_dynamic_object_field **✅ t142** |
| S90 | 函数名作值（静态直接消费面）：新增 CGType::Fn（ptr 承载 llvm::Function* 自身，静态可知无运行期对象，~~不进任何动态 kind 表~~（S118 t171 起以运行期 tag 9 纳入动态承载，静态表示仍是函数 ptr 本身））；visitIdentifier 变量解析 miss 时改查顶层 functions_ 裸名直查（改编键含 '.' 不误命中嵌套/方法），fn_key 绑定命中出 Fn 值（t91 "function 'f' used as a value" 拒编面解锁）；消费面：print/toString/插值/"+" 拼接经 gen_print/to_str 新增 Fn 臂固定 `<function>`（对齐 Value::to_string Function 分支）、==/!= 任一侧 Fn 常量折叠 false/true（对齐 values_equal 函数比较落 default 恒 false——含同一函数 add==add，实证 false）；~~KW_OBJECT 声明以函数值初始化建 Fn ptr 槽（镜像 t120）、无初值 object 首赋函数值槽重绑 Fn（t121 吸收路径扩展）、Fn 槽同型重赋走 visitAssign 新分支异型拒编~~（t143 的「object 槽 Fn ptr 静态近似」已于 S118 t171 退役——`object o = add;` 改建 {tag,bits} 动态承载槽，其后异型/换承载位重赋随之解锁；无初值 `object o;` 首赋函数值仍落 t143 ptr 近似（槽布局不可中段改 {i64,i64}，其后异型重赋拒编，锁 s121_reject_object_uninit_fn_slot））；闭包捕获经 t141 按引用机制自动覆盖 Fn 槽（cell_id 建槽即分配）；间接调用经变量在语义层已拦（callee 需 FUNCTION 符号，双端同拒非差分面）；~~作实参/返回值/进数组/进类字段落各消费点既有 default 拒编不错编（各配 s93 拒编用例锁面）~~（四面已于 S118 t171 以 tag 9 动态承载解锁，三枚 s93 锁退役为差分用例 s121_fn_to_object_signature 第一、二节与 s121_fn_array_bits 第一节，进类字段面见 s121_fn_consume_control_flow）；零新增 collie_rt 接口 | 函数名 print/多参/toString/插值/双向拼接、==/!= 常量折叠（含 ×整数字面量/小数字面量）、object 变量承载+同型重赋+无初值首赋、静态 tuple 含函数值整体打印/常量下标深读/深比较、三目臂合流、嵌套函数名作值且直接调用回归、闭包捕获持函数值 object 变量程序编译执行，输出与解释器一致；新差分用例 s93_function_value + ~~拒编用例 s93_reject_function_as_argument/s93_reject_function_in_array/s93_reject_function_return_value~~（三锁已于 S118 t171 退役为差分用例，见本行范围外注） **✅ t143** |
| S91 | 异质数组含类实例元素（kind 7 tag 5）：把实例纳入 t139 的异质动态标量承载——`is_dyn_scalar` 本体不收 Obj（tuple 物化面 t140 的 "dynamic tuple with array/object/nested-tuple elements" 拒编不受影响），改在两处以 `|| == CGType::Obj` 放行：visitArrayLiteral 第三收敛判定（`(d, 1)`/`[1, new A(), "two", true]` → elem=Dyn）与 visitIndexAssign 的 elem==Dyn 整槽写；物化与写槽复用既有 `arr_kind_of(Obj)=5` + `elem_to_bits` PtrToInt 通道（零新接口、零新 rt 函数），读出仍是 CGType::Dyn；消费面按 Dyn 既有臂补 tag 5：rt `collie_rt_print_num`/`num_to_str` 加 tag 5 → `<object>`（对齐解释器 `Value::to_string` Instance 分支，与 kind 5 单数组 t100 同规则）、`arr_to_str` case 7 的 tag switch 加 case 5 → `<object>`（整体打印/toString/插值含实例位即 `[<object>, 2]`）、`arr_eq` kind7 逐元素加 `lt == 5 → return 0`（解释器 values_equal 无 Instance 分支，实例恒不等，含同一实例 `a == a` 亦 false）；`bits_to_elem`/`arr_kind_of`/`elem_to_bits` 早已支持 kind 5，实例位读出为 Dyn 后 `bit_max`/uninit 不涉及；t139 的 s89_reject_hetero_obj_array（`[1, new A()]` 解释器可打印 vs codegen 拒编）经此真实解锁退役为差分面，新拒编用例锁三面：~~Dyn 元素上的字段访问（`a[0].name`，解释器按运行期实例出 "rex" vs codegen "property access" 拒编——读出静态类型是 Dyn 而非 Obj，无类名可解码字段，维持拒编不错编）~~（经 S92 t145 tag==5 守卫并入 t142 动态分派真实解锁，s94_reject_dyn_elem_field_access 退役）、~~Dyn 值进 object 槽（`object o = a[0]`，落既有 "initializing 'object' variable with non-object value" 拒编）~~（经 S93 t146 object 动态承载槽真实解锁，s94_reject_dyn_elem_to_object_slot 退役）、~~Obj×Arr 混排字面量（`[d, [1, 2]]` 维持 "heterogeneous array literal"，嵌套数组无 tag 承载位）~~（Obj×Arr 已于 S107 t160 以 kind 7 tag 4 真实解锁，s94_reject_obj_nested_array_mix 退役为差分用例 s110_obj_nested_array_mix）| `[d, 1, "two", true]` 整体打印/toString/插值、逐元素与负索引读（实例位 `<object>`、标量位原样）、length 与循环遍历、`==`/`!=` 深比较（含实例位恒不等，同字面量 `a == a` 亦 false）、kind7×非 kind7 恒不等、整槽写（实例覆标量/标量覆实例/Dyn 值透传写）、跨树两实例混排 `[new Dog(), new Cat(), "end"]`、动态 object 变量入数组、父类静态类型实例入数组、Int 数组变量整体重赋含实例异质数组（elem 保持 Dyn）、bool/tribool/decimal 与实例混排程序编译执行，输出与解释器一致；新差分用例 s94_hetero_obj_array + 拒编用例 s94_reject_dyn_elem_field_access（→ S92 t145 解锁退役）/s94_reject_dyn_elem_to_object_slot（→ S93 t146 解锁退役）/s94_reject_obj_nested_array_mix（→ S107 t160 解锁退役）；退役 s89_reject_hetero_obj_array **✅ t144** |
| S92 | kind 7 元素读出（Dyn）上的字段读/写与方法调用：新增成员 helper `dyn_obj_guard`——接收者静态类型为 Dyn 时在 `visitProperty`/`visitPropertyAssign`/`visitMethodCall` 三入口并入分派守卫：运行期比较 tag 与 5（实例位），命中经 `IntToPtr(bits)` 还原实例 ptr 并重绑 `{ptr, Obj, cls 空}`，直接并入既有动态 object 分派面（字段读写走 t142 全体定义者按对象头类 id switch + 字段声明全等守卫 + PHI 合流；方法调用走 t133 全体定义者 switch，含 toString 内建 `<object>` 兜底与跨类签名全等守卫）；未命中（标量位）落既有 `collie_rt_trap_undefined_property`/`trap_undefined_method` 陷阱报错退出（拒错编从陷阱：解释器 `1.name` 同为运行期报错；标量自有成员如数值 `.abs()`、字符串位 `.length`/`.trim()` 解释器可运行而 codegen 无统一标量成员承载，同归陷阱面非静默错值）；守卫置于接收者求值后、实参/写值求值前，陷阱路径 stdout 无增量；继承字段实证由 t142 子类前缀布局臂自然承载（`[an,0][0].tag` 子类实例读父类字段双端一致）；原 s94_reject_dyn_elem_field_access 面经此真实解锁退役为差分；零新增 collie_rt 接口 | 实例位字段读/负索引读/写回（与源变量别名同步可见）、跨树两类同名字段读写、方法无参/带参/none 返回副作用、方法内 this 字段、toString() 内建兜底 `<object>`、链式 `.name.length`（字段读结果静态 Str）、循环变量索引守卫实例位字段读、整槽重写后新实例字段解码、父类静态类型入数组按对象头类 id 分派子类覆写方法、object 变量共面回归程序编译执行，输出与解释器一致；新差分用例 s95_dyn_elem_member + 退役 s94_reject_dyn_elem_field_access **✅ t145** |
| S93 | object 动态承载槽（非 Obj 初始值）：新增成员 helper `to_dyn` 组装 kind 7 {tag,bits}（Num/Dyn 原样透传、Obj 打 tag 5 存 `PtrToInt` 实例 ptr（同 t144 编码）、~~其余标量按 `arr_kind_of`+`elem_to_bits`~~其余标量与数组按 `arr_kind_of`+`elem_to_bits`（数组 tag 4 存内层数组 ptr，S107 t160））；visitVarDecl KW_OBJECT 非 Obj 分支——初始值 ∈ is_dyn_scalar（Int/Double/Num/Bool/Str/Tri/Dyn；S107 t160 起判定收束为 `is_dyn_carrier`，另收 Obj/Arr）时不再落 "initializing 'object' variable with non-object value" 拒编，改建 CGType::Dyn 聚合槽 store to_dyn、登记 cell_id 可捕获（t141 按引用机制自动覆盖），Obj 初始化（t120 静态类名槽）路径不变零回归；~~函数初值建 t143 Fn ptr 槽的近似路径亦于本分支退役（S118 t171 起 `object o = add;` 直接落本行 Dyn 聚合槽；无初值 `object o;` 首赋函数值仍走 t143 ptr 重绑，锁 s121_reject_object_uninit_fn_slot）~~；visitAssign 新增 var->type==Dyn 分支——重赋整槽替换（标量/Dyn 经 to_dyn、Obj 打 tag 5，读回恒 Dyn）；消费面全复用既有臂：print/toString/插值/拼接（rt_print_num/num_to_str tag 分派，tag 5 → `<object>`）、==/!=（既有 Dyn 比较臂对齐 values_equal——实例位无 Instance 分支恒 false）、字段读写/方法调用（t145 dyn_obj_guard 实例位守卫并入 t142/t133 动态分派）；进 kind 7 异质数组字面量（is_dyn_scalar(Dyn) 透传）；零新增 collie_rt 接口。残余面锁拒编：Obj 初始化的静态类名槽重赋标量/Dyn（ptr 槽不可中段改聚合槽，"assigning incompatible value"）、~~Arr/Tup 初始化 object 槽（kind 7 无承载位，"initializing 'object' variable with non-object value"）~~（Arr 已于 S107 t160 以 tag 4 承载解锁，s96_reject_object_slot_array_init 退役为差分用例 s110_object_slot_array_init；Tup 初始化 object 槽仍拒编）；标量位成员消费（~~串位 `.length`~~（已于 S99 t152 以 `collie_rt_dyn_len` 按运行期 tag 解锁）、数值 `.abs()`——解释器可运行）落 t145 守卫陷阱或既有拒编，非静默错值面 | kind 7 元素读出（实例位/标量位）进 object 槽打印、整数/小数/串/bool 直接初始化、toString/插值/"+" 双向拼接、实例位字段读与覆写方法调用（守卫+动态分派）、重赋标量↔标量/Obj 进槽/Dyn 进槽、`o == d`/`o != d` 相等折叠（恒 false/true 对齐解释器）、object 槽值入异质数组字面量、闭包捕获 object 动态槽（体内随重赋读取）、循环局部 object 变量逐元素承载程序编译执行，输出与解释器一致；新差分用例 s96_object_dyn_slot + 拒编用例 s96_reject_obj_slot_scalar_reassign/~~s96_reject_object_slot_array_init~~（→ S107 t160 解锁退役）；退役 s94_reject_dyn_elem_to_object_slot **✅ t146** |
| S94 | 动态承载值（Dyn）进标量类型化槽的运行期拆槽：新增成员 helper `dyn_unbox_to`（输入 {tag,bits}，按目标类型生成 tag 判定链 `dynbox.hit`/`dynbox.next` + PHI 合流，全不命中落新 rt `collie_rt_trap_dyn_convert(tag, target)` 报错退出）+ 静态 `is_dyn_unbox_target`/`bit_type_name`；四个汇聚点同解释器单一 `coerce_to_declared` 覆盖面——`coerce_for_slot`（声明初值/赋值/类字段/构造器实参，`store_tuple_var` 逐槽写显式传 `allow_dyn_unbox=false` 维持拒编）、`coerce_call_arg`（形参）、`visitReturn`（返回值）、`visitVarDecl` byte/word 分支（拆整数位后接既有 `check_bit_range`）；接受表严格照解释器：integer←tag0（小数态落 "cannot assign decimal value to 'integer' variable" 特例文案）、decimal←0(SIToFP)/1(BitCast)、number←0/1 原样透传（tag+bits 即 Num 表示）、bool←tag2 Trunc、tribool←tag6 Trunc + tag2 Select 加宽、string←tag3 IntToPtr 加数值/布尔隐式转串（复用 `to_str` 同源 rt_i64_to_str_/rt_f64_to_str_/rt_bool_to_str_，对齐 KW_STRING 分支）、byte/word 陷阱文案经 `dyn_target_name` 透传声明类型名（解释器报 'byte'/'word' 非 'integer'）、char/character 由 `declared_cgtype` 既有 Str 承载自然覆盖；来源全复用既有 Dyn 通道（kind 7 元素读 t139、异质 tuple 非常量索引 t138、动态 tuple 索引/`get` t140、object 动态承载槽 t146），拆槽后落点即静态类型，下游算术/比较/length/拼接/再入容器全走既有面；未命中陷阱路径 stdout 无增量（守卫在写槽前）。拒编锁面：~~Dyn→类名（Obj）声明/形参/返回（静态类槽按声明类前缀偏移取址，运行期类不可保证）~~ **已于 S105 t158 以运行期 tag＋类 id 双守卫解锁**（另覆盖变量重赋/类名字段槽/kind 5 实例数组元素槽）、Dyn→同质数组元素槽（元素静态 kind 不可证）、Dyn→tuple 逐元素槽（解释器 tuple 元素无类型，拆槽即错型）；新增 1 个 rt 接口 | 六标量落点×各 tag 命中（含 integer 只收整数态、decimal 双态、tribool 收 bool 加宽、string 收数值/布尔隐式转串）、拆槽后下游算术/比较/length/拼接、既有类型变量重赋、byte/word/char 落点、动态 tuple 索引与异质 tuple 非常量索引读出进槽、object 动态承载槽值进标量槽、类字段赋值/构造器实参/方法实参、函数实参与返回值拆槽、循环与条件区内非常量索引逐轮拆槽、内层函数按引用捕获外层局部（t141）写拆槽值、拆槽值写 kind 7 异质数组槽程序编译执行，输出与解释器一致；13 组 tag×目标未命中（整数槽收小数/串、bool 槽收串/小数、decimal 槽收 bool、byte 槽收串/越界值）双端同报错消息；新差分用例 s97_dyn_to_slot + 拒编用例 s97_reject_dyn_to_class_decl/s97_reject_dyn_to_class_arg/s97_reject_dyn_to_class_return/s97_reject_dyn_to_homog_array_slot；退役 s88_reject_dyn_string_slot/s89_reject_dyn_string_slot/s90_reject_dyn_tuple_string_slot **✅ t147** |
| S95 | 动态承载值（Dyn）参与算术的运行期数值守卫与 '+' 字符串位分派：新增成员 helper `dyn_num_operand`（Dyn 侧 `tag < 2` 无符号判定——整数态/小数态 {tag,bits} 与 Num 同构，直接透传进既有 `collie_rt_num_arith`（int×int BigInt 精确、'/' 恒真除法、% 为 floor 取模、decimal 混合全由已对齐 rt 语义免费承载），其余位（bool/tribool/串/数组/实例）落新 rt `collie_rt_trap_non_numeric_arith` 报错退出，消息核心对齐解释器 `eval_arithmetic` 的 "Arithmetic operands must be numbers"）+ `dyn_concat_or_add`（'+' 专用：解释器"任一侧字符串即两侧 to_string 拼接"依赖运行期性质，故按 tag==3 分派——命中走 `collie_rt_concat(to_str 双侧)` 结果打 tag 3、未命中双侧数值守卫后下沉 `rt_num_arith(op 0)`，PHI 合流回 CGType::Dyn，串在左/右的不对称结果 `x7`/`7x` 均可复现）；visitBinary 五算术臂（+ - * / %）在有 Dyn 侧时先行 `require_dyn_arith`（双侧 ∈{Int,Double,Num,Dyn}，含字符串/布尔等其余静态组合维持既有 "non-numeric operand of 'X'" 拒编不错编）再走守卫路径，`-`/`*`/`/`/`%` 结果打 Num、`+` 结果打 Dyn；操作数求值固定先 lhs 后 rhs（陷阱取首个非数值位，避开参数求值顺序未定带来的漂移）；来源复用全部既有 Dyn 通道（kind 7 元素读 t139/含实例位 t144、异质 tuple 非常量索引 t138、动态 tuple 索引与 `get` t140、object 动态承载槽 t146）；算术结果可再进标量类型化槽（S94 拆槽）、再参与运算、作函数形参/返回值、类字段与方法内自增、闭包捕获外层局部自增、循环内非常量索引逐轮守卫。拒编锁面：~~Dyn 参与比较（`a[0] < a[1]` / `a[2] < "y"`——解释器 `eval_comparison` 走数值视图或字典序可运行，codegen 比较面双侧需静态数值或字符串表示 → "non-numeric operand of '<'"）~~（已于 S96 t149 以 rt `collie_rt_dyn_eq`/`collie_rt_dyn_rel` 运行期 tag 分派解锁，`s98_reject_dyn_compare` 退役）、~~Dyn 作下标（`src[idx[i]]`——解释器整数位可运行 → "non-integer index"）~~（已于 S97 t150 以整数位 bits 透传 + 小数位走既有 floor/clamp + 其余位 rt `collie_rt_trap_index_kind` 解锁，`s98_reject_dyn_index` 退役）；非数值位在四则消费点为运行期陷阱（stdout 无增量）；新增 1 个 rt 接口 | Dyn×静态数值与 Dyn×Dyn 的四则与取模（int/decimal 混合、负数 floor 取模 `-9 % 4 == 3`、真除法 `-9 / 2.5 == -3.6`）、结果再运算与进 integer/decimal/number 槽、'+' 字符串位双向分派（`x7`/`7x`/`xtrue`/`7y`）、异质 tuple 与动态 tuple 索引算术（原 s88/s90 拒编面）、object 动态承载槽整数位与字符串位算术、函数实参（含 Dyn 拆槽入参）与返回、类字段 `this.v` 算术回写、if/while 分支体、闭包捕获局部逐次自增；4 组非数值位（bool/tribool/串/实例）进 `+ * /` 双端同报错消息核心；新差分用例 s98_dyn_arith + 拒编用例 ~~s98_reject_dyn_compare~~（已于 S96 t149 退役）/s98_reject_dyn_index；退役 s88_reject_dyn_arith/s89_reject_dyn_arith/s90_reject_dyn_tuple_arith **✅ t148** |
| S96 | 动态承载值（Dyn）参与比较的运行期 tag 分派：新增成员 helper `dyn_cmp_box`（静态侧按数组 kind 编码打框——Int→tag0、Double→tag1 BitCast、Str→tag3 PtrToInt、Num/Dyn 原样透传 {tag,bits}，其余（Obj/Arr/Tup/Fn/Bool/Tri）落 "comparison of dynamic value with this type" 保守拒编不错编）+ 新增 2 个 rt 接口：`collie_rt_dyn_eq`（判等，复刻解释器 `values_equal` 接受表——tribool 分支先行（两侧须 tribool/bool，bool 按 false→0/true→2 加宽，unset 恒不等任何 bool）、tag 不同仅 Number 混合态（0/1 互配）走 `collie_rt_num_cmp` 双视图（5 == 5.0）、同 tag 下 0/1 数值、2 bool 位模式、3 串 `strcmp` 内容、4/7 复用 `collie_rt_arr_eq` 深比较（kind7×kind7、kind5 实例位恒不等、跨 kind 恒 0 均已是同源规则）、5/其余落 default 恒 false；**无陷阱臂**（解释器 `values_equal` 从不抛错）；`collie_rt_dyn_rel`（关系比较 op 2/3/4/5：双侧数值→ `collie_rt_num_cmp` 继承已对齐的 int 精确/混合 double/NaN IEEE 语义，双侧串位→ `strcmp` 字典序，其余组合打 "Comparison operands must be both numbers or both strings" 后 exit(1)，对齐 `eval_comparison`）；visitBinary 比较臂（== != < <= > >=）新增 `if (has_dyn)` 分支，插入位置严格在既有 Void/Tup/Arr/Obj/Fn 相等折叠与 Str×Str/Tri/Bool 各臂**之后**、`has_num` 静态数值臂**之前**（此前已编译的 `ov == a[0]` 恒 false 折叠等面零回归）；!= 由同一 eq 取反；操作数求值固定先 lhs 后 rhs（同 S95，避开参数求值顺序未定带来的漂移）；结果恒 CGType::Bool，可进 if/while/三元/逻辑运算、作函数实参与返回值、写回 kind 7 槽；来源复用全部既有 Dyn 通道（kind 7 元素读 t139/含实例位 t144、异质 tuple 非常量索引 t138、动态 tuple 索引与 `get` t140、object 动态承载槽 t146、S95 算术结果）。拒编锁面：~~Dyn 作下标（`src[idx[i]]`，s98_reject_dyn_index 续锁）~~（已于 S97 t150 解锁，`s98_reject_dyn_index` 退役）；Dyn×Obj/Arr 静态侧（`cmp_side_ok` 圈定，运行期类/嵌套承载不可静态统一）。语义层双端同拒面非差分、不加锁：`a[i] == 字面量`（含 `a[2] == sv`/`a[3] == bv`/`a[0] == 5.0`/`a[0] == av`）、`tv == a[4]`、类方法体内 `this.field == x`（"Incomparable operand types"，关系比较 `this.field < x` 可用） | 判等矩阵跨 tag 命中与不命中（整数×小数双视图 `5 == 5.0`、串位内容相等与不等、bool×tribool 加宽、unset 恒不等任何 bool、实例位恒不等含同一实例 `o == o`、Dyn×静态 Int/Double/Str 双向、异质数组与动态 tuple 索引、object 动态承载槽整数位/串位）、关系比较数值两路（int 精确、混合 double、NaN 恒 false）与同串位字典序、比较结果进 if/while 循环逐轮、三元臂取、方法实参与返回（`Gate.allow(a[0])`/`notless(a[1])`）、闭包捕获外层比较、比较结果写回 kind 7 槽再读出程序编译执行，输出与解释器一致；Dyn×串与串×数值的关系比较（int vs str）双端同报 "Comparison operands must be both numbers or both strings" 消息核心（diff 门禁要求两端 rc=0，陷阱面只手工探针验证不注册用例）；新差分用例 s99_dyn_compare；退役 s98_reject_dyn_compare（经此真实解锁，非降格迁就测试）；新增 2 个 collie_rt 接口 **✅ t149** |
| S97 | 动态承载值（Dyn）作下标的运行期 tag 分派：`index_to_int`（四个汇聚点——数组读/数组写/串读/嵌套数组读的唯一下标入口）新增 `CGType::Dyn` 臂——解释器 `normalize_index` 先判 `is_number()` 再判 `floor`，故 tag 0（整数态）bits 即 i64 经 PHI **零转换透传**，tag 1（小数态）`BitCast` 还原 double 后走既有 floor 判定 + ±4e18 clamp + FPToSI 路径（`1.0` 可用、`2.5` 与 NaN 落既有 "Index must be an integer"），其余位（bool/tribool/串/实例）落**新增 rt** `collie_rt_trap_index_kind(tag)`，消息核心照解释器 "Index must be a number, got <kind>"（tag→kind 名映射按 `Value::kind_name`：2 bool/3 string/4 array/5 object/6 tribool/7 array；裸消息同既有索引陷阱家族）；既有 double 降址逻辑抽为 helper `index_from_double`（Double/Num 臂与 Dyn 小数态共用，PHI 入边块取 helper 返回时的插入块 `idxint.cont` 而非 `dynidx.dec`，否则 IR 验证报 PHI 前驱不匹配）；负索引回绕与越界全复用既有 rt（`rt_arr_norm_index`/`rt_str_index`），越界消息 "Index 9 out of range (size 3)" 双端同源；静态侧 Bool/Str/Obj/Arr/Tup/Fn 下标维持既有 "non-integer index" 拒编不错编。来源复用全部既有 Dyn 通道（kind 7 元素读 t139/含实例位 t144、异质 tuple 非常量索引 t138、动态 tuple 索引与 `get` t140、object 动态承载槽 t146、S95 算术结果）；`s98_reject_dyn_index`（`src[idx[i]]`，解释器整数位取元素得 20 vs codegen 拒编）经双端实证真实解锁退役为差分面；新增 1 个 collie_rt 接口 | kind 7 整数位作下标（正/负索引）、小数位整数值（`1.0`/`-2.0`）、循环内非常量下标逐轮 tag 判定（异质 `picks` 数组）、串下标 `s[Dyn]` 与负索引、下标写回（`src[Dyn] = 99`、kind 7 槽 `idx[Dyn] = 2` 写后读回可见）、异质 tuple 非常量索引读出作下标、嵌套数组（外层 kind 4）作下标对象、kind 7 自指 `idx[picks[0]]`、object 动态承载槽作下标且重赋后可见、函数形参（number/Dyn 拆槽）与返回值链路、类方法内 `this.data[i]`、闭包捕获外层局部改值后作下标（t141）、条件区内与算术结果作下标、动态 tuple 索引作下标程序编译执行，输出与解释器一致；6 组陷阱位（串/bool/tribool 位、小数位非整数值、实例位、进 kind 7 与串的越界值）双端 rc=1 且消息核心一致、stdout 无增量（diff 门禁要求两端 rc=0，陷阱面仅手工探针验证不注册用例）；新差分用例 s100_dyn_index + ~~拒编用例 s100_reject_obj_param_index（object 动态形参作下标——Obj 承载无静态数值表示）~~（该锁面已于 S108 t161 解锁退役，程序扩为差分用例 s111_obj_param_index）；退役 s98_reject_dyn_index **✅ t150** |
| S98 | 动态域（数组经函数签名/字段/返回值边界后 elem=Num 哨兵）元素读出按运行期 kind 承载，**消除缺口 CG9**：visitIndex 的 `elem == Num` 臂由「kind > 1 调 `collie_rt_trap_arr_kind` 陷阱退出」改为运行期 kind 分派——kind 7 经 `rt_arr_get_dyn` 取回真实 `{tag,bits}`（双步长槽，负索引/越界陷阱复用 rt），kind 0/1 与 number tag 编码重合 bits 原样透传，kind 2/3/4/5/6 槽位模式即 Dyn 的 bits 编码；两路 PHI 合流统一产出 `CGType::Dyn`（与 Num 同构，数值位读出行为不变），消费面全走 t147–t151 既有 Dyn 臂（print/转串/插值/==/算术/比较/作下标/进类型化槽/写回动态域）；rt 侧 `collie_rt_num_to_str`/`collie_rt_print_num` 补 tag 4/7 → `collie_rt_arr_to_str`（动态域嵌套与异质元素转串对齐解释器 Array 分支），`collie_rt_arr_set_num` 补 kind==7 目标落双步长槽（异质数组就地写回不再落 CG7 陷阱）、`visitIndexAssign` 补 Dyn 值分量直传；删除 `collie_rt_trap_arr_kind` 接口与 `rt_trap_arr_kind_` 注册 | 动态域逐 kind 读出转串/插值/print（kind 0/1/2/3/6 标量、kind 4 嵌套数组、kind 7 异质含串与 bool）、`==`/`!=` 深比较、读后写回就地交换（bool/str/异质数组跨签名边界引用语义）、类数组字段动态域读写与混排槽写回、kind 5 实例位读出 `<object>` 并进 object 动态承载槽后字段/方法可用、数值读出进类型化槽/tuple/数组字面量（静态类型由 Num 变 Dyn 不缩已解锁面）程序编译执行，输出与解释器一致；新差分用例 s101_dyn_domain_read + 拒编用例 s101_reject_dyn_elem_length（动态域串元素 `.length`——解释器按 String 承载可取长，Dyn 属性面维持拒编不错编；**已于 S99 t152 双端实证同为 `len: 2` 后退役**）；原 CG9 运行期陷阱面仅手工探针、无锁定用例可退役 **✅ t151** |
| S99 | 动态承载值（Dyn）的长度面——`.length` 属性与 `len()` 内建按运行期 tag 分派：新增 collie_rt 接口 `collie_rt_dyn_len(tag, bits, mode)`（mode 0 = `.length`、1 = `len()`），tag 3 → `collie_rt_str_len`（UTF-8 码点数）、tag 4/7 → `collie_rt_arr_len`（元素个数）、tag 5 按 mode 报 `Undefined property 'length' on object` / `len() expects a string or array, got object`、tag 0/1/2/6 按 mode 报 `Property 'length' is only supported on strings and arrays, got <kind>` / `len() expects a string or array, got <kind>`（kind 名对齐 `Value::kind_name`，0/1 同报 number），接受表逐条复刻解释器 `visitProperty` 的 length 分支与 `call_builtin_len`；codegen 两触点：`visitProperty` 在 t145 实例位守卫**之前**加 `Dyn && name == "length"` 臂（返 `CGType::Int`），`len` 内建加 `Dyn` 臂；`.length` 臂带静态守卫——全程序无类声明名为 `length` 的字段（`classes_` 扫描），有则整面回落 t145 `dyn_obj_guard` 的实例位字段分派（t142/t145/t146 已交付面零回归，串/数组位的长度面属该残余面）；来源复用全部既有 Dyn 通道（kind 7 元素读 t139/含实例位 t144、动态域元素读 t151、object 动态承载槽 t146、动态 tuple 索引与 `get` t140/t84、拼接结果 t148、闭包捕获 t141）；结果恒 Int，作下标/插值/条件/算术/进数组字面量与 tuple 全走既有面；零删除接口，collie_rt 净接口数 +1 | 动态域串/嵌套数组元素、kind 7 异质元素、循环内非常量下标元素、object 动态槽（含重赋后可见）、动态 tuple 索引与 `get` 串位、拼接结果、类数组字段元素、返回值动态域、闭包体内取长九路 `.length`/`len()` 程序编译执行，输出与解释器一致；`.length` 结果进下标/插值/条件/`==`/算术/数组字面量/tuple 与 while 界；新差分用例 s102_dyn_length（A–P 共 17 行输出双端逐字节一致）+ ~~拒编用例 s102_reject_obj_param_length（object 动态形参取长，解释器按承载可取长）~~（该锁面已于 S108 t161 解锁退役，程序扩为差分用例 s111_obj_param_length）；退役 s101_reject_dyn_elem_length（`a[0].length` 双端同为 `len: 2`，真实解锁）；陷阱五组（number 整数态/小数态、bool、tribool、实例位 × `.length`/`len()`）双端 rc=1 且消息核心一致，按门禁约定仅手工探针不注册用例；类声明 `length` 字段的双端回归（静态 `h.length` 与 Dyn 实例位字段读一致、串位落 t145 陷阱行为不变）；ctest -C Release 全量 125/125 **✅ t152** |
| S100 | 动态承载值（Dyn）作索引接收者——嵌套容器索引 `a[i][j]` 按运行期 tag 分派读写：新增 collie_rt 接口 `collie_rt_dyn_index(tag, bits, index, otag, obits)` 与 `collie_rt_dyn_set(tag, bits, index, vtag, vbits)`（外加 static `collie_rt_dyn_kind_name`），接受表逐条复刻解释器 `visitIndex`/`visitIndexAssign`——tag 3 → `collie_rt_str_index`（UTF-8 码点、负索引归一、越界陷阱复用，读结果仍以 tag 3 回 Dyn）、tag 4/7 → 内层数组经 `collie_rt_arr_norm_index` 归一后按内层 kind 取槽（kind 7 读双步长槽 `{tag,bits}`，kind 0/1/2/3/4/5/6 槽位模式即 bits、tag 恒等于内层 kind），其余 kind 报 `Only arrays, strings and tuples can be indexed, got <kind>` / 写侧 `Only arrays can be index-assigned, got <kind>`（串写报 `Strings are immutable, cannot assign to index`），kind 名对齐 `Value::kind_name`（0/1 同报 number、4/7 同报 array）；写路径直接复用 `collie_rt_arr_set_num`（就地保持 CG7 tag/kind 失配陷阱策略），零删除接口，collie_rt 净接口数 +2；codegen 两触点：`visitIndex` 把接收者守卫放宽到 `Str/Arr/Dyn` 三型并在 Dyn 分支经两个 entry alloca 出参 PHI 合流产出 `CGType::Dyn`，`visitIndexAssign` 同放宽、值侧 `to_dyn` 打框（异质标量/实例位/内层数组三系，Arr 经 `arr_kind_of`→4，`is_dyn_scalar` 不放宽以保住 `s94_reject_obj_nested_array_mix`（S107 t160 承载判定收束为 `is_dyn_carrier` = `is_dyn_scalar` ∪ {Obj, Arr}，数组位写入经 `arr_kind_of`→4 真实解锁，该拒编锁退役为差分用例 s110_obj_nested_array_mix）），结果沿用右侧值以支持链式；来源与消费面全复用既有通道（动态域元素读 t151、kind 7 元素读 t139/t144、object 动态槽 t146、动态 tuple 索引与 `get` t140/t84、拼接 t148、闭包捕获 t141），读出 Dyn 可继续嵌套索引、取长（S99）、参与 print/转串/插值/`==`/算术/作下标/进类型化槽；顺带修复两处静默错编：`collie_rt_arr_to_str` 的 kind 7 内层 switch 缺 tag 4/7 臂（曾落到串分支把数组指针当 `char*` 打出空串/乱码）与 `collie_rt_arr_eq` 的 kind 7 逐元素比较缺 tag 4/7 臂（曾按指针位比较，内容相同的嵌套数组误判不等），两者改为递归 `collie_rt_arr_to_str`/`collie_rt_arr_eq` | 动态域嵌套数组/串/三层/异质元素读、循环内非常量下标、object 动态槽读写与重赋后可见、类数组字段 `this.rows[i][j]` 方法内读写、tuple 元素位再索引、闭包体内捕获值再索引、写后整槽 print 联动与链式赋值、kind 7 槽含数组位转串与深比较程序编译执行，输出与解释器一致；新差分用例 s103_dyn_nested_index（A–S 共 23 行输出双端逐字节一致）；陷阱矩阵七文件（number 整数态/小数态、bool、tribool、实例位作接收者、串索引写、越界下标）双端 rc=1 且消息核心一致，仅手工探针验证、不注册用例（差分门禁要求两端 rc=0）；静态 kind 数组写异类值的 CG7 陷阱面与 object 动态形参/返回值成员消费面维持拒编不错编 **✅ t153** |
| S101 | 动态承载值（Dyn）上的串方法——`.trim()` / `.trimLeft()` / `.trimRight()` / `.subString(start[, end])` 按运行期 tag 分派：新增 collie_rt 接口 `collie_rt_dyn_str_trim(tag, bits, mode, name)` 与 `collie_rt_dyn_str_substring(tag, bits, start, end, name)`（外加 static `collie_rt_dyn_str_method_trap`），tag 3 → 复用既有 `collie_rt_str_trim` / `collie_rt_str_substring` 垫片（零语义复制：trim 只剥空格与 Tab、subString 按 UTF-8 码点区间且 `end==-1` 取 length），其余位复刻解释器两类消息——实例位（tag 5）走解释器方法查找失败分支 `Undefined method '<name>' on object`，其余走 `Method '<name>()' is only supported on strings, got <kind>`（kind 名对齐 `Value::kind_name`）；codegen 一触点：`visitMethodCall` 在 t145 `dyn_obj_guard` **之前**加 Dyn 串方法臂，静态守卫「全程序无任何类定义同名方法」（扫 `classes_` 的 `dispatch`）——命中守卫的极程序整面回落 t145 实例位方法动态分派（t133/t145 已交付面零回归，该程序中串位取长以外的串方法维持运行期陷阱为残余面）；`subString` 的下标参数降级提取为成员 helper `sub_string_index`（原 `visitMethodCall` 内 lambda），静态串接收者与 Dyn 接收者两路共用同一套 NaN / 精确 -1.0 / floor / clamp [0,4e18] 预处理（t105 语义零漂移）；结果恒 Str，进 print/`==`/插值/`.length`/再索引/写回动态域全走既有面；来源复用全部既有 Dyn 通道（kind 7 元素读 t139/含实例位 t144、动态域元素读 t151、object 动态承载槽 t146、动态 tuple 索引与 `get` t140/t84、拼接结果 t148、闭包捕获 t141、Dyn 再索引 t153）；零删除接口，collie_rt 净接口数 +2 | 动态域串元素、kind 7 异质元素、object 动态槽（含重赋后可见）、动态 tuple 索引与 `get` 串位、`+` 拼接结果、类数组字段元素（含方法内 `this.items[i]` 读写）、闭包体内捕获值、Dyn 再索引出的内层串位八路 trim 三兄弟与 subString 程序编译执行，输出与解释器一致；非常量下标参数、Double 参数（`subString(1.5)`）、`end` 传 -1/超大值钳制、`start >= end` 空串、结果进 `.length`/`==`/插值/写回动态域；新差分用例 s104_dyn_string_methods（A–R 共 20 行输出双端逐字节一致）+ 拒编用例 s104_reject_dyn_to_number（Dyn 上的 `toNumber()` 解释器按承载可转数、codegen 维持拒编不错编，锁 t155 候选面；**已于 S102 t155 双端实证同为 `42` 后退役**）；陷阱矩阵双端 rc=1 且消息核心一致（整数位/小数位/bool/tribool/数组位报 "Method 'trim()' is only supported on strings, got number|bool|tribool|array"、实例位报 "Undefined method 'trim' on object"，diff 门禁要求两端 rc=0 故仅手工探针不注册用例）；类定义同名 `trim` 方法时的整面回落双端回归（静态 `c.trim()` 与 Dyn 实例位 `a[0].trim()` 同为 `T`）；ctest -C Release 全量 128/128 **✅ t154** |
| S102 | 动态承载值（Dyn）的 toNumber——内建形式 `toNumber(d)` 与方法形式 `d.toNumber()` 按运行期 tag 分派转数：新增 collie_rt 接口 `collie_rt_dyn_to_number(tag, bits, otag, obits, mode)`（mode 0 = 内建形式、1 = 方法形式），接受表逐条复刻解释器 `to_number_value`——tag 0/1 数值位原样透传（整数态保持整数表示，不加宽为小数）、tag 2 bool 转整数 0/1、tag 3 串解析复用既有 `collie_rt_str_to_num` 垫片（剥空白/严格大小写 Infinity/纯整数精确/strtod 等价，零语义复制）、其余位报错，且**两类消息按形式分开**：方法形式的实例位（tag 5）走解释器 `visitMethodCall` 的方法查找失败分支 `Undefined method 'toNumber' on object`，其余（含内建形式的实例位）报 `toNumber() cannot convert <kind>`（kind 名对齐 `Value::kind_name`，`collie_rt_dyn_kind_name` static 复用）；出参写回 `{tag,bits}` 沿用 str_to_num 的 ABI 规避；codegen 三触点：`to_number_num` 加 `case CGType::Dyn`（内建形式统一入口，函数形式与方法形式共用成员 helper `dyn_to_number(v, method_form)`，entry alloca + PHI-free 直读两出参）、`visitMethodCall` 在 t145 `dyn_obj_guard` **之前**加 `Dyn && name == "toNumber" && 无实参` 臂，静态守卫与 t154 同策略（扫 `classes_` 的 `dispatch` 全程序无同名类方法）——命中守卫的极程序整面回落 t145 实例位方法动态分派，结果恒 `CGType::Num`（非 Dyn），故下游算术/比较/插值/print/`number`·`integer` 类型化槽（S94 拆槽）全走既有零新增消费面；来源复用全部既有 Dyn 通道（kind 7 元素读 t139/含实例位 t144、动态域元素读 t151、object 动态承载槽 t146、动态 tuple 索引与 `get` t140/t84、拼接结果 t148、闭包捕获 t141、Dyn 再索引 t153）；零删除接口，collie_rt 净接口数 +1 | 动态域元素、kind 7 异质元素（含串/bool/tribool/整数/小数位）、object 动态槽（含重赋后可见）、动态 tuple 非常量索引与命名 `get`、`+` 拼接结果、类数组字段元素（方法内 `this.items[i]`）、闭包体内捕获值、Dyn 再索引出的内层串位、循环内非常量下标元素九路内建与方法形式程序编译执行，输出与解释器一致；串解析面覆盖纯整数/小数/带空白/不可解析（`"abc"`→NaN）、严格大小写 `+Infinity`/`-Infinity`、宽松拼写 `"infinity"`→NaN、科学计数 `1e3`、十六进制 `0x10`（两端同为 strtod/stod 的 16）；结果进 `+ 1`/`* 2`/`== 42`/`< 100`/插值/`number` 与 `integer` 类型化槽；新差分用例 s105_dyn_to_number（A–S 共 21 行输出双端逐字节一致）+ s105_dyn_same_name_method（同名类方法守卫回落态的实例位分派零回归，A–C 共 3 行）；退役 s104_reject_dyn_to_number（`a[0].toNumber()` 双端同为 `42`，真实解锁）；陷阱四组（tribool 位/实例位方法形式/实例位内建形式/数组位）双端 rc=1 且消息核心一致（`toNumber() cannot convert tribool|array|object`、`Undefined method 'toNumber' on object`），差分门禁要求两端 rc=0 故仅手工探针不注册用例；残余面：类定义同名 `toNumber` 方法的极程序中 Dyn **非实例位**的转数落 t145 运行期陷阱（解释器按承载可转，与 t154 同策略的整面回落）；数值系同质数组字面量 `[3, 4.5]` 的整数位统一定宽为小数（既有 S60/S94 小数位进 integer 槽陷阱面，非本任务面，`integer i = a[0]` 与 `integer i = a[0].toNumber()` 双端行为一致）——该定宽残余已于 S106 t159 按元素保型落 kind 7 消除；ctest -C Release 全量 129/129 **✅ t155** |
| S103 | 动态承载值（Dyn）上的 number 专属方法——`abs()` / `integerPart()` / `decimalPart()` / `isInteger()` / `isDecimal()` / `isNaN()` / `isInfinity()` / `isFinite()` / `isPositive()` / `isNegative()` 按运行期 tag 分派：新增 collie_rt 接口 `collie_rt_dyn_num_guard(tag, name)`（只放行 tag 0/1，复刻解释器进 number 段前的 `is_number()` 判定），并把 t154 的 static `collie_rt_dyn_str_method_trap` 泛化为 `collie_rt_dyn_method_trap(tag, name, accepted)`（接受态关键词参数化，串/数两路共用同一套 `Undefined method '<name>' on object` 与 `Method '<name>()' is only supported on <accepted>, got <kind>` 消息，零语义复制）；codegen 一触点——`visitMethodCall` 在 t145 `dyn_obj_guard` **之前**加 Dyn number 方法臂，十方法名以匿名 helper `is_number_method_name` 枚举并与静态数值接收者分支**同源复用**该谓词（防两处面漂移），先调 rt 守卫再**以 Num 接收者直接复用 t67 `gen_number_method`**：Dyn 的 `{tag,bits}` 与 Num 表示完全重合（零转换、零新增降级），整数态/小数态双路与结果类型（Num 或 Bool）全归既有面，消费侧零新增；静态守卫与 t154/t155 同策略（扫 `classes_` 的 `dispatch` 全程序无同名类方法），命中则整面回落 t145 实例位方法动态分派；来源复用全部既有 Dyn 通道；零删除接口，collie_rt 净接口数 +1 | 动态域元素、kind 7 异质元素（含 `true`/`"x"` 位混排）、object 动态槽（含重赋后可见）、动态 tuple 非常量索引与命名 `get`、`+` 拼接来源的数位、类数组字段元素（方法内 `this.vals[i]`）、闭包体内捕获值、Dyn 再索引出的内层数位、循环内非常量下标元素九路双端一致；整数位走 BigInt 等价精确路径（`integerPart`/`decimalPart`/`isInteger` 恒真恒零）、小数位走 double 路径，NaN 与 `+Infinity` 承载位覆盖 `isNaN`/`isFinite`/`isInfinity`/`isInteger`/`isPositive`/`abs`/`integerPart`/`decimalPart`（两端同为 `NaN`/`+Infinity`）；消费面覆盖结果进 `+ 1`/`== 5.5`/`* 2`/插值/`integer` 与 `number` 类型化槽；新差分用例 s106_dyn_number_methods（A–T 共 24 行输出双端逐字节一致）+ s106_dyn_same_name_number_method（同名类 `abs()` 时整面回落：静态 `r.abs()`/Dyn 实例位 `a[0].abs()`/object 槽 `o.abs()` 同为 `100`·`200`，静态 `number`/`integer` 接收者 D/E 零回归）；陷阱矩阵五组双端 rc=1 且消息核心一致（串位/bool 位/tribool 位/数组位报 "Method 'abs()'('isInteger()') is only supported on numbers, got string|bool|tribool|array"、实例位报 "Undefined method 'abs' on object"，diff 门禁要求两端 rc=0 故仅手工探针不注册用例）；ctest -C Release 全量 131/131 **✅ t156** |
| S104 | 动态承载值（Dyn）上的 tribool 三态判定方法——`isTrue()` / `isFalse()` / `isUnset()` 按运行期 tag 分派：新增 collie_rt 接口 `collie_rt_dyn_tri_guard(tag, name)`（**仅放行 tag 6**，复刻解释器进 tribool 方法段前的 `is_tribool()` 判定——bool 位不放行，解释器对 `true.isTrue()` 是运行期报错而非加宽接受），其余位落 t156 泛化出的共享陷阱 `collie_rt_dyn_method_trap(tag, name, "tribool")`（串/数/tribool 三路共用同一消息族，零新增陷阱代码：实例位 `Undefined method '<name>' on object`、其余 `Method '<name>()' is only supported on tribool, got <kind>`）；codegen 一触点——`visitMethodCall` 在 t145 `dyn_obj_guard` 之前加 Dyn tribool 方法臂：tag 6 的 bits 就是静态 tribool 的 i8 三态编码零扩展（t136 装箱即 `zext i8`），故 `trunc i64 to i8` 后**直接复用 t65 的常量 icmp 降级**（把该降级从 `visitMethodCall` 内联体提取为成员 helper `gen_tribool_method(tri, name)`，静态 Tri 接收者与 Dyn 接收者同源，防两表面漂移）；三方法名以匿名 helper `is_tribool_method_name` 枚举并与静态臂共用；结果恒 Bool，进条件/`==`/三元/插值/print/类型化槽全走既有面；静态守卫沿用 t154/t155/t156 策略（扫 `classes_` 的 `dispatch` 全程序无同名类方法），命中守卫的极程序整面回落 t145 实例位方法动态分派；零删除接口，collie_rt 净接口数 +1 | 动态域元素、kind 7 异质元素、object 动态承载槽（含重赋后可见）、动态 tuple 非常量索引与命名 `get`、循环内非常量下标元素、Dyn 再索引出的内层 tribool 位（`a[0][0].isTrue()`）、闭包体内捕获值、类数组字段元素（方法内 `this.flags[i].isTrue()` 含 `store(i, true)` 写入后）八路双端一致；消费面覆盖结果进 `==`、三元条件（`tribool r = a[0].isUnset() ? true : unset`）、插值 `@"{a[0].isTrue()}"` 与 bool×bool 判等；新差分用例 s107_dyn_tribool_methods（A–N 共 16 行输出双端逐字节一致）+ s107_dyn_same_name_tribool_method（同名类 `isTrue()` 时整面回落：静态 `x.isTrue()`、Dyn 实例位 `a[0].isTrue()`、object 槽 `o.isTrue()` 同为 `100`·`200`，静态 tribool 接收者 C 零回归）；陷阱矩阵五组双端 rc=1 且消息核心一致（bool 位/number 位/串位/数组位报 "Method 'isTrue()'('isUnset()') is only supported on tribool, got bool|number|string|array"、实例位报 "Undefined method 'isTrue' on object"，diff 门禁要求两端 rc=0 故仅手工探针不注册用例）；本面无既有拒编锁面可退役；**kind 收敛残余**：bool 字面量与 tribool 混排的同质数组字面量（`[unset, true]`）在 codegen 按 S83 收敛为 tribool kind、解释器逐元素保型，故该程序两端 rc 不一致（解释器运行期报错、codegen 可编）——属既有 S83 数组字面量 kind 收敛面（与 `[3, 4.5]` 整数位定宽同族）而非本任务面，成功探针改用 `tribool` 变量承载以保两端保型一致（~~该残余面~~已于 S106 t159 按元素保型落 kind 7 解锁，见 S106 行）；ctest -C Release 全量 133/133 **✅ t157** |
| S105 | 动态承载值（Dyn）进类名（Obj 非空 cls）落点——声明初始化/形参实参/返回值/变量重赋/类名字段槽/kind 5 实例数组元素槽六处按运行期 tag＋类 id 双守卫拆槽：新增成员 helper `dyn_unbox_to_obj(dyn, cls, line, col)`（**零新增 collie_rt 接口**，复用 S94 的 `collie_rt_trap_dyn_convert(tag, target)` 以声明类名作落点类型名）——`tag == 5` 且对象头类 id ∈ {声明类 ∪ 其后代类} 才 `inttoptr` 还原实例 ptr：id 集合按 `is_subclass_of` 静态收集，单元素走 `icmp eq`、多元素走同一 `switch`（default 落陷阱）；放行后槽静态 cls 记声明类，字段沿继承链前缀偏移解码、方法按对象头 id 动态分派（t61/t86/t103）、子类特有字段经父类槽走 t142 动态字段分派，消费面（`==`/插值/算术/拼接/闭包捕获/三元/循环）零改动；守卫只放行声明类及其后代——祖先实例落进后代槽（downcast）时解释器可正常访问继承成员，codegen 的 t61「静态 cls 无后代即直调单态化实例」优化会错值，保守取陷阱不错编；退役 s97_reject_dyn_to_class_{decl,arg,return} 三枚拒编锁为差分用例；差分用例 s108_dyn_to_class_slots（五类落点 × kind 7 元素/object 动态承载槽/动态域非常量下标/三元臂四路来源 × 字段读写/方法调用/插值/相等/算术消费，含类字段方法内 `this.pet = this.bag[i]` 与 kind 5 数组元素槽写）、s108_dyn_to_class_inherit（后代实例进父类槽、多后代实参 id switch、子类特有字段经父类槽读、跨类同字段引用语义）；三组守卫未命中手工探针（标量位 `cannot assign number to 'Dog' variable`、兄弟类实例位与祖先实例位均 `cannot assign object to 'Dog' variable`——解释器对三者均可运行到成员访问才报错或直存不校验，diff 门禁要求两端 rc=0 故仅手工探针不注册用例）；ctest -C Release 全量 135/135 **✅ t158** |
| S106 | 数组字面量按元素保型（kind 7 承载扩至数值系互混与 bool×tribool）：visitArrayLiteral 收敛判定删除两条整体拍平分支——数值系互混/全 Num（原统一提升 Double 视图）与 bool×tribool 混排（原经 `to_tri` 加宽为 Tri 槽），改为凡 elem 与元素类型互不兼容且两侧均 ∈ `is_dyn_scalar`（∪ Obj，S91 通道不变；S107 t160 起该并集统一为成员判定 `is_dyn_carrier` = `is_dyn_scalar` ∪ {Obj, Arr}）即收敛 `elem=CGType::Dyn` 落运行时 kind 7 双步长 {tag,bits} 槽，存槽按运行期 tag 直存（`rt_arr_set_dyn`）；写槽循环随之删除失效的整槽加宽（`to_double`/`to_tri`），同质字面量（无任何跨 kind 混排）仍走单值槽原路径不变。kind 7 元素读出恒 Dyn，故整数位 `integer i = [3, 4.5][0]`、`a[0].integerPart()`、`a[1].isDecimal()`、bool 位 `b[1].isTrue()` 全按运行期 tag 判定——解释器对数组逐元素保型，此前 codegen 定宽/加宽静态视图会答出解释器运行期报错的结果（差分面从"错值"消除）。新增 Dyn 接收者 `.toString()` 臂（复用 `collie_rt_num_to_str` 按 tag 分派：0/1 数值格式化、2/6 bool/tribool 词、3 串、4/7 递归 `arr_to_str`、5 "<object>"）；类定义同名 `toString` 的撞名守卫命中时**不再整面回落**，改运行期 tag 拆分——新增成员 helper `dyn_obj_clsid_or_sentinel(tag, ptr)`（仅 tag==5 加载对象头类 id，其余位取哨兵 `~0` 落 switch default），default 臂在 Dyn 接收者下由 `collie_rt_num_to_str` 兜底产出（此前为 "<object>" 常量），实例位仍进用户方法、标量位仍进内建转串，两面同时保住。rt 面 `collie_rt_arr_eq` 改为逐元素归一化 {tag,bits} 后交 `collie_rt_dyn_eq` 单点判定（kind 7 取双步长槽、其余 kind 的 tag 恒 = kind），取消"一侧 kind 7 即恒不等"短路——保型后同质数组（如 `[10, 20]` 的 kind 0）与保型数组结构相等时必须按元素值判定（实证：`s43_numarr` 的 `b == [10, 20]` 静默 false）；collie_rt 净接口数不变（`collie_rt_dyn_eq` 加前向声明）；退役拒编锁 s86_reject_tribool_num_write（number 值写 bool×tribool 混排槽经保型后自然解锁，降为差分用例 s109_tribool_num_write） | 新差分用例 7 枚双端逐字节一致：s109_numeric_kind_literal（Int×Double 混排的元素保型消费：类型化槽窄化/`isInteger`/`integerPart`/`toNumber`/算术/比较/插值/整槽写/异质含 str+bool+tribool/下标值再用作数组下标）、s109_all_num_literal（全 Num 字面量 + 循环遍历 + object 承接）、s109_tribool_bool_mix（`[unset, true]` 的 bool/tribool 变量承接与三态互写）、s109_tribool_num_write（原 S83 拒编锁面）、s109_arr_eq_cross_kind（kind 7 × 静态 kind、嵌套数组、bool×tribool、串位共 15 组深比较）、s109_dyn_tostring_same_name_method（类定义 `toString` 时数值位转串 + 实例位走用户方法 + 混合数组）、s109_dyn_array_slots（保型数组进函数动态域形参/返回值/类字段后的逐元素保型）；陷阱面双端消息一致仅手工探针（`integer i = a[1]` 小数位进 integer 槽同为 "cannot assign decimal value to 'integer' variable"、bool 位 `isTrue()` 同为 "Method 'isTrue()' is only supported on tribool, got bool"，diff 门禁要求两端 rc=0 故不注册用例）；ctest -C Release 全量 141/141（差分 121 + 拒编 14 + 单元 4 + CLI 2） **✅ t159** |
| S107 | 数组位（tag 4）纳入 kind 7 动态承载——新增成员判定 `is_dyn_carrier(t)` = `is_dyn_scalar(t)` ∪ {Obj, Arr}（与 `is_dyn_scalar` 并列声明），把此前分散各处的 `(is_dyn_scalar(x) \|\| x == CGType::Obj)`、`!is_dyn_scalar(x) && x != Obj && x != Arr` 手写并集收束为单点，四落点接上：① visitArrayLiteral 跨 kind 收敛判定（`[d, [1, 2]]`、`[1, [2, 3], "x", true]` → `elem=Dyn`）；② visitIndexAssign 的 elem==Dyn 整槽写（写入值为数组 → 常量 tag 4 + `elem_to_bits` PtrToInt）；③ visitIndexAssign 的 Dyn 接收者内层写（值侧 `to_dyn` 打框）；④ visitVarDecl KW_OBJECT 非 Obj 分支与 visitAssign `var->type==Dyn` 分支（object 动态槽以数组初始化/重赋）。bits 即内层数组 ptr 位模式，与 t85 kind 4 单数组表示完全重合（**零新增 collie_rt 接口/函数**——tag 4 的消费早在 S99/S100 就落在 `collie_rt_dyn_len`/`dyn_index`/`dyn_set`/`arr_to_str` case 4/7/`dyn_eq` 4/7 递归臂上，本面只让这些既有通道可达）；`is_dyn_scalar` 本体不变，tuple 物化处（t140）仍按它判定，故「dynamic tuple with array/object/nested-tuple elements」与 `~~s90_reject_dynamic_tuple_field~~`（已于 S110 t163 解锁退役）拒编面零回归；Dyn 数组位进静态同质落点（`dst[0] = src[0]`，dst 为整数数组）仍落 "array element type mismatch in index assignment" 保守拒编（s97_reject_dyn_to_homog_array_slot 不动）；经 Dyn 数组位写异类值（`object o = [1, 2]; o[-1] = "s"`）落既有 CG7 运行期陷阱（与数组位无关的通用失配面，双端消息核心一致，差分门禁要求两端 rc=0 故仅手工探针、不注册用例） | `[d, [1, 2]]` 整体打印/`len`/`.length`/逐元素与嵌套索引读/实例位字段读与方法调用/插值/深比较（含实例位恒不等、同字面量 `a == a` 亦 false）、`object o = [1, 2]` 及其 `.length`/`len()`/索引读/负索引/`toString()` 与内建 `toString`/插值、空数组与 `[[1, 2], [3, 4]]` 承载、kind 7 写侧数组覆标量与数组覆数组（`m[2] = [[4], [5]]` 三层读）、跨 kind 深比较（`[1, [2]] == [1, 2]` 逐元素递归判不等、同构 `[1, [2, 3]]` 判等、引用语义别名写后两侧均见）、`[unset, [true, 2]]` tribool×数组混排、含数组位 Dyn 槽的闭包捕获与重赋换形状、while/for 循环与 if 条件消费程序编译执行，输出与解释器一致；新差分用例 s110_obj_nested_array_mix/s110_object_slot_array_init/s110_dyn_array_bit_write/s110_dyn_array_eq_deep/s110_dyn_array_closure_loop；退役 s94_reject_obj_nested_array_mix、s96_reject_object_slot_array_init（两面经本任务真实解锁）；ctest -C Release 全量 144/144（差分 126 + 拒编 12 + 单元 4 + CLI 2） **✅ t160** |
| S108 | object 动态类型形参/返回值改为 CGType::Dyn 双字承载（原为 CGType::Obj 空 cls，t132 限实例值）——`declared_signature_type` 的 KW_OBJECT 臂单点换表示，两触点接上：`coerce_call_arg` 新增 want==Dyn 且 `is_dyn_carrier(a.type)` 的实参打框（标量按 `arr_kind_of`、实例 tag 5、数组位 tag 4，复用 S107 的承载集单点判定）、`visitReturn` 新增 current_ret_type_==Dyn 且 `is_dyn_carrier(v.type)` 的返回值打框；函数体/方法体/构造器体共用同一签名登记入口，故 object 进类方法与构造器签名同面解锁；体内消费全部落 S92–S107 既有运行期 tag 分派臂（打印/toString/插值/算术/比较/长度/下标/串方法/number 方法/tribool 方法/字段读写/方法动态分派），**零新增 collie_rt 接口、零消费侧改动**。实例位经 `dyn_obj_guard` 还原实例 ptr 后并入 t133 空 cls 方法 switch 与 t142 字段类 id 分派，s85_object_param / s92_dynamic_object_field 两面零回归；~~Tup~~/~~Fn~~/Void 无 {tag,bits} 编码，维持 "argument type mismatch" / "return type mismatch" 保守拒编不错编（新增锁面 ~~s111_reject_obj_param_tuple~~，已由 S117 t170 以 tag 8 真实解锁退役；~~s93_reject_function_as_argument~~ 已由 S118 t171 以 tag 9 真实解锁退役） | 退役 s100_reject_obj_param_index、s102_reject_obj_param_length 两枚拒编锁（两面程序实证 colliec 现 rc=0 且双端 stdout 一致，属真实解锁非降格迁就测试，其程序作为新用例第一节保留）；新差分用例 7 枚双端 rc=0 且 stdout 逐字节一致——s111_obj_param_scalar_carry（打印/toString/插值/算术/双向拼接/内建 len 与 toString/串方法 trim+subString/`toNumber()`，四路标量承载含 unset）、s111_obj_param_length（串位与数组位取长、空串、`.length` 进 integer 返回、kind 7 元素经形参取长）、s111_obj_param_index（承载整数作下标、负索引、object 槽/三元臂/object 返回值三路来源作下标、`a[pos + 1]` 下标内含算术）、s111_obj_param_array_carry（数组位长度/索引/负索引/`toString`/写回对调用侧可见/嵌套 `o[0][1]`/数组承载出 object 返回值）、s111_obj_param_instance_carry（方法动态分派三态类、直接字段读写与引用语义、跨类专属字段、返回值进类名槽、实例×标量混排数组逐元素经形参）、s111_obj_param_reassign_capture（形参体内重赋后消费、闭包捕获形参单元重赋前后两读、tribool 位 isTrue/isUnset、number 位 isInteger/toNumber、Dyn 透传第二形参、循环体内元素经形参）、s111_obj_method_signature（object 进构造器形参与类方法形参/返回值、方法返回承载进类名槽、同名 `toString` 撞名守卫下实例位走用户方法与标量位走内建转串）；语义层两端同拒面（非 codegen 差分，不注册用例）：object 形参与字面量比较 `o == 5`、`o == "b"` 同为 "Incomparable operand types"；两端同运行期陷阱面（diff 门禁要求两端 rc=0，仅手工探针）：bool 位算术 `true + 1` 同为 "Arithmetic operands must be numbers"、串位 `isInteger()` 同为 "only supported on numbers, got string"、bool 位 `isTrue()` 同为 "only supported on tribool, got bool"；范围外——tuple 值进 object 形参维持拒编（新增锁 ~~s111_reject_obj_param_tuple~~，tag 空间无 tuple 承载位→已由 S117 t170 打 tag 8 真实解锁，锁退役为差分用例 s120_tuple_to_object_slot 第一节）、函数值进 object 形参维持 ~~s93_reject_function_as_argument~~ 锁（已由 S118 t171 以 tag 9 真实解锁，锁退役为差分用例 s121_fn_to_object_signature 第一节）、~~`object` 型类字段维持 "variable type 'object'" 拒编~~（已于 S109 t162 解锁——字段槽同为 {tag,bits} 承载）；ctest -C Release 全量 150/150（差分 133 + 拒编 11 + 单元 4 + CLI 2） **✅ t161** |
| S109 | object 型类字段以 CGType::Dyn 双字承载（原落 `declared_cgtype` default 拒编 "variable type 'object'"）——`register_class_layout` 字段分支新增 KW_OBJECT 前置分支产 `ftype=Dyn`、`fcls` 空（无静态布局可言，与 object 变量 t146/t160、动态签名 t161 同一表示），两触点接上：① `coerce_for_slot` 新增 `slot_type==Dyn && is_dyn_carrier(v.type)` 打框（字段初始化/字段赋值/构造器 `this.f =` 三处汇聚点共用单点）、② `visitNew` malloc 上界把 Dyn 与 Num 同记 16 字节（漏记即 8 字节槽写双字 struct，越界写堆）；字段读出恒 Dyn，消费全部落 S92–S108 既有运行期 tag 分派臂（打印/插值/toString/算术/长度/下标/串方法/number 方法/tribool 方法/类型化槽拆槽/字段读写/方法动态分派），实例位经 `dyn_obj_guard` 并回 t133 方法 switch 与 t142 字段类 id 分派，**零新增 collie_rt 接口、零消费侧改动**；多类树字段读/写的 `{i64,i64}` struct PHI 与空 cls 全体定义者分派同面可达；~~Tup~~/~~Fn~~ 无 {tag,bits} 编码，维持 "implicit conversion for variable" 保守拒编不错编（新增锁 ~~s112_reject_object_field_tuple~~，已由 S117 t170 以 tag 8 真实解锁退役；函数位进 object 型类字段已由 S118 t171 以 tag 9 真实解锁，见 s121_fn_to_object_signature 的 Box.payload 节） | 新差分用例 6 枚双端 rc=0 且 stdout 逐字节一致——s112_object_field_scalar_init（int/decimal/bool/string 位初值与重赋、进 integer/decimal/bool 类型化槽、串位 `.length`、字段读出进 object 变量）、s112_object_field_array_bit（同质/kind 7/嵌套数组位、`.length` 与 `len`、负索引、别名写对字段侧可见、换形状、两个 object 变量绑定同字段的深比较）、s112_object_field_instance_bit（实例位方法调用与字段读写、`object` 返回值进类名变量的向下转型解码、字段初值直接是 `new`、换形状回标量）、s112_object_field_inherit_tree（继承树同名 object 字段读写、upcast 接收者、空 cls 的 object 接收者分派、`object` 返回值进类名字段槽）、s112_object_field_reassign_capture（构造器 object 形参落字段、闭包捕获字段读出的 object 变量、while/for 内换形状、if 以 `toString()` 判定）、s112_object_field_methods（串位 trim/trimLeft/subString/索引、`toNumber` 两形式、number 位 abs/isInteger/integerPart/isDecimal、tribool 位 isTrue/isFalse、同名 `toString` 撞名守卫下实例位走用户方法与标量位走内建转串）；两端同拒（语义层 "Incomparable operand types"，非 codegen 差分面）：字段位与字面量比较 `b.payload == 20`、`b.payload == unset`；范围外——tuple 值进 object 字段维持拒编（新增锁 ~~s112_reject_object_field_tuple~~→已由 S117 t170 以 tag 8 真实解锁，锁退役为差分用例 s120_tuple_object_field 第一节）、函数值进 object 字段维持 ~~s93 函数值面拒编~~（已由 S118 t171 以 tag 9 真实解锁，函数位进 object 型类字段见差分用例 s121_fn_to_object_signature）、object 字段值进静态同质数组元素槽维持 s97 锁、静态同质数组别名写异类值仍落既有 CG7 运行期陷阱（与字段面无关，仅手工探针）；ctest -C Release 全量 157/157（差分 139 + 拒编 12 + 单元 4 + CLI 2） **✅ t162** |
| S110 | 动态 tuple（CGType::TupDyn）命名字段直接访问 `t.name` 解锁（原 `visitProperty` 非 length 分支落 "named field access on dynamic tuple" 拒编，t140 范围外）——形状运行期才定故无静态偏移承载，改与 `get(key)` **同源单点**：常量键名交 `rt_tuple_key_dyn` 按非空名 strcmp 扫描 values(kind7 {tag,bits}) 取回 {tag,bits} 拼 `CGType::Dyn`（**零新增 collie_rt 接口**），其后消费全部落 S92–S109 既有运行期 tag 分派臂（打印/toString/插值/算术/比较/长度/下标/串方法/number 方法/tribool 方法/类型化槽拆槽/object 槽与类名字段落点）；未命中落 rt 既有陷阱 `Undefined tuple field '<name>'`，与解释器 `visitProperty` 元组分支同消息（位置 tuple 名字为空串、标识符永不匹配，两端一致）；`length` 仍先于同名字段返回元素数（对齐解释器分支顺序，s113 用例钉住）；顺带修正 **rt 侧一处既有静默错值**：`collie_rt_append_tagged`（tuple 转串单元素格式化 helper）原只覆盖 tag 0/1/2/6 并把其余全当字符串，含数组位/实例位的动态 tuple 整体打印把指针位模式当串输出（`(arr: , obj: , ...)` 空串/乱码）——改为 case 3 串、case 4/7 递归 `collie_rt_arr_to_str`、case 5 `<object>`，与 `collie_rt_dyn_to_str`/`arr_to_str` 的 tag 分派同源；`is_dyn_scalar` 物化承载集**不变**（静态 Arr/Obj/嵌套 Tup 元素仍拒编，~~新锁 s113_reject_dyn_tuple_static_array_elem~~ 其中 Arr/Obj 两面已于 S111 t164 换 `is_dyn_carrier` 真实解锁、该锁退役，嵌套 Tup/函数值位由 ~~s114_reject_dyn_tuple_nested_elem~~/~~s114_reject_dyn_tuple_function_elem~~ 接锁（前者已由 S117 t170 以 tag 8 真实解锁退役，后者已由 S118 t171 以 tag 9 真实解锁退役，函数元素进动态 tuple 见差分用例 s121_fn_tuple_element）），经 kind 7 数组读出（恒 Dyn）携带的数组位/实例位本就可进动态 tuple，本任务只是让它可读可打印正确 | 退役 s90_reject_dynamic_tuple_field 拒编锁（其程序 `print(r.name)` 实证 colliec 现 rc=0 且双端 stdout 一致，属真实解锁非降格迁就测试，程序作为 s113_dyn_tuple_named_field 第一节保留）；新差分用例 6 枚双端 rc=0 且 stdout 逐字节一致——s113_dyn_tuple_named_field（返回值来源的 `.name`/`.age`/`.length`/`get` 同值/整体打印/`toString`/插值/双向拼接/算术/进 integer 槽）、s113_dyn_tuple_field_kinds（int/decimal/string/bool/tribool 五位字段读出后进算术/比较/串方法/`trim`/`.length`/`isDecimal`/`isInteger`/`isUnset`/`toNumber` 与六类类型化槽 integer/decimal/string/bool/tribool/number）、s113_dyn_tuple_field_array_bit（kind 7 数组读出的数组位/实例位进 tuple 后整体打印/`toString`/插值/内层索引与负索引/`.length`/深比较/进 object 槽再索引）、s113_dyn_tuple_field_param_return（Tuple 形参体内取字段并作 integer/string 返回、体内重赋换形状后取字段、`==?` 三支合流、tuple 字面量实参直接取字段）、s113_dyn_tuple_field_loop_consume（for 循环内逐轮物化取字段、`.name` 与 `get("name")` 恒等、同名字段 `len` 与 `.length` 属性并存、进 object 槽、拼接/比较/嵌套函数实参）、s113_dyn_tuple_field_class_capture（字段结果进类 integer 字段与 object 字段、类方法内插值消费、闭包捕获字段读出值、while 内换形状消费）；两端同运行期陷阱面（diff 门禁要求两端 rc=0，仅手工探针）——`r.z` 未命中字段两端同为 "Undefined tuple field 'z'"（解释器 rc=1 带行列前缀、产物 rc=1 无位置前缀，消息核心一致）；两端同拒面（非 codegen 差分）——`t.a = 1` 属性赋值同为语义层 "Cannot assign property on type 'KW_TUPLE'"；范围外——~~静态 Arr/Obj/嵌套 Tup 元素进动态 tuple 维持物化拒编（新锁 s113_reject_dyn_tuple_static_array_elem，解锁候选路径是把物化承载集从 `is_dyn_scalar` 换 `is_dyn_carrier`（须先双端实证嵌套 Tup 位仍无编码），属 t164 候选）~~（Arr/Obj 元素面已于 S111 t164 以 `is_dyn_carrier` 承载集真实解锁、s113 锁退役；嵌套 Tup 与函数值元素位经双端实证确认仍无 tag 编码，由 ~~s114_reject_dyn_tuple_nested_elem~~/~~s114_reject_dyn_tuple_function_elem~~ 接锁（前者已由 S117 t170 以 tag 8 真实解锁退役，后者已由 S118 t171 以 tag 9 真实解锁退役））、Dyn 数组位进静态同质数组元素槽维持 s97 锁、tuple 进 object 形参/字段维持 s111/s112 锁；ctest -C Release 全量 163/163（差分 145 + 拒编 12 + 单元 4 + CLI 2） **✅ t163** |
| S111 | 动态 tuple（CGType::TupDyn）物化承载集从 `is_dyn_scalar` 换 `is_dyn_carrier`——静态类型即数组/类实例的值可进动态 tuple（`(a: [1, 2], obj: new Point())`），并把**同形状但叶位类型不可静态合流**的三元/`==?` 分支从拒编改为回退动态合流：`materialize_tuple_dynamic` 逐元素改经 `to_dyn` 单点打框（数组位 tag 4 存内层数组 ptr、实例位 tag 5 存实例 ptr、标量位按 `arr_kind_of`，与 t146/t160/t162 的 object 承载族同一表示），守卫 `!is_dyn_carrier` 才拒编（消息改 "dynamic tuple with nested-tuple or function elements"，嵌套 Tup/函数值位无 tag 编码维持拒编不错编）；合流判定 `tuple_arms_same_shape` 更名 `tuple_arms_static_mergeable` 并加叶位可合流校验（规则与 `merge_tuple_arms` 阶段一严格一致：同型、数值互混、Tri×Bool、同 Arr 异 elem 降动态域哨兵、同 Obj 异 cls 需最近公共祖先），不满足者不再落 "yield tuples with incompatible elements" 拒编而是逐支物化后 PHI 合流为 TupDyn（解释器这些分支本就择一返回整值 tuple，静态展开无处承载异型叶位；跨树两实例叶位——无最近公共祖先的 `new Dog()`/`new Cat()` 两支——同经此回退动态合流，方法按对象头类 id 分派）；**零新增 collie_rt 接口**——数组位/实例位的消费全部落 S92–S110 既有运行期 tag 分派臂（打印/`toString`/插值/`.length`/内建 `len`/索引读写与负索引/内层嵌套三层索引/串方法/number 方法/tribool 方法/`toNumber`/object 形参与 object 类字段落点/类名槽 downcast/方法按对象头类 id 动态分派），实例位 `<object>` 转串与判等恒不等由 t144（`arr_eq` 的 tag 5 恒不等）与 t163（`append_tagged` 的 case 4/5/7 分派）既有；判等语义双端实证一致——数组位经 `collie_rt_dyn_eq` tag 4/7 递归 `arr_eq` 深比较（同构判等、异 kind 判不等），实例位 tag 5 落 default 恒 false（解释器 `values_equal` 无 Instance 分支，同一实例 `b == b` 亦 false），tuple 整体判等经 `rt_tuple_eq`（len + 名字表 + values 交 `arr_eq`）归一到同一规则 | 退役 s113_reject_dyn_tuple_static_array_elem 拒编锁（其程序 `(a: [1, 2], b: 3)` 实证 colliec 现 rc=0 且双端 stdout 逐字节一致，属真实解锁非降格迁就测试，程序作 s114_dyn_tuple_static_array_elem 第一节保留）；新差分用例 6 枚双端 rc=0 且 stdout 逐字节一致——s114_dyn_tuple_static_array_elem（数组位打印/`.length`/`len`/负索引/插值/`toString`/引用语义别名写回对 tuple 侧可见/kind 7 异质数组位/空数组位/嵌套数组位三层索引/CJK 串数组/深比较）、s114_dyn_tuple_instance_elem（实例位方法按类 id 动态分派含继承树覆写与 upcast、字段读写、进类名槽 downcast、进 object 槽、数组含实例位、判等恒不等含同一实例、`<object>` 三形式）、s114_dyn_tuple_element_carry_out（数组位/实例位进 object 形参与 object 类字段、Tuple 形参往返携带数组位、number/串方法、数组位判等与含实例位的 tuple 整体判不等）、s114_dyn_tuple_merge_loop（三元/`==?`/while 换形状/for 下标消费/位置索引 `t[0]`·`t[-1]`/闭包捕获含数组位与实例位的动态 tuple/if 与三元条件/循环内数组位算术）、s114_dyn_tuple_incompatible_leaf（同名单字段六支叶位横跨 integer/string/bool/实例/数组/tribool 的 `==?` 合流、三元异型叶位、同为数组异 elem 仍走静态合流、合流结果进 object 承载后分派、动态合流后判等）、s114_dyn_tuple_mixed_kind_elems（同 tuple 标量位+数组位+实例位混排下十类消费面、写回与换承载、以数组位长度作 `==?` 分支条件）；新拒编锁 ~~s114_reject_dyn_tuple_nested_elem~~（嵌套 tuple 元素；已由 S117 t170 以 tag 8 真实解锁，锁退役为差分用例 s120_dyn_tuple_nested_elem 第一节）/s114_reject_dyn_tuple_function_elem（函数值元素，与 s93 函数值面同族）；两端同拒/同陷阱面（非 codegen 差分，仅手工探针）——`t.get(0)` 两端同为 "get() key must be a string"、`t.a = 1` 两端同为语义层 "Cannot assign property on type 'KW_TUPLE'"、`object == 字面量` 两端同为语义层 "Incomparable operand types"（关系比较 `>` 可用）；ctest -C Release 全量 170/170（差分 151 + 拒编 13 + 单元 4 + CLI 2） **✅ t164** |
| S112 | 动态承载值（Dyn）的**数组位**进静态 `array` 落点解锁（原五处汇聚点各按 `v.type != CGType::Arr` 拒编："initializing 'array' variable with non-array value"/"assigning non-array value to 'x'"/"argument type mismatch"/"return type mismatch"/"implicit conversion for variable"，t147 范围外）——解释器 `coerce_to_declared` 的 `KW_ARRAY` 分支只要求 `is_array()`（p6/p9/p10 实证 integer 位报 "cannot assign number to 'array' variable"、实例位报 object、串位报 string），元素 kind 随数组对象运行期自带，故新增成员 helper `dyn_to_arr_slot(dyn)`：运行期守卫 `tag ∈ {4, 7}`（同质嵌套数组位 t85/t160、异质数组位 t139）即 `inttoptr` 还原 opaque ptr，其余位落既有 rt `collie_rt_trap_dyn_convert(tag, "array")`（消息核心与解释器逐字一致，**零新增 collie_rt 接口**）；五落点一次接全——`visitVarDecl` Arr 槽初始化、`visitAssign` Arr 分支、`coerce_call_arg`（array 形参）、`visitReturn`（array 返回值）、`coerce_for_slot`（array 类字段：`visitNew` 初始化与 `visitPropertyAssign` 单树/多树两路汇聚，`allow_dyn_unbox` 守卫使 tuple 元素槽仍按 t147 保守拒编）；**落点 elem 一律降 `Num` 动态域哨兵**（t70 机制复用）：数组位来源的运行期 kind 静态不可知（tag 4 的 bits 可指向 kind 0–7 任一数组），故声明槽记 Num、变量重赋按 t106 异型互赋同规则降级并沿用其循环回边/全局跨函数快照两面守卫，形参/字段槽本就恒 Num，读出即 Dyn 后消费全部落 S92–S111 既有运行期 tag 分派臂（打印/`toString`/插值/`.length`/内建 `len`/索引读写与负索引/嵌套三层索引/串方法/number 方法/tribool 方法/`toNumber`/深比较/实例位方法按对象头类 id 动态分派/再进动态 tuple 物化） | 新差分用例 6 枚双端 rc=0 且 stdout 逐字节一致——s115_array_slot_decl（数组位进声明槽后打印/`.length`/`len`/索引与负索引/`toString`/插值/与数组字面量深比较/写回引用语义对 tuple 侧可见/object 承载值转 array 落点/空数组位与单元素位/函数返回值直接落槽）、s115_array_slot_kinds（kind 7 异质位落槽后逐元素算术/串方法/`.length`、嵌套数组位三层索引、CJK 串数组、decimal×tribool×bool 位 `isDecimal`/`isUnset`、kind 7 含数组位混排、实例位数组落槽后方法按对象头类 id 分派含继承树覆写与专属字段读）、s115_array_slot_reassign（静态 elem 变量被数组位重赋并降级、二次换 kind 7 异质数组位、赋值表达式值语义、if 两支均落动态承载、循环内逐轮重赋消费）、s115_array_slot_signature（数组位作 array 形参实参、array 返回值取回、`sum(twice(k.a))` 链式、类方法 `load(v array)`/`grab() array`、object 承载值穿 array 形参再回 object）、s115_array_slot_field_capture（类字段初值即数组位、属性赋值面、字段读进出 array 形参、继承树父类字段初始化与子类方法重赋、闭包重赋外层动态域 array 变量、字段→object→array 三方别名）、s115_array_slot_domain（落槽后动态域消费：元素算术/关系比较/就地写回/`==?` 以长度为分支/串拼接与插值/while 累加/for 消费/再进动态 tuple 物化/嵌套函数实参往返/object 承载再索引与写回/空数组与单元素数组判等）；新拒编锁 s115_reject_dyn_array_slot_downgrade_in_loop（循环体内重赋令静态 elem 降级动态域——解释器 rc=0，codegen 沿用 t106 循环回边守卫保守拒编不错编）；两端同运行期陷阱面（diff 门禁要求两端 rc=0，仅手工探针）——非数组位落 array 槽：integer 位 "cannot assign number to 'array' variable"、实例位 object、串位 string（解释器带行列前缀、产物无位置前缀，消息核心一致）；两端同错面（非本面）——落槽数组写异类值仍落既有 CG7 同质数组陷阱（`array x = t.a; x[0] = "str"` 产物 rc=1 而解释器 rc=0，与 `array x = [1,2]; x[0] = "str"` 同一策略，非本任务引入）；`array == 动态 tuple 字段/object 承载值` 属语义层两端同拒 "Incomparable operand types"（非 codegen 差分）；ctest -C Release 全量 177/177（差分 157 + 拒编 14 + 单元 4 + CLI 2），既有 s97/s109/s43 系数组面全绿，实证动态域与判等路径零回归 **✅ t165** |
| S113 | 三目 / `==?` 臂含动态承载值（Dyn）的合流 + 动态承载数组位写静态嵌套数组（kind 4）元素槽——`array z = flag ? t.a : [5, 6];`、`takes(flag ? t.a : q)`、`g[0] = t.a`（`g` 为 `[[…]]` 嵌套数组）双端 rc=0 且 stdout 逐字节一致；合流单点新 helper ~~`arms_dyn_mergeable`（任一臂 Dyn 且全臂落 `is_dyn_carrier` 承载族）~~（该判定已由 S115 t168 换成一枚静态合流 helper `merge_static_arm_types` + 准入 `arms_all_dyn_carrier` 的「静态合不上即按承载族回退」形态，覆盖面更广）逐臂 `to_dyn` 打框后 PHI 合流为 Dyn，`gen_ternary`/`gen_multi_match` 两触点同源，消费全走 S92–S112 既有运行期 tag 分派臂（打印/`toString`/插值/长度/索引/串·number·tribool 方法/类型化槽拆槽/object 槽与类名槽 downcast/进 array 落点）；元素槽写复用 `dyn_to_arr_slot` 的 `tag ∈ {4, 7}` 守卫后 `inttoptr`→`ptrToInt` 交既有 `rt_arr_set_`，非数组位落同一 rt 陷阱（解释器异质数组可直存任意值，属残余陷阱面），**零新增 rt 接口**；~~Tup/Fn 臂维持拒编（新锁 s116_reject_dyn_arm_tuple/s116_reject_match_arm_tuple）~~（Tup 臂已由 S117 t170 以 tag 8 真实解锁，两锁退役为差分用例 s120_tuple_arms_merge 第一、二节；~~Fn 臂维持拒编~~（函数位臂已由 S118 t171 以 tag 9 真实解锁，同族函数位锁面退役为差分用例 s121_fn_arms_merge 第一、二节）），~~静态数组值写动态域（elem=Num 哨兵）元素槽维持既有保守拒编（新锁 s116_reject_arr_elem_to_dyn_domain_slot）~~（该面已由 S114 t167 真实解锁、锁退役，见下行）；新差分用例 6 枚 s116_dyn_arm_merge_basic/s116_dyn_arm_merge_kinds/s116_dyn_arm_merge_obj/s116_match_arm_merge/s116_elem_slot_dyn_write/s116_elem_slot_roundtrip，ctest -C Release 全量 186/186（差分 163 + 拒编 17 + 单元 4 + CLI 2） **✅ t166** |
| S114 | 静态数组位/实例位写动态域（`elem == Num` 哨兵）数组元素槽——`this.items[0] = v`（`v array`，解释器 `[[1, 2]]`）、形参体内 `a[0] = [9, 9]`、无初始化声明 `array slot; slot = [1, "x"]; slot[0] = [7, 8];`、`this.pens[i] = new Dog()` 双端 rc=0 且 stdout 逐字节一致；原 t166 撞面先接锁（s116_reject_arr_elem_to_dyn_domain_slot）本批真实解锁并退役。`visitIndexAssign` 的 `object.elem == CGType::Num` 分支加 `v.type == Obj \|\| Arr` 一臂：值经既有 `to_dyn` 打 tag 5/4（bits 即实例/内层数组 ptr 位模式，与 kind 5/4 单值槽和 kind 7 双步长槽同源）后交**既有** `collie_rt_arr_set_num` 按数组运行期 kind 对齐（tag==kind 直存 / kind 7 落 {tag,bits} 双步长槽 / int→dec 提升 / 失配落 CG7 陷阱），与 t151 的 Dyn 值同路故**零新增 collie_rt 接口、零消费侧改动**；动态域读（t70/t88/t151）本就按运行期 kind 产出 Dyn，写回的数组位/实例位读回即落 S92–S113 全部 tag 分派臂（打印/`toString`/插值/`len` 与 `.length`/内层索引与负索引/三层嵌套/串与 number 方法/`toNumber`/成员访问/方法按对象头类 id 动态分派含继承覆写与 upcast/深比较/实例位判等恒不等）；写侧来源一次扫全动态域槽的三类持有者（类字段 `public array`、`value array` 形参体内、无初始化声明的局部与全局）；**运行期 kind 为数值系同质数组时写异类位仍落 CG7 陷阱**（解释器异质可容）——数组运行期 kind 加宽会让已按旧 elem 生成读代码的持有者静默错值，故本面只在 kind 对齐时放行、其余维持陷阱并新锁 s117_reject_arr_elem_to_homog_domain（静态同质 `elem == Int` 槽写数组值，两端 rc 不一致登记不注册）；tuple 值/函数值写动态域元素槽维持拒编（kind 0–7 无二者承载位，新锁 ~~s117_reject_tuple_elem_dyn_domain~~/s117_reject_function_elem_dyn_domain，同 s111/s112/s93 族；前者已由 S117 t170 以 tag 8 真实解锁，锁退役为差分用例 s120_tuple_elem_dyn_domain_write 第一节，而 tuple 值写**静态同质**数组元素槽仍拒编（新锁 s120_reject_tuple_elem_homog_array））；新差分用例 6 枚 s117_dyn_domain_arr_elem_write/s117_dyn_domain_obj_elem_write/s117_dyn_domain_elem_carry_out/s117_dyn_domain_elem_control_flow/s117_dyn_domain_elem_mixed_consume/s117_dyn_domain_elem_alias，ctest -C Release 全量 194/194（差分 169 + 拒编 19 + 单元 4 + CLI 2） **✅ t167** |
| S115 | 三目 / `==?` 臂**静态合流失败**时按承载族回退合流为 Dyn——跨树两实例臂 `object mixed = flag ? d : c;`（`Dog`×`Cat` 无公共祖先，解释器 rc=0 且按真实类分派方法、读各自字段）、实例位×标量/串/数组/tribool 臂 `flag ? d : 5`、`flag ? [1, 2] : d`、异型标量臂 `flag ? 1 : "s"`、`==?` 同形三支含默认支，全部双端 rc=0 且 stdout 逐字节一致；原 t166 只处理「任一臂已是 Dyn」的混排，本批把回退面推广到**一切静态合不上的承载组合**。改法是把两触点内联的合流循环抽成单点成员 helper `merge_static_arm_types(values, result_type, result_elem, result_cls)`（返回 `ArmMerge{Ok, NoCommonAncestor, Incompatible}`，规则原样：同型直用含 Arr elem 降 `Num` 域哨兵 / Obj 取最近公共祖先、数值互混、Tri×Bool 加宽）+ 一枚准入 `arms_all_dyn_carrier(values)`（全臂落 `is_dyn_carrier`）——判 Ok 用静态结果（**同 cls、有 NCA、数值/Tri×Bool、Arr elem 精度全部不变**），判假且全臂承载即 `result_type = Dyn` 走既有 `to_dyn` 打框 PHI（`{i64,i64}` 第一类 struct），判假且有非承载臂按失败种类原样落旧文案（`"… yield instances of different classes"` / `"… have incompatible types"`，~~Tup/Fn~~/Void 臂零漂移——Tup 臂自 S117 t170、Fn 臂自 S118 t171 起即落动态合流，不再走旧文案）；合流值即 Dyn，消费全落 S92–S114 既有 tag 分派臂（方法按对象头类 id 动态分派、各自字段读写、`toString`/插值/长度/索引/串与 number 与 tribool 方法/六类类型化槽拆槽/object·array·类名落点守卫），**零新增 rt 接口、零消费侧改动**；`arms_dyn_mergeable` 由本判定取代（其「任一臂 Dyn」条件是「静态合流失败」的真子集，全臂 Dyn 时静态合流本就产 Dyn）；合流值进类名槽的 downcast（`Dog dd = flag ? d : c;` 命中 `Cat`）解释器可直存并按真实类分派而产物落 t158 类 id 守卫陷阱，属登记不注册面；tuple 位×实例位、~~函数位×实例位~~臂维持拒编（新锁 ~~s118_reject_tern_arm_tuple_obj~~/~~s118_reject_match_arm_tuple_obj~~/~~s118_reject_tern_arm_function_obj~~/~~s118_reject_match_arm_function_obj~~，同 s116 族；前两锁已由 S117 t170 以 tag 8 真实解锁，退役为差分用例 s120_tuple_arms_merge 第三、四节；函数位两锁已由 S118 t171 以 tag 9 真实解锁，退役为差分用例 s121_fn_arms_merge 第一、二节）；探针另撞出既有活跃差分「`==?` 目标为动态承载值」（`cmp ==? 5: …`，`gen_match_eq` 只认静态可定两侧）新锁 s118_reject_match_target_dyn 接住并列下一批（该锁已由 S116 t169 真实解锁退役，其程序作 s119_match_dyn_scalar_bits 第一节保留）；新差分用例 6 枚 s118_merge_cross_tree_obj/s118_merge_obj_scalar_arr/s118_match_cross_tree/s118_merge_precision_kept/s118_merge_landing_slots/s118_merge_nested_consume，ctest -C Release 全量 205/205（差分 175 + 拒编 24 + 单元 4 + CLI 2），改动前先跑 194/194 确认零回归 **✅ t168** |
| S116 | `==?` / `switch` 的**目标或候选**为动态承载值（Dyn）时按运行期 tag 判等——`object cmp = flag ? 5 : 6;` 后 `cmp ==? 5: "hit", "miss"`、动态域元素位 `object e = src[0]; e ==? 1: …`、静态目标×Dyn 候选 `k ==? five` / `g ==? a2`（原 **Arr × 非 Arr 恒 false** 的静默不命中面）、两侧皆 Dyn、实例位与 tuple 位参与比较恒 false，以及 `switch (cmp) { 5 {…} default {…} }` 同路径，双端 rc=0 且 stdout 逐字节一致。改法是 `gen_match_eq` 首臂加「任一侧 Dyn 且另一侧非 Tup」准入 `dyn_match_side_ok`（Dyn/Int/Double/Num/Str/Bool/Tri/Arr/Obj；~~Fn~~/Void 返回 false 维持原拒编文案——S118 t171 起 `dyn_match_side_ok` 收 Fn、入口放宽为「任一侧 Dyn 或 Fn」，静态函数位×静态标量位一并盒化）后两侧统一 `to_dyn` 盒化，下沉**既有** `collie_rt_dyn_eq`（与 t149 `visitBinary ==` 同源，复刻解释器 `values_equal`：Tri×Bool 加宽三态、数值跨 tag 走 double 视图、串按内容、数组位交 `rt_arr_eq` 深比较含 kind 7 内的实例位、tag 失配与实例位恒 false），**零新增 collie_rt 接口、零新增降级、零消费侧改动**；`==?`（级联比较块链）与 `switch` 两调用点同源解锁，候选惰性求值与首命中顺序不变；静态×静态各臂一字未动（Str/Tri/Bool/数值/Tup/Arr/Obj 路径零漂移），Dyn×Tri 原落 "'==?' comparison of tribool with this value type" 亦改走运行期 tag 判定；Tup 侧保留 t78 的「Tup × 非 Tup 恒 false」折叠（kind 0–7 无 tuple 承载位，Dyn 永不持 tuple，与解释器 kind 必不等一致）（该排除已由 S117 t170 撤销：tag 8 承载 tuple 位后 `gen_match_eq` 放开 Tup 侧改走运行期判定，见差分用例 s120_dyn_tuple_compare）；退役锁 s118_reject_match_target_dyn（实证 colliec 现 rc=0 且双端 stdout 逐字节一致，程序作 s119_match_dyn_scalar_bits 第一节保留）；新差分用例 6 枚 s119_match_dyn_scalar_bits/s119_match_dyn_container_bits/s119_match_dyn_both_sides/s119_match_dyn_switch/s119_match_dyn_signature_field/s119_match_dyn_control_flow（覆盖标量位与跨 tag 数值、数组位深比较与实例位/tuple 位恒 false、两侧皆 Dyn 与 for/while 逐轮、switch 多值分支与 tuple 分支值、穿 object 形参/返回值/类字段/方法体/闭包捕获、结果进 if/三目臂/异质数组字面量）；~~新拒编锁 s119_reject_match_dyn_fn_candidate~~（Dyn 目标×函数候选，~~Fn 无 {tag,bits} 编码~~（S118 t171 起函数位以 tag 9 承载，该锁真实解锁退役为差分用例 s121_fn_match_candidate 第一节），同 s93/s118 族）；探针另撞出 `object big = 100000000000000000000;` 落 "integer literal out of i64 range (gap CG1)"（解释器 rc=0，属 CG1 远局面且消息不含 "not yet supported"，登记不注册）；ctest -C Release 全量 211/211（差分 181 + 拒编 24 + 单元 4 + CLI 2），改动前先跑 205/205 仅待退役锁失败，实证本面是叠加而非改写 **✅ t169** |
| S117 | tuple 位纳入动态承载（**运行期 tag 8**，bits 即 t140 `collie_rt_tuple` 堆对象 ptr `{len, names kind 3, values kind 7 {tag,bits}}`，零新布局）——`is_dyn_carrier` 从 `is_dyn_scalar` ∪ {Obj, Arr} 扩为 ∪ {Obj, Arr, **Tup, TupDyn**} 单点判定，`to_dyn` 加 Tup（先 `materialize_tuple_dynamic` 物化再打 tag 8）与 TupDyn（透传打 tag 8）两臂，于是 object 形参/返回值、object 变量槽、object 型类字段、三目与 `==?` 的承载族回退合流、kind 7/动态域元素槽写、异质数组字面量元素**各落点自动接上、无需逐点改守卫**；rt 侧只加路由：`dyn_len` case 8（`.length` 取元素数，内建 `len()` mode 1 仍落原消息陷阱——解释器 `call_builtin_len` 不收 tuple）、`dyn_index` tag 8 交 `collie_rt_tuple_get_dyn` 位置索引含负索引、`dyn_set` tag 8 落 "Tuples are immutable, cannot assign to index"、`dyn_to_str`/`append_tagged`/`rt_print_num` 递归转串、`dyn_eq` tag 8 交 `collie_rt_tuple_eq`（嵌套 tag 8 递归、tag 5 恒不等，对齐 `values_equal` 的 Tuple 分支）、`trap_index_kind`/`trap_dyn_convert` 两张 kind 名表补 `case 8: "tuple"`；`visitProperty` 的 Dyn 接收者改为按运行期 tag 两路分派后合流为 Dyn（tag 8 → **既有** `rt_tuple_key_dyn` 常量键扫描，解释器 `visitProperty` 元组分支的 `length` 优先天然由 t152 臂保证；tag 5 → `dyn_obj_guard` 后并入既有字段类 id 分派再 `to_dyn`；其余位落既有 undefined property 陷阱），原内联的实例字段读抽成成员 helper `read_obj_field(..., trap_when_undefined)`，新参数只对 Dyn 的 tag==5 臂传 true（无定义者时陷阱 + `unreachable` 并返 `{nullptr, Dyn}` 占位，调用侧据此不连边、合流 PHI 少一路），静态接收者维持原拒编文案；`get(key)` 面新增**本批唯一一枚** rt 接口 `collie_rt_dyn_tuple_get(tag, bits, key, otag, obits)`（tag 8 交 `tuple_key_dyn`，其余位交既有 `collie_rt_dyn_method_trap(tag, "get", "tuples")`——解释器 get 段在实例分支之后故实例位报 "Undefined method 'get' on object"），`visitMethodCall` 在 toString 臂前加「Dyn × `get` × 单串实参 × 无任何类定义同名方法」臂；`gen_match_eq` 去掉 S116 的「另一侧非 Tup」限制（tag 8 后 Dyn 真能持 tuple，保留 t78 的「Tup × 非 Tup 恒 false」折叠即静默不命中，探针 `t ==? o`/`o ==? t` 实证），`visitBinary ==` 的 Tup×Dyn **刻意不加**运行期路径（语义层对 Tuple×object 比较报 "Incomparable operand types"，两端同不可达） | 退役 8 枚拒编锁——s111_reject_obj_param_tuple、s112_reject_object_field_tuple、s114_reject_dyn_tuple_nested_elem、s116_reject_dyn_arm_tuple、s116_reject_match_arm_tuple、s117_reject_tuple_elem_dyn_domain、s118_reject_tern_arm_tuple_obj、s118_reject_match_arm_tuple_obj（加用例前全量 211/211 仅这 8 枚失败，逐条重跑实证 `I=0 C=0 SAME`，程序作对应新用例第一节保留）；新差分用例 7 枚双端 rc=0 且 stdout 逐字节一致——s120_tuple_to_object_slot（tuple 作 object 实参、进 object 变量槽后 `.length`/`[0]+1`/负索引、经 object 返回值、命名字段与 `get`、同槽重赋换形状、三目臂、字面量直接作实参）、s120_tuple_object_field（`public object payload = (1, 2)` 默认值、方法内 `this.payload = v` 与 `grab()` 读出后 `.a`/`.length`/`[1]`/`get`、字段换形状后读新字段）、s120_dyn_tuple_nested_elem（`mk(flag)` 返回含嵌套 tuple 元素的动态 tuple 后整体打印 / `t.n` / 进 object 槽再 `.n.q` / 内层 `.length` / 数组字段取长与索引 / `==?` 以嵌套 tuple 为候选 / 换承载后读深层字段）、s120_tuple_arms_merge（三目 `t.a : (k: 1)`、`==?` 臂含 tuple 与数组、tuple 臂×实例臂两支与三支、合流值 `.x` 成员读、`==?` 以 tuple 为候选命中、`flag ? t : d` 后 `[0]`/`.length`）、s120_tuple_elem_dyn_domain_write（形参体内 `a[0] = (x: 1)`、tuple 位写动态域槽后整体打印/`.b`/`[1]`/`.length`/`get`、写嵌套 tuple 位后 `.w.q`、含 tuple 元素的异质数组字面量、for 循环逐轮读）、s120_dyn_tuple_member_access（穿 object 形参的打印/`.length`/`[0]+1`/负索引、命名字段常量键、`get("k")`、object 返回值来源、类字段初值与方法写入、闭包捕获、同名字段 `length` 与 `.length` 属性并存（属性取元素数、`get` 取字段）、嵌套 tuple 的 `.v.deep`）、s120_dyn_tuple_compare（Dyn 目标×Tup 候选、Tup 目标×Dyn 候选即原静默不命中面、Dyn×Dyn `==`/`!=`、嵌套 tuple 深比较、`switch` 的 tuple 候选命中、tuple 位与整数位混排三支）；新拒编锁 2 枚 s120_reject_tuple_elem_homog_array（tuple 位写静态同质数组 kind 0 元素槽，解释器 rc=0 打印 `[(a: 1, b: 2), 2]`，8 字节单值槽无法承载 {tag,bits}，残余活跃差分）/s120_reject_dyn_tuple_prop_assign（`o.a = 9`：解释器运行期 "Cannot assign property on tuple"、codegen 编译期拒编，两端皆失败非差分面）；两端同运行期陷阱面（仅手工探针）——`len(o)`/`number q = o`/`arr[o]`/`o + 1`/`o.nope` 五面消息核心一致；两端同语义层拒编面——`Tuple × integer` 三目、Tuple×object 的 `==`；范围外——~~函数位（Fn）仍无 {tag,bits} 编码（s114_reject_dyn_tuple_function_elem/s117_reject_function_elem_dyn_domain/s118_reject_tern_arm_function_obj/s118_reject_match_arm_function_obj/s119_reject_match_dyn_fn_candidate 五锁维持）~~（五锁已由 S118 t171 以 tag 9 真实解锁退役，函数位进各落点见 S118 与差分用例 s121_*）、类定义同名 `get` 方法或字段撞名时静态守卫让 Dyn 接收者整面回落 tag 5 分派或陷阱、tuple 静态展开路径（t95 同形状合流、t78 Tup×Tup `gen_tuple_eq`）一字未动；ctest -C Release 全量 212/212（差分 188 + 拒编 18 + 单元 4 + CLI 2） **✅ t170** |
| S118 | 函数位纳入动态承载（**运行期 tag 9**，bits 即函数 ptr 的位模式 `ptrToInt(llvm::Function*)`，与 t143 静态 `CGType::Fn` 承载同源，零新布局）——`is_dyn_carrier` 从 `is_dyn_scalar` ∪ {Obj, Arr, Tup, TupDyn} 扩为再 ∪ {**Fn**} 单点判定，`to_dyn` 加 Fn 臂，于是 object 形参/返回值、object 变量槽、object 型类字段、三目与 `==?` 的承载族回退合流、kind 7/动态域元素槽写、异质数组字面量元素**各落点自动接上、无需逐点改守卫**；rt 侧**零新增接口**，只补 tag 9 路由：`dyn_kind_name`/`trap_index_kind`/`trap_dyn_convert`/`dyn_len` 四张 kind 名表加 `case 9: "function"`（方法与转换陷阱消息与解释器 `kind_name()` 逐字一致），`num_to_str`/`print_num` 两张转串表与 `arr_to_str` 的 kind 7 内层 switch、`append_tagged` 两处单元素格式化加 `case 9` 打 `<function>`——**这六处是必须显式加臂的静默错值面**（`default:` 把 bits 当 `char*` 输出，函数 ptr 打出来是空串/乱码）；判等**不加臂**：tag 9 落 `collie_rt_dyn_eq` 既有 `default: return 0` 即恒不等，天然复刻解释器 `values_equal` 无 Function 分支（`add == add` 为 false）；`visitArrayLiteral` 首元素 kind 规则加 Tup/TupDyn/Fn → `elem = Dyn` 哨兵（函数无数组 kind，跨 kind 混排收敛 kind 7）；`visitIndexAssign` 静态位分支加 Fn（交 `to_dyn` 打 tag 9 后与 t151 的 Dyn 值同路）；`visitVarDecl` 的 KW_OBJECT 臂**删掉 t143 的「函数初值建 Fn ptr 槽」静态近似**（`object o = add;` 直接落 `{tag,bits}` 动态承载槽，其后异型重赋即原样解锁——同 t161/t162「换表示即解锁落点」形态）；`materialize_tuple_dynamic` 守卫文案随承载集更名 "dynamic tuple with unsupported element type"；`gen_match_eq` 入口从「任一侧 Dyn」扩为「任一侧 Dyn 或 Fn」且 `dyn_match_side_ok` 收 Fn（静态函数位×静态标量位原落 "'==?' comparison of these value types"，盒化后交同一路 `rt_dyn_eq`；Void 维持原拒编文案）；`materialize_dyn_scalars` 加 Fn 臂（单点走 `to_dyn`）+ 两处 `is_scalar`（非常量索引/动态取键）收 Fn，异质**静态** tuple 的函数位元素经 `t[i]`/`t.get("x")` 读出即落 Dyn tag 9 | 退役 8 枚拒编锁——s93_reject_function_as_argument、s93_reject_function_return_value、s93_reject_function_in_array、s114_reject_dyn_tuple_function_elem、s117_reject_function_elem_dyn_domain、s118_reject_tern_arm_function_obj、s118_reject_match_arm_function_obj、s119_reject_match_dyn_fn_candidate（加用例前全量 204/212 仅这 8 枚失败，逐条重跑实证 `I=0 C=0 SAME` 才删除，程序作对应新用例第一节保留）；新差分用例 7 枚双端 rc=0 且 stdout 逐字节一致——s121_fn_to_object_signature（函数位作 object 实参与返回值、进 object 变量槽后 `toString`/拼接/`==`与`!=`恒 false、经 object×object 形参判等、`Box` 的 `public object payload` 与 `put`/`grab`/`show`、闭包捕获、for 逐轮、if 条件、槽与字段换型重赋）、s121_fn_array_bits（`[add]` 整体打印、函数位与数值/串/实例位/数组位混排字面量、`mix[0] = add` 后整体打印与逐位读出、`bag.length`、含函数位数组深比较恒 false、数值系同质数组换 kind 7 接收者后循环写入、嵌套数组位读出）、s121_fn_tuple_element（`mk(flag)` 动态 tuple 含函数元素的整体打印/`.f`/`get("f")`/进 object 槽、静态 tuple `(fn: helper, n: 1)`、函数位与数组/数值/串混排 tuple 转串、两支同 Fn 走原静态合流、异质静态 tuple 的 `h[i]`/`h.get("x")`/`h[-1]` 与判等）、s121_fn_dyn_domain_elem（形参体内 `a[0] = hi` 后取长/打印/逐位读出/经 object 槽转串、多位写入并回读、经数组形参层层传递后再进 object 形参）、s121_fn_arms_merge（三目 Fn×实例 / Fn×整数 / Fn×串 臂合流为 Dyn 后打印与转串、`==?` 臂 Fn×整数、合流值写 kind 7 元素槽、两臂同 Fn 走原同型合流、嵌套三目）、s121_fn_match_candidate（`==?` 的 Dyn 目标×函数候选、静态整数目标×返回 integer 的函数候选、函数位作 `switch` 分支值与动态目标 `switch`、动态域元素槽读出的函数位作 `==?` 目标、`gi == gi`/`gi != gi` 表达式）、s121_fn_consume_control_flow（类方法签名与 `public object` 字段承载函数位、循环内函数位累积进 kind 7 元素槽、object 槽循环换型、提前 return 路径、递归透传、`switch` 分支体内写入读出、多层实参穿透、数组字段承载函数位）；新拒编锁 2 枚 s121_reject_object_uninit_fn_slot（无初始化 `object o;` 首赋函数值仍落 t143 ptr 近似，其后异型重赋拒编——同 s96 族「槽布局 ptr 不可中段改 `{i64,i64}`」）/s121_reject_tuple_dyn_index_container_elem（异质 tuple 含 Arr/Obj 元素的非常量索引与动态取键，本批只让函数位入 `is_scalar`；该锁已由 S119 t172 把容器位一并纳入而真实解锁退役，程序作差分用例 s122_tuple_dyn_index_arr_elem 第一节）；两端同运行期陷阱面（diff 门禁要求两端 rc=0，仅手工探针）——函数位的 `.length`/索引/算术/`abs`/`trim`/`toNumber`/关系比较/进 `number`·`string` 类型化槽/`len()`/`.name` 成员读共十一面消息核心两端一致（其中八面含 "function"，算术与关系比较两端同报通用操作数消息）；两端同语义层拒编面（非 codegen 缺口）——`object` 位与函数名的 `==`（"Incomparable operand types"）、Fn×Arr / Fn×Tuple / Fn×unset 三目臂、非 `object` 目标×函数候选的 `==?`（函数符号的类型即返回类型，故 `k ==? gi` 仅在返回类型可比时才到得了 codegen）；范围外——**间接调用无需支持**（`o(3)` 经变量在语义层两端同拦 "Undefined function 'o'"，tag 9 无运行期调用面）、数值系同质数组写函数位落 CG7 运行期陷阱（s117_reject_arr_elem_to_homog_domain 面，要的是元素槽 `{tag,bits}` 化或数组表示整体改造）、函数位进非 object 静态落点（`number q = add;` 落 t158 转换陷阱）、BigInt 运行时化（CG1）未动；ctest -C Release 全量 213/213（差分 195 + 拒编 12 + 单元 4 + CLI 2） **✅ t171** |
| S119 | 异质 tuple 的**容器位元素**纳入非常量索引/动态键 get() 的 tags+bits 物化路径（Arr tag 4 / Obj tag 5 / Tup 与 TupDyn tag 8 / Fn tag 9；Void 仍无编码）——`visitIndex` 与 `visitMethodCall` 的两处同名 `is_scalar` 判定从「标量 + Dyn + Fn」扩为与 `is_dyn_carrier` 同集，`materialize_dyn_scalars` 加 Fn/Tup/TupDyn 臂单点走 `to_dyn`（静态展开 tuple 先经 t140 物化）、Arr/Obj 沿用既有 `arr_kind_of` 4/5 + `elem_to_bits` ptrToInt（与 `to_dyn` 的 tag 4/5 编码逐位相同，零漂移），读出即 `CGType::Dyn` 后走 S92–S118 全部运行期 tag 分派消费（打印/转串/`.length`/嵌套索引/深比较/`==?`/`switch`/实例字段与方法/拆槽进类型化槽/落点）；同质**跨树实例位**元组另抽 `tuple_obj_merged_cls` helper 做回退判定——静态最近公共祖先不可定时（原 `tuple_dynamic_result` 落 "non-constant tuple access on incompatible instance elements"）令 `single_array` 判假改走本 Dyn 路径，读出 tag 5 后字段/方法按对象头类 id 运行期分派（对齐解释器不校验类）；`tuple_dynamic_result` 内保留同 helper 的保守拒编作防御；零新增 rt 接口、零新增降级 | 双端探针 p1~p6/p8~p9/p11/p14/p15/p19 解释器 rc=0 且改后 colliec rc=0、stdout 逐字节一致（数组位/实例位/嵌套 tuple 位/六类承载位混排/动态 tuple 来源/函数位嵌套/落静态 `array` 槽含引用共享）；p13/p12 为两端同失败面（非差分）；**加用例前全量 213/213 仅待退役锁 s121_reject_tuple_dyn_index_container_elem 失败**，重跑该程序实证 `I=0 C=0 SAME`（真实解锁）；新差分用例 6 枚——s122_tuple_dyn_index_arr_elem（数组位索引/`get`/负索引/进 object 槽取长与索引/嵌套数组位 `v[i][0][1]`/深比较/`==?`/`switch`/for 逐轮/`.toString()`）、s122_tuple_dyn_index_object_elem（实例位字段读与方法调用、**同质跨树实例位回退**、同类多位仍走静态展开零漂移、判等恒不等、写 kind 7 元素槽后再读字段、`==?` 实例候选）、s122_tuple_dyn_index_nested_tuple（嵌套 tuple 位 `.length`/位置索引/`.n`/`get`、**同质嵌套 tuple** 原拒编面、深比较、`==?` 与 `switch` 的 tuple 候选）、s122_tuple_dyn_index_mixed_carriers（串/数组/实例/函数/数值/bool/tribool 七位混排逐轮打印、串位 `.trim()`、数组位 `.length`、实例位字段、函数位转串、tribool 位 `==? unset`、拼接、写 kind 7 槽、`switch` bool 位）、s122_tuple_dyn_index_signature_field（读出值穿 object 形参/返回值、类字段 put/grab/show、object 槽换型、for/while 逐轮、`==?` 数组候选、深比较 `==`/`!=`）、s122_tuple_dyn_index_nested_forms（`Tuple` 返回值来源的动态 tuple 位、嵌套 tuple 含函数位、各内层 kind 不同的数组位、拆槽进 `integer` 槽、落静态 `array` 槽与数组形参含引用共享）；新拒编锁 s122_reject_tuple_void_element（tuple 的 none 元素无 {tag,bits} 编码，构造点即拒编）；三门禁全绿：`cmd //c t102_build.cmd` → `ctest -N -C Release` 219 → `ctest -C Release` 219/219（差分 201 + 拒编 12 + 单元 4 + CLI 2） |
| 后续 | BigInt 运行时化 | 逐任务扩展 |

不在第一期范围：异常语义（tuple 已于 S21 t68 以静态展开解锁、相等比较已于 S28 t75 解锁、同质 tuple 非常量索引已于 S36 t83 解锁、同质命名 tuple get() 动态键已于 S37 t84 解锁（同质 Arr/Obj 元素已于 S63 t114 解锁——结果 CGType 静态可定复用单数组物化），异质 tuple 非常量索引/动态键（含 Bool/Str）~~仍拒编~~（已于 S85 t138 以 CGType::Dyn 动态标量解锁显示面消费——print/toString/插值；算术/比较/进数组~~仍拒编~~（算术已于 S95 t148 以 `dyn_num_operand` tag<2 数值守卫 + '+' 的 tag==3 运行期分派解锁，比较已于 S96 t149 以 rt `collie_rt_dyn_eq`/`collie_rt_dyn_rel` 运行期 tag 分派解锁（判等复刻 `values_equal` 接受表、关系比较数值/同串字典序两路），~~Dyn 作下标仍拒编（s98_reject_dyn_index 锁面）~~已于 S97 t150 解锁（整数位 bits 直接透传、小数位整数值走既有 floor/clamp、其余位落运行期同消息陷阱 `collie_rt_trap_index_kind`），进同质数组仍拒编，~~object 动态形参（CGType::Obj 空 cls）作下标仍拒编（s100_reject_obj_param_index 锁面）~~已于 S108 t161 解锁——object 形参/返回值改 {tag,bits} 动态承载后下标走 S97 既有 dyn_index 面），~~进类型化槽/进函数签名~~（标量落点 integer/decimal/number/bool/tribool/string/byte/word/char 与标量形参/返回已于 S94 t147 以 `dyn_unbox_to` 运行期 tag 守卫解锁，进 Obj 类名槽已于 S105 t158 以运行期 tag+类 id 守卫解锁（含变量重赋/类名字段槽/kind 5 实例数组元素槽）；~~Dyn 数组位进 array 落点维持拒编~~（Dyn 数组位进 `array` 落点已于 S112 t165 解锁——`dyn_to_arr_slot` 的 `tag ∈ {4,7}` 守卫 + 落点 elem 降 Num 动态域，覆盖变量声明/重赋/`array` 形参与返回值/`array` 类字段五汇聚点；**元素**槽即同质数组元素槽（s97_reject_dyn_to_homog_array_slot）/tuple 元素槽仍保守拒编不错编；~~嵌套数组（kind 4）元素槽写 Dyn 数组位仍拒编~~（已由 S113 t166 解锁——复用 `dyn_to_arr_slot` 守卫后交既有 `rt_arr_set_` 写内层数组 ptr，非数组位落同一陷阱）；三目/`==?` 臂 Dyn 与非 Dyn 承载臂合流亦由 S113 t166 解锁（~~`arms_dyn_mergeable`~~ 该判定已由 S115 t168 换成 `merge_static_arm_types` + `arms_all_dyn_carrier`，凡静态合不上且全臂落承载族一律回退 Dyn，含跨树两实例臂）））；两层数值系嵌套数组已于 S38 t85 解锁，≥3 层与内层 bool/str 已于 S42 t89 解锁——~~内层元素经动态域索引读出 kind ≥ 2 落 CG9 陷阱不错值~~（已于 S98 t151 以运行期 kind 分派产出 Dyn 消除）；类继承向上转型已于 S39 t86 解锁——限覆写同签名，downcast/无关类仍拒编（父类静态类型调子类特有方法已于 t102 解锁、同树 downcast 成员访问已于 t103 解锁）；object 动态类型变量声明已于 S69 t120 解锁——~~限 Obj 初始值~~（Obj 初始值静态类名近似：槽记初始值类名，字段按该类前缀偏移解码、方法按对象头 id 动态分派、同树重赋走 visitAssign 既有守卫 t86/t103），无初始化 object 声明已于 S70 t121 解锁——首赋吸收 RHS 类名（visitAssign Obj 分支识别 cls 空占位，吸收后与类名声明同规则，非 Obj 值首赋仍拒编）；非 Obj 初始值/~~object 函数形参与返回值仍拒编~~（object 动态类型作形参/返回值已于 S81 t132 解锁，~~限实例值对齐 codegen object 变量语义~~（S108 t161 起签名即 {tag,bits} 动态承载，标量/数组/实例位全可经形参与返回值传递，仅 ~~Tuple~~/~~Fn~~/Void 维持拒编（Tuple 位已由 S117 t170 以 tag 8、函数位由 S118 t171 以 tag 9 承载解锁，见 s120_tuple_to_object_slot / s121_fn_to_object_signature）））；byte/word 类字段已于 S40 t87 解锁，byte/word 返回类型已于 S50 t97 解锁——返回值经 check_bit_range 校验，byte/word 函数参数已于 S51 t98 解锁——形参绑定置 bit_max 复用赋值点陷阱（调用点无需陷阱：重载解析保证实参恒为已校验 byte/word 值），byte/word 类方法/构造器参数与返回已于 S52 t99 解锁——方法单签名按名解析，形参绑定点插 check_bit_range 范围陷阱（实参可为整数字面量，覆盖方法/构造器/base 全路径），返回走 t97 陷阱（方法调用结果参与 ==/!= 比较、word→byte 返回属解释器语义边界非 codegen 拒编面）；bool/string/嵌套数组动态域透传已于 S41 t88 解锁——print/len/== 全 kind 安全，~~动态域索引读出 bool/str/嵌套元素运行期陷阱不错值，缺口 CG9~~（已于 S98 t151 消除，缺口 CG9 退役）；嵌套函数声明已于 S44 t91 解锁——限函数体内嵌套（受限雷姆达提升），~~嵌套体引用外层局部（捕获）仍拒编~~（闭包按引用捕获外层形参/局部已于 S88 t141 解锁——每个真实存储槽 cell_id 自增取号、嵌套原型尾随 ptr 捕获参数转发宿主槽指针、体内读写直达共享单元对齐解释器闭包语义；~~函数名作值仍拒编~~（函数名作值静态直接消费面已于 S90 t143 解锁——新增 CGType::Fn 以 ptr 承载函数指针、print/toString/插值/拼接固定 `<function>`、==/!= 常量折叠、object 声明变量承载与同型重赋、静态 tuple 元素/三目臂/嵌套函数名作值；~~作实参传参/返回值/进数组/进类字段仍需运行期一等公民表示维持拒编不错编~~（该四面已于 S118 t171 以 tag 9 动态承载解锁，`object` 声明变量槽的 t143「Fn ptr 静态槽」近似同批退役、改落 {tag,bits}；间接调用经变量语义层已拦非差分面）；类方法体内嵌套（单态化副本重分配 cell_id）与 Tuple 捕获（解构无单槽）保守维持标识符不可见拒编，同 t104；类方法体内嵌套已于 t104 解锁；顶层控制流块内函数声明已于 S73 t124 解锁——以顶层语句序号作合成前缀复用 declare_nested_in 递归登记为块作用域嵌套，块外调用两端同为语义层 Undefined 报错）；无初始化变量声明已于 S45 t92 解锁——限四静态类型且~~同块赋值后读，分支/循环块内赋值后读仍拒编~~（S59 t110 定赋区域跟踪：if/else 全路径定赋后读已放行，单支/循环体内赋值后区外读仍拒编）（number/tribool/char/character/byte/word 已于 S49 t96 一并放行，array/类类型已于 S54 t101 放行——array 建 opaque ptr 槽 + elem=Num 动态域哨兵、类类型建 Obj 槽 + cls，visitAssign Arr/Obj 分支补同块 uninit 清除，~~无初始化 Tuple 仍拒编——形状无从推断~~（S61 t112 放行——解构槽组延迟到首赋处按 RHS 形状建；换形状重赋已于 S66 t117 放行——重建槽组 + 条件发射区/全局跨函数双守卫，区内与跨函数仍拒编不错编））；三元/==? 分支不同类实例已于 S46 t93 统一到最近公共祖先——无公共祖先的两类合流~~仍拒编~~（已于 S115 t168 逐臂打 tag 5 合流为 Dyn 解锁）；三元/==? 分支不同 elem 数组已于 S47 t94 统一动态域——数组变量再赋不同 elem 仍拒编；三元/==? 分支 tuple 已于 S48 t95 静态展开合流（限同形状——元素数+名字表递归一致）——~~形状/名字不一致仍拒编~~（形状/名字不一致已于 S87 t140 以 CGType::TupDyn 动态 tuple 物化 + PHI 合流解锁：各支物化为运行时 tuple 对象 {len, names, values(kind7)} 后合流，同形状仍走 t95 静态展开路径不变；tuple 进函数形参/返回值同经 S87 物化解锁；~~含 Arr/Obj/嵌套 Tup 元素的 tuple 物化仍拒编不错编~~（元素静态类型即数组/类实例已于 S111 t164 解锁——物化承载集从 `is_dyn_scalar` 换 `is_dyn_carrier`、逐元素经 `to_dyn` 打 tag 4/5；同形状但叶位类型不可静态合流的三元/`==?` 分支同经此从拒编改为回退动态合流；嵌套 Tup 与函数值元素位仍无 tag 编码，维持物化拒编不错编，锁 ~~s114_reject_dyn_tuple_nested_elem~~/~~s114_reject_dyn_tuple_function_elem~~）→ 其中嵌套 Tup 位已由 S117 t170 以 tag 8 真实解锁（锁退役，程序作差分用例 s120_dyn_tuple_nested_elem 第一节），函数值元素位已由 S118 t171 以 tag 9 真实解锁（锁退役，程序作差分用例 s121_fn_tuple_element）；类实例进数组已于 S53 t100 解锁——限同类（kind 5，复用 CGValue.cls 记元素类名），本地静态读出/字段/方法调用/整槽写同类或子类 upcast 全支持（整槽写同树互赋已于 S67 t118 解锁——visitIndexAssign 镜像 t115 追加 downcast，兄弟类/无关类仍拒编不错编），混合类字面量已于 S68 t119 解锁——NCA 收敛（有公共祖先则元素类收敛到最近公共祖先、逐元素收敛、字段按 NCA 前缀偏移解码、方法按对象头类 id 动态分派），无公共祖先（跨树）~~仍拒编不错编~~（已于 S84 t137 解锁——收敛 cls 空动态 object 元素，方法走 t133 分派、~~字段访问维持拒编~~字段读写已于 S89 t142 以对象头类 id switch 分派解锁——各定义者同名字段声明类型不一致仍保守拒编不错编）；整槽写异类仍拒编，~~动态域（数组过签名/字段/返回值）obj 元素读出落 CG9 陷阱不错值~~（已于 S98 t151 解锁）；实例数组变量同继承树整体互赋（`a = [...]`）已于 S64 t115 解锁——visitAssign Arr 分支镜像标量 Obj 放行 upcast/downcast、var->cls 保持不变，兄弟类/无关类仍拒编不错编；异质基本类型（int×decimal×bool×tribool×string）数组字面量已于 S86 t139 解锁——收敛 CGType::Dyn 运行时 kind 7 双步长 {tag,bits} 槽，本地 print/len/索引读写/== 深比较全承载，~~bool×number 混排维持保守拒编~~（t136 s86_reject_bool_num_array 经此退役），数值系互混（`[3, 4.5]`/全 Num）与 bool×tribool（`[unset, true]`）混排已于 S106 t159 一并归入本路按元素保型（原整体拍平为 Double 视图/Tri 加宽槽，整数位定宽与 bool 位加宽会让 codegen 答出解释器运行期报错的结果）；异质数组含 Obj/Arr/Tup 元素 ~~仍拒编~~（**Obj 已于 S91 t144 纳入 kind 7**：tag 5 存实例 ptr 位模式，`[d, 1, "two", true]` 打印/转串/插值/深比较/整槽写全承载，读出为 Dyn 且其上字段读/写与方法调用已于 S92 t145 以 tag==5 守卫并入 t142/t133 动态分派解锁（标量位成员访问落运行期陷阱；~~Dyn 进 object 槽维持拒编~~已于 S93 t146 解锁——非 Obj 初始值的 object 声明建 kind 7 动态承载槽，消费走既有 Dyn 臂，s94_reject_dyn_elem_to_object_slot 退役）；s89_reject_hetero_obj_array 经此真实解锁退役；~~Arr/Tup 混入仍拒编 "heterogeneous array literal"，实例与嵌套数组异质需动态承载非本期面~~（Arr 已于 S107 t160 纳入 kind 7 tag 4，`[d, [1, 2]]`/`[1, [2, 3], "x", true]` 与 object 槽数组初始化全承载，s94_reject_obj_nested_array_mix/s96_reject_object_slot_array_init 经此退役；~~Tup 混入仍拒编 "heterogeneous array literal"~~（Tup 混入已于 S117 t170 纳入 tag 8 承载，`[inner, 1]` 双端 rc=0，见差分用例 s120_tuple_elem_dyn_domain_write））），~~kind 7 数组经函数签名/字段/返回值动态域透传后索引读落 CG9 陷阱不错值（elem 降 Num、kind 7 大于 1，属动态域边界）~~（已于 S98 t151 经 rt_arr_get_dyn 取回 {tag,bits} 解锁））。
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
| 顶层函数声明 | 两遍处理：第一遍扫顶层建全部原型（递归/前向调用天然可用），第二遍生成函数体；符号名 `collie.<name>` + InternalLinkage（用户标识符无 '.'，不与 main/printf 冲突）；~~同名重载拒编~~（同名函数后定义遮蔽前定义 last-wins 已于 S80 t131 解锁，对齐解释器 env_.define 覆盖语义——被遮蔽前定义跳过体生成；方法重载见 S72 t123 first-wins；顶层控制流块内兄弟同名仍拒编不变）；顶层控制流块内函数声明以语句序号合成前缀走 declare_nested_in 登记为块作用域嵌套（S73 t124）；函数体内嵌套见 S44 t91 |
| 参数/返回类型 | 限 integer/decimal/bool/string；`none` 返回降 void（CGType::Void）；实参/返回值仅 integer→decimal 隐式提升（与解释器 coerce 一致）；number/bool→string 隐式转换已于 S80 t131 解锁（对齐解释器 coerce_to_declared KW_STRING 分支，经新增 coerce_to_string_value 复用 collie_rt_*_to_str；tri/arr/obj/tup 仍拒编）；object 动态类型作形参/返回值已于 S81 t132 解锁（~~限实例值，对齐 codegen object 变量语义~~（S108 t161 起签名以 CGType::Dyn 的 {tag,bits} 承载，标量/数组/实例位皆可传，~~Tuple~~/~~Fn~~/Void 维持拒编（Tuple 位已由 S117 t170 打 tag 8、函数位由 S118 t171 打 tag 9 纳入承载））） |
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
| 非 string 接收者 / 非 Int 索引 | array/tuple 已支持；~~非 Int 索引拒编（Double 索引解释器运行期也报错）~~ number/decimal 整数态索引已于 S71 t122 解锁（index_to_int 归一，非整数态运行期陷阱 "Index must be an integer"）；Bool/Str 下标仍拒编不错编 |

**S10 降级补充（t57 实现）：string 方法**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `s.trim()` / `trimLeft()` / `trimRight()` | `call ptr @collie_rt_str_trim(ptr, i32 mode)`（mode 0=两端/1=左/2=右）：只剥空格与 Tab（对齐解释器 is_blank），返 malloc 新串（CG6 不 free） |
| `s.subString(start[, end])` | `call ptr @collie_rt_str_substring(ptr, i64, i64)`：UTF-8 码点区间 [start,end)，缺省 end 传 -1 运行时取 length，越界 clamp、start>=end 得空串；参数收 Int/Double/Num（~~限 Int，Double/NaN 特例拒编~~ t105 解锁）——Double/Num 对齐解释器：end 在 floor 前判 NaN/精确 -1.0 取 length（floor(-0.9) 为 -1 不特判、clamp 到 0 得空串，顺序敏感），NaN start 归 0，其余 llvm.floor 后 double 域 clamp [0,4e18]（防 fptosi poison，±Infinity/超大值由 rt 垫片按 length 收口）转 i64；零新增 rt 接口 |
| `x.toString()`（任意标量接收者） | 复用 `to_str` 降级（与内建 `toString(x)` 同一路径），结果为 Str |
| `toNumber()` / number/tuple 方法 | toNumber() 已于 S16（t63）解锁；tribool 方法（isTrue/isFalse/isUnset）已于 S18（t65）解锁、其在动态承载值（Dyn）上的按 tag 分派已于 S104（t157）解锁；number 专属方法（abs/integerPart 等）已于 S20（t67）解锁、其在动态承载值（Dyn）上的按 tag 分派已于 S103（t156）解锁；tuple.get("字面量键")已于 S21（t68）静态解析 |

**S12 降级补充（t59 实现）：array 最小闭环**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 数组字面量 `[a, b, c]` | 逐元素求值后同质推断（~~Int/Double 混合整体提升 Double，提升后输出仍与解释器一致；其余混合/嵌套拒编~~；S106 t159 起跨 kind 混排（数值系互混、bool×tribool、含 string/bool/Dyn）一律收敛 elem=Dyn 落 kind 7 双步长槽按元素保型，仅完全同 kind 混排仍走单值槽；嵌套元素混入 Obj 外的拒编不变；空数组 elem 记 Int）→ `call ptr @collie_rt_arr_new(i64 len, i64 kind)` 单块 malloc 数组对象（kind：0=Int/1=Double/2=Bool/3=Str/4=Arr/5=Obj/6=Tri/7=Dyn）→ 逐元素 `elem_to_bits` 转 8 字节位模式后 `collie_rt_arr_set` 写槽（kind 7 走 `rt_arr_set_dyn`） |
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
| 范围外拒编 | ~~无初值字段（解释器落 none 无静态表示）~~（构造器定赋子集已于 S58 t109 解锁，S74 t125 放宽至顶层直线定赋可夹杂 this-free 语句，none 可观察面维持拒编）、tuple 字段、`object` 声明类型、~~方法重载~~（同名不同参重载已于 S72 t123 解锁 first-wins；extends/base/实例作参数返回值已于 S14 t61 解锁；array 字段 S24 t71、实例字段 S25 t72、number 字段 S27 t74 陆续解锁；tribool 字段随 S18 tribool 支持自然放行，t74 实测确认；实例相等比较已于 S35 t82 解锁恒 false、~~实例进数组/元组仍拒编~~（同类实例进数组已于 S53 t100 解锁见 S53，进元组仍拒编）） |

**S14 降级补充（t61 实现）：class 二期——继承/base/实例作函数参数返回值**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class D extends B` 布局 | 字段 = 父链 base-first 合并 + 自身追加（GEP 索引前缀不变）；父类须声明在前，同名字段遮蔽拒编 |
| 方法单态化 | 对每个类 C 沿链每个 (定义类 D, 方法 m) 生成独立函数 `collie.C.D.m`（分派上下文 = C、base 解析上下文 = D，与解释器 `call_class_method(instance, method, defining_class)` 同构）；类内分派表 dispatch 存覆写解析后的映射，体内 `this.m()` 按 C 的分派表解析——模板方法模式（父类方法调子类覆写）与解释器动态分派等价（向上转型拒编保证静态 cls 即动态类） |
| `: base(args)` 构造器委托 | 按定义类的父类解析（current_defining_class_），调父类构造器在当前分派类下的单态化实例；父类无构造器时 0 实参为空操作（对齐解释器 visitBaseCall） |
| `base.m(args)` | 从定义类的父类起静态查首个定义者（绕过子类覆写，C# 语义），调该定义者在当前分派类下的单态化实例 |
| 实例作函数参数/返回值 | 签名处 IDENTIFIER 类名 → ptr + cls；实参/return 处 cls 严格相等否则拒编（语义层同步支持类名签名→object 动态放行，t61） |
| `@override` | 纯语义层校验（t40），codegen 忽略注解位 |
| 范围外拒编（继承相关） | 向上转型（声明/赋值/三元/实参/返回均严格同类）、子类同名字段遮蔽、父类声明晚于子类、~~构造器/方法重载~~（同名不同参重载已于 S72 t123 解锁 first-wins） |

**S15 降级补充（t62 实现）：number tagged 双表示（CG5 收窄）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `number` 值表示 | `{i64 tag, i64 bits}` first-class literal struct：tag 0=整数（bits 即 i64）/1=小数（bits 为 double bitcast 位模式）；变量单 alloca 槽、函数签名/PHI 直用该 struct（LLVM 自动处理 ABI 降级），仅 collie_rt 边界 extractvalue 拆散标量传参 + out 指针写回（规避 MSVC x64 16 字节 struct 传参隐藏指针 ABI 错配） |
| 算术 `+ - * / %` / 一元 `-`（任一侧 Num） | `call void @collie_rt_num_arith(i64 op, i64 atag, i64 abits, i64 btag, i64 bbits, ptr otag, ptr obits)`（op：0=+ 1=- 2=* 3=/ 4=% 5=一元负号）：双整数精确 + - * 与 floor 取模（i64 溢出复用 CG1 陷阱报错退出）、`/` 恒 double、混合走 double、除零 IEEE 754；另一侧 Int/Double 先 to_num 加宽 |
| 比较 `== != < <= > >=`（任一侧 Num） | `call i32 @collie_rt_num_cmp(i64 op, i64 atag, i64 abits, i64 btag, i64 bbits)`（op：0..5 对应 == != < <= > >=）后与 0 做 icmp ne 返 i1：双整数精确、混合 double 视图（5 == 5.0 为 true）、NaN 语义对齐解释器 |
| `print(n)` / `toString(n)` | `collie_rt_print_num(i64 tag, i64 bits)` / `collie_rt_num_to_str(i64, i64)`：整数态按整数打、小数态走 f64 四步格式（与 print_f64 共享，对齐 Value::to_string） |
| integer/decimal → number 加宽 | 声明/赋值/实参/return 四路径 to_num：保持原表示打 tag（对齐解释器 coerce_to_declared 的 KW_NUMBER 分支）；~~反向 number→integer/decimal 窄化拒编（静态无法判定 tag）~~（S60 t111 解锁：integer 窄化插 tag 陷阱、decimal 双态直收） |
| 三元分支混型 | 任一分支 Num → 两分支块尾统一 to_num，merge 处 PHI 用 struct 类型 |
| 范围外拒编 | number 数组元素表示不变（S12 同质 kind 拍板，动态域见 S23）、任意精度自动扩容（BigInt 运行时化留远期，超 i64 整数运算陷阱退出）（toNumber 内建已于 S16 t63、number 类字段已于 S27 t74 解锁） |

**S16 降级补充（t63 实现）：toNumber 内建**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `toNumber(s)`（string）/ `s.toNumber()` | `call void @collie_rt_str_to_num(ptr s, ptr otag, ptr obits)`（出参写回同 num_arith 的 ABI 规避）：复刻解释器 to_number_value 的 string 分支——剥两端空白 → 严格大小写 `Infinity`/`+Infinity`/`-Infinity` → 纯整数串（可带单个 +/- 前缀）精确整数表示 → strtod 等价 std::stod（须整串消费且结果有限，`"1.5f"` 残留/`"infinity"` 宽松拼写均失败）→ 一切失败返 NaN 不报错；超 i64 纯整数串 strtoll ERANGE 走 CG1 陷阱（解释器 BigInt 精确，不静默错编） |
| `toNumber(b)`（bool）/ `b.toNumber()` | 纯 IR：`zext i1 → i64` 后打 tag 0（整数表示 0/1，对齐解释器） |
| `toNumber(x)`（integer/decimal/number） | 纯 IR：复用 to_num 加宽（保持原表示打 tag）/ number 透传 |
| 内建与方法形式 | visitCall（分发插在 len 之后、用户函数查表之前）与 visitMethodCall 共用 to_number_num 降级；结果恒为 Num |
| `toNumber(d)` / `d.toNumber()`（动态承载值 Dyn） | S102（t155）解锁：`call void @collie_rt_dyn_to_number(i64 tag, i64 bits, ptr otag, ptr obits, i64 mode)` 按运行期 tag 分派（0/1 透传、2 转 0/1、3 复用 str_to_num、其余报错），产出 Num 后进既有消费面；方法形式与内建形式仅 mode 不同（实例位的两类消息有别） |
| 范围外拒编 | none/array/tuple/实例参数（解释器此处为运行期报错 "toNumber() cannot convert ..."；~~Dyn 承载值~~ 已于 S102 t155 按运行期 tag 解锁，其数组/实例/tribool 位为同消息运行期陷阱） |

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
| `isTrue()` / `isFalse()` / `isUnset()` | 接收者限 Tri，`icmp eq i8 2/0/1` 出 Bool；动态承载值（Dyn）接收者于 S104（t157）解锁——先 `call void @collie_rt_dyn_tri_guard(i64 tag, ptr name)`（仅 tag 6 放行，其余位含 bool 位复刻解释器 `Method '<name>()' is only supported on tribool, got <kind>` / 实例位 `Undefined method '<name>' on object`），再 `trunc i64 bits to i8` 走同一 icmp（tag 6 的 bits 即 i8 三态编码的零扩展，t136 装箱即 zext），零新增降级 |
| print / `toString` | 双 select 三常量串（is_true ? "true" : (is_false ? "false" : "unset")），零新增 collie_rt 接口 |
| `==?` tribool 目标穷尽省默认 | 默认检查移至 target 求值后（仅 Tri 目标可省），无默认链尾 `unreachable`（i8 值域严格 {0,1,2} + 语义层保证穷尽） |
| 范围外拒编 | ~~tribool 进数组元素~~（已于 S83 t136 解锁，kind 6 三态槽）；tribool 进元组；object 动态路径三态；`if/while/for/do-while` 条件 tribool（语义层已拦） |

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
| Dyn 接收者（{i64,i64}） | S103（t156）解锁：表示与 Num 完全重合，先 `call void @collie_rt_dyn_num_guard(i64 tag, ptr name)` 复刻解释器进 number 段前的 `is_number()` 判定（tag > 1 落 `collie_rt_dyn_method_trap`——实例位 `Undefined method '<name>' on object`、其余 `Method '<name>()' is only supported on numbers, got <kind>`），放行后直接按 Num 接收者走上一行的双路降级，零新增降级；静态守卫「全程序无同名类方法」，命中则整面回落 t145 实例位方法分派 |
| 接口面 | 零新增 collie_rt 接口（仅复用 `collie_rt_trap_int_overflow`）；10 个方法均 0 参（语义层已校验，codegen 防御拒编） |

**S21 降级补充（t68 实现）：tuple 静态展开**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 元组字面量 `(1, name: v)` | 无运行时对象：逐元素求值后连同名字表登记 `tuple_values_` 注册表，表达式值为 CGType::Tup 虚值（llvm value 恒 nullptr，`tup` 存注册表下标）；元素类型/个数/名字编译期全可知（语义层零追踪，codegen 自建） |
| `Tuple t = …` 声明 / 重赋值 | 解构为逐元素独立 alloca 槽（槽名 `t.0`/`t.1`，嵌套元组递归子条目，形状入 `tuple_vars_`，取自初始值；无初始化声明延迟到首赋处建槽，S61 t112）；变量读逐槽 load 重组新虚值；重赋值要求同形状（元素数 + 名字表一致）逐槽写，否则拒编 |
| `t[常量 i]` | 编译期解析成对应元素：`const_int_of` AST 层模式匹配（整数字面量或一元负号包字面量，避开 emit 的溢出检查产非常量指令）；负索引归一化对齐解释器 normalize_index；越界/动态索引拒编 |
| `t.length` / `t.name` / `t.get("键")` | length 常量折叠 `i64 n`（优先于同名字段，对齐解释器分支顺序）；命名字段线性扫名字表（不排除空名）、get 限字符串字面量键（排除空名）——两处匹配规则分别对齐解释器 visitProperty/get；未命中/动态键拒编；动态 tuple（TupDyn，S87）不走本行的静态折叠：length 经 `rt_tuple_len`，`t.name` 与 `t.get(key)` 同交 `rt_tuple_key_dyn` 运行期扫描取 {tag,bits} 拼 Dyn（命名字段访问 S110 t163 解锁，未命中落 rt 陷阱同解释器消息） |
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
| 范围外 | Tup 字段（既有拒编不变）、~~number 字段读出赋窄化静态槽（走既有 Num→静态槽拒编）~~（S60 t111 解锁）、字段上直接一元负号/相等比较（语义层对属性访问的既有限制，经 number 局部变量可用） |

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
| `==?`/switch 数组候选 | gen_match_eq 加 Arr 分支：双 Arr 下沉 rt_arr_eq、~~Arr × 非 Arr 恒 false~~（非 Arr 侧若为动态承载位，恒 false 会让数组位候选静默不命中，已由 S16 t169 改走 `rt_dyn_eq` 深比较）恒 false（语义层通常更早拦截，防御性双保险）；两调用点同时解锁 |
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
| 范围外 | ~~异质 tuple 非常量索引（结果类型静态不可定，拒编 "non-constant index on heterogeneous tuple"，解释器动态可求值）；Num/嵌套(Tup/Arr/Obj)元素同质 tuple（数组槽 elem_to_bits 仅 4 类，拒编 "non-constant tuple index on this element type"）~~（数值系异质与全 Num 同质已于 S56 t107 解锁；同质 Arr/Obj 元素已于 S63 t114 解锁——结果 CGType 静态可定复用单数组物化；~~含 Bool/Str 的异质与同质 Tup 元素维持拒编~~（含 Bool/Str 的异质已于 S85 t138 以 Dyn 标量解锁；同质 Tup 与 Arr/Obj/Fn 容器位元素已于 S119 t172 一并入 tags+bits 物化路径））；空 tuple 非常量索引；`(t[i]==literal)`/`&&` 混逻辑（语义层不追踪 tuple 元素类型，两端一致 "Incomparable operand types"）；tuple 作函数形参/实参（"tuple in function signature"）；tuple `get()` 动态键（已于 S37 t84 解锁）；零新增 collie_rt 接口 |

**S37 降级补充（t84 实现）：非常量 tuple get() 动态键**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t.get(k)`（k 运行期字符串，t 同质命名 tuple） | visitMethodCall Tup+get 分支：键非字符串字面量进动态键路径——四守卫（空 tuple / 元素非 {Int/Double/Bool/Str} / 异质 / 无命名字段分别拒编）后 emit(key) 校验 Str，rt_arr_new 物化 names 数组（kind 3 string，无名元素存空串 ""）+ values 数组（elem kind，逐元素 elem_to_bits），新接口 `collie_rt_tuple_get(names, vals, key)` 按非空名 strcmp 扫描：命中返对应 values 槽 i64 bits（bits_to_elem 还原元素类型），未命中打 "Undefined tuple field '<key>'" + exit(1)（核心消息对齐解释器 RuntimeError，位置前缀缺失同 S36 越界陷阱既定分歧）；空名槽 rt 侧 `name[0] != '\0'` 天然跳过（对齐解释器非空名匹配） |
| 常量字符串键 `t.get("a")`（回归） | 保持编译期解析（t68 路径不变），未命中静态拒编 "undefined tuple field 'X'" 不错编 |
| 悬垂防护 | `const CGTuple t` 按值拷贝——动态键路径 `emit(key)` 可能触发 register_tuple 扩容 tuple_values_ 使引用悬垂，与 S36 visitIndex/gen_tuple_eq 同一防护 |
| 范围外 | ~~异质 tuple 动态键（拒编 "non-constant get() on heterogeneous tuple"）~~~~；Num/嵌套(Tup/Arr/Obj)元素（"non-constant tuple get() on this element type"）~~（数值系异质与全 Num 同质已于 S57 t108 解锁；同质 Arr/Obj 元素已于 S63 t114 解锁；~~含 Bool/Str 的异质与同质 Tup 元素维持拒编~~（含 Bool/Str 的异质已于 S85 t138 以 Dyn 标量解锁；同质 Tup 与 Arr/Obj/Fn 容器位元素已于 S119 t172 一并入 tags+bits 物化路径））；无命名字段 tuple（"non-constant get() on tuple with no named fields"）；空 tuple（"non-constant get() on empty tuple"）；非 string 键（"non-string tuple get() key"，解释器亦运行期报错）；新增 1 个 collie_rt 接口 |

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
| `[x, y, 3]`（x/y 为 number）字面量 | ~~visitArrayLiteral 数值系同质判定扩展：元素 CGType ∈ {Int/Double/Num} 互混时 elem 统一提升 Double（Num 运行期 tag 静态不可判，double 视图承载）；全 Num 字面量循环后兜底 elem=Num→Double；存槽循环 Int/Num 元素 to_double 提升——print 输出与解释器混合表示一致（rt format_f64 整数值省 .0）~~（已于 S106 t159 改为按元素保型：互混/全 Num 一律收敛 elem=Dyn（运行时 kind 7 双步长 {tag,bits} 槽），存槽按运行期 tag 直存，整数位不再定宽为小数；不含跨 kind 混排的同质字面量（如 `[1, 2]` 的 kind 0）仍走单值槽原路径） |
| Num → double 转换 | to_double 加 Num 分支：tag==0 ? SIToFP(bits) : BitCast(bits)，两分支无副作用 select 免分支 |
| `a[i] = n`（n 为 Num，a 槽 Int/Double） | visitIndexAssign 静态数值槽收 Num 值：tag 运行期定，下沉既有 rt_arr_set_num 按槽 kind 对齐——tag==kind 直存、0→1 提升、1→0 失配落 CG7 陷阱（小数态 Num 写 int 槽解释器动态异质可容 vs 产物陷阱退出不错值，陷阱实证面） |
| 范围外 | 超大整数 Num 进数组 double 视图丢精度（Int→Double 混合提升既有同域先例，CG1 缺口范畴）；无初始化变量绑 none（候选 D 后置）；位运算 Num 整数态非差分面（语义层 "Bit operands expected" 两端一致拦截，预检实证排除）；零新增 collie_rt 接口 |

**S44 降级补充（t91 实现）：嵌套函数声明（受限雷姆达提升）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `function outer() { function inner() {...} ... }` | 雷姆达提升为模块级函数：declare_function 加 prefix 缺省参，注册键改编 `outer.inner`（用户标识符无 '.' 天然防冲突），符号名 `collie.outer.inner`；尾部 declare_nested_in 递归下探 Block/If/While/For/DoWhile/Switch 全语句子树，命中 FunctionStmt 建原型并登记 nested_fns_（FunctionStmt* → 改编键） |
| 声明处可见性 | visitFunction 嵌套路径：nested_fns_ 查表命中即在声明处向 scopes_.back() 登记 CGVar{fn_key=改编键}（对齐解释器"执行到声明处 env_.define"——声明前调用不可见、所在块退出即失效）；生成现场 in_function_/current_ret_type_/current_ret_cls_ 保存恢复（嵌套生成完还原外层函数上下文） |
| 嵌套体内可见集 | 链底 = 全局层拷贝 + 外层链上全部 fn_key 绑定拷入（自身递归/前置兄弟嵌套可见，对齐动态作用域"声明先于调用即可见"）；~~变量槽不拷——引用外层局部即标识符不可见拒编（捕获面范围外，实证：解释器 42 vs 拒编 "identifier 'captured'"）~~（外层形参/局部按引用捕获已于 S88 t141 解锁，见下方 S88 补充；类方法体内嵌套捕获与 Tuple 捕获仍拒编不错编） |
| `inner()` 调用解析 | visitCall 三级顺序：内建 → 作用域链函数绑定（lookup_var fn_key 非空 → functions_[fn_key]）→ 顶层 functions_[fname]（嵌套绑定遮蔽顶层同名，对齐解释器 env 由内向外解析） |
| 范围外 | ~~嵌套体引用外层局部（捕获，拒编不错编）~~（外层形参/局部按引用捕获已于 S88 t141 解锁，见下方 S88 补充；类方法体内嵌套捕获与 Tuple 捕获仍拒编）；~~类方法体内嵌套函数（declare-pass 不下探方法体，维持 "nested function declaration" 拒编）~~（t104 解锁，见下方补充）；~~函数名作值/被赋值（"function 'f' used as a value" / "assignment to function"，非一等公民）~~（函数名作值静态消费面已于 S90 t143 解锁——print/toString/插值/拼接/相等/object 变量承载；函数名绑定被赋值（"assignment to function"）与作实参/返回值/进容器仍拒编）；同外层同名嵌套（语义层 "already defined" 双端拦截非差分面）；零新增 collie_rt 接口 |

**S44 降级补充（t104 实现）：类方法体内嵌套函数**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class C { function m() { function inner() {...} ... } }` | register_class_methods 自身方法循环（含构造器）尾部对方法体逐语句 declare_nested_in，改编键前缀 `<定义类>.<方法名>`（如 `Calc.fact.f`，符号 `collie.Calc.fact.f`，同 t91 规则；与顶层同名在语义层即拒绝）；继承副本共享同一 FunctionStmt，仅定义类注册一次 |
| 单态化副本重访 | 同一方法体按分派类多次生成，visitFunction 嵌套路径加重访守卫：`info.fn` 非空（首个副本已生成完整函数体）时仅登记可见性绑定即返——嵌套体是独立函数，行为与分派类无关 |
| this 上下文隔离 | visitFunction 生成现场保存/清空/恢复 current_this_/current_class_name_/current_defining_class_：嵌套体内 this/base 落 "'this' outside class method" 拒编不错编（同 t91 外层局部捕获面；不清空会跨函数引用方法 this 实参生成非法 IR，解释器动态作用域可运行为既定拒编差分） |
| 范围外 | 嵌套体引用 this/字段/方法形参/方法局部（捕获面，拒编不错编，同 t91）；零新增 collie_rt 接口 |

**S88 降级补充（t141 实现）：闭包按引用捕获外层形参/局部**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 捕获单元身份 | CGVar 新增 `cell_id`：visitVarDecl 每个真实存储槽（含 byte/word/uninit/array/class/object/通用路径与形参绑定）建时 `next_cell_id_++` 取号，0=全局/函数绑定/元组解构等非单槽者（不会被按 ptr 捕获） |
| 捕获收集 | build_closure_prototype（宿主 `current_this_==nullptr` 的嵌套函数，体生成前调）：文件局部递归 collect_free_idents_expr/stmt 收体内自由标识符（含更深嵌套体供逐层转发、不下探 ClassStmt），逐个经宿主 scopes_ 解析，仅「有单槽 ∧ 非 GlobalVariable ∧ cell_id≠0 ∧ fn_key 空」登记为 CGFunction::Capture（name+宿主 cell_id+type/elem/cls/bit_max/uninit） |
| 原型重建 | 捕获为空保留第一遍无捕获原型（现有嵌套函数零改动，89 绿色差分不受影响）；非空则 eraseFromParent 重建 llvm 函数为「原参数 LLVM 类型 + 等长尾随 ptr 捕获参数」——调用点均在声明之后词法生成，erase 安全 |
| 捕获绑定 | visitFunction 形参循环改按 nparams 下标 getArg，捕获绑定先入参数层（slot=收到的 ptr 形参、无 alloca、cell_id 透传、type/elem/cls/bit_max/uninit 还原），同名形参随后覆盖即遮蔽 |
| 调用点转发 | visitCall 正常实参后逐捕获 `lookup_var` 比对 cell_id：一致即同一单元（宿主 alloca / 自递归或内层转发所得 ptr）传其槽指针；不一致（该名被内层遮蔽、无从取回登记槽）保守拒编不错编 |
| 按引用语义 | 体内读写经捕获 ptr 直达宿主槽：内层写外层可见、外层写内层迟绑定可见，对齐解释器闭包共享变量单元；uninit 透传防未赋值捕获变量错值 |
| 范围外 | 类方法体内嵌套捕获（单态化副本各帧重分配 cell_id、跨副本共享 ptr 悬垂）与 Tuple 捕获（解构无单槽）维持标识符不可见拒编（同 t104）；~~函数名作值仍拒编~~（作值静态消费面已于 S90 t143 解锁，持函数值的 object 变量经本机制按引用捕获自动覆盖；作实参/返回值/进容器仍拒编）；零新增 collie_rt 接口 |

**S45 降级补充（t92 实现）：无初始化变量声明**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `integer/decimal/bool/string x;` | 槽照常创建（顶层零初始化 GlobalVariable / 函数内 alloca 不预存），CGVar 记 `uninit=true` ~~+ `decl_depth=scopes_.size()`~~（S59 t110 删除）；解释器语义为绑 none，但读被静态拒编故槽初值不可观测 |
| 读 uninit 变量 | visitIdentifier 拒编 "use of uninitialized variable"——语义层 use-before-init 检查流不敏感（分支/循环内赋值后读会放行），解释器该场景运行期输出 none，零初始化槽会错出 0——拒编不错编 |
| `x = v` 后续读 | ~~visitAssign 通用路径 store 后仅当 `scopes_.size() == decl_depth`（同块直线区域）清 uninit 放行；深层块赋值存值但不清标记~~（S59 t110 定赋区域跟踪：赋值无条件清，条件发射区快照隔离 + if/else 交集） |
| 全局无初始化 + 函数体内读 | 函数体链底快照拷贝全局层时 uninit 状态随 CGVar 拷贝：声明后已同块赋值的全局在函数内可读，未赋值的保守拒编 |
| 范围外 | ~~byte/word/number/tribool~~（S49 t96 一并放行）/~~数组/类类型~~（S54 t101 放行）/Tuple 无初始化维持拒编；~~分支/循环块内赋值后读拒编不错编~~（S59 t110：if/else 全路径定赋后读已放行，单支/循环体内赋值后区外读维持拒编）；零新增 collie_rt 接口 |

**S46 降级补充（t93 实现）：三元/==? 分支实例类型统一到最近公共祖先**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `c ? new B() : new A()`（B extends A） | gen_ternary 分支类型统一：Obj cls 不等改求 nearest_common_ancestor（沿 super 链，含自身端点——a 即 b 祖先返 a、反之返 b、否则自 a 父链找首个 b 的祖先），result cls 累计取 NCA；Obj 的 LLVM 表示统一指针，PHI 无关 cls，值本身零转换 |
| `k ==? 1: new B(), new A()` 多支合流 | match 合流同规则扩展到 N+1 支（result_cls 逐支累计 NCA，兄弟类→公共父、孙类/兄弟类→更高祖先） |
| 合流值方法调用/字段读 | t86 机制天然正确：对象头类 id + visitMethodCall 动态分派按运行期真实类解析覆写方法；字段走父类前缀布局，祖先静态类型读偏移一致 |
| 无公共祖先两类合流 | ~~维持拒编 "ternary/'==?' branches yield instances of different classes"（解释器动态类型同签名方法可行，实证：解释器 X vs 拒编——拒编不错编）~~（该面已由 S115 t168 解锁：静态合流失败且全臂落 `is_dyn_carrier` 时逐臂 `to_dyn` 打 tag 5 合流为 Dyn，方法按对象头类 id 运行期分派，见 S115 行） |
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
| 范围外 | ~~tuple 进函数签名~~/进数组维持拒编（形状跨调用点不定，需按形状特化；签名面已于 S87 t140 以 TupDyn 物化 + PHI 解锁，进 object 签名已由 S117 t170 打 tag 8 解锁）；~~异质数组字面量维持拒编~~（已于 S86 t139 以 kind 7 动态标量承载解锁），~~实例进数组维持拒编~~（同类实例进数组已于 S53 t100 解锁）（预检活跃差分面，后置候选）；零新增 collie_rt 接口 |

**S49 降级补充（t96 实现）：无初始化变量声明放行面扩展**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `number n;` | 槽照常创建（Num 承载 struct{i64,i64}），零初始化 GlobalVariable/alloca 合法常量；CGVar 记 uninit ~~+ decl_depth~~（S59 t110 删除），读拒编保证零值不可观测——沿用 S45 t92 机制 |
| `tribool tb;` / `char c;` / `character d;` | Tri（i8）/Str（ptr）同理零初始化槽合法；char/character 经 declared_cgtype 映射 Str 承载 |
| `byte by;` / `word wd;` | i64 承载零初始化 + CGVar.bit_max（255/65535）；赋值走 visitAssign 通用路径——既有赋值点 check_bit_range 陷阱自动生效，不丢范围校验（实证：范围内赋值两端一致） |
| 赋值后读 / 全局函数体内读 / 循环体内声明 | 复用 S45 t92 的 uninit/~~decl_depth/同块清除~~（S59 t110 定赋区域跟踪）与链底快照机制，六新类型零新增控制流 |
| 范围外 | ~~array（elem 无从推断）/类类型~~（S54 t101 放行——array 建 opaque ptr 槽 + elem=Num 动态域哨兵、类类型建 Obj 槽 + cls）/~~Tuple（形状无从推断）~~（S61 t112 放行——延迟到首赋处按 RHS 形状建槽）无初始化维持拒编；~~分支/循环块内赋值后读维持拒编不错编~~（S59 t110：if/else 全路径定赋后读已放行）；零新增 collie_rt 接口 |

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
| 范围外 | 方法调用结果静态类型为 object（is_comparable_type 无 object 放行）不能直接参与 ==/!= 比较、word 值不能 return 给 byte 返回类型——属解释器语义边界（非 codegen 拒编面）；异质数组字面量/实例进数组/tuple 进函数签名维持拒编（活跃差分面后置候选；其中实例进数组已于 S53 t100、异质数组字面量已于 S86 t139 以 kind 7 动态标量承载陆续解锁，tuple 进签名已于 S87 t140 物化解锁、object 签名 tuple 位已于 S117 t170 打 tag 8 解锁）；零新增 collie_rt 接口 |

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
| `array a;` 无初始化声明 | visitVarDecl 无初始化分支加 KW_ARRAY 前置分支（静态类型放行块后、终点 unsupported 前）：槽照常创建（opaque ptr，顶层零初始化 GlobalVariable / 函数内 alloca），CGVar 记 elem=Num 动态域哨兵（t70——elem 无从静态推断，后续赋值走 visitAssign Arr 动态域透传）+ uninit ~~+ decl_depth~~（S59 t110 删除）；解释器语义绑 none，读被静态拒编故槽初值不可观测 |
| `Cat c;` 无初始化声明（c 为已注册类名） | visitVarDecl 加 IDENTIFIER 分支（`classes_.count(类名)` 守卫）：Obj 槽照常创建，CGVar 记 cls=声明类名 + uninit ~~+ decl_depth~~（S59 t110 删除）；后续赋值走 visitAssign Obj 分支，读出属性链/方法调用/upcast 动态分派全走 t61/t86 既有路径 |
| `a = […]` / `c = new Cat(...)` 同块赋值 | visitAssign 的 Arr 分支与 Obj 分支原 CreateStore 后即 `return` 早退，**不经通用路径 t92 的 uninit 清除逻辑**——故两分支各补清除~~`if (var->uninit && scopes_.size()==var->decl_depth) var->uninit=false`；深层块（分支/循环体）赋值存值不清标记~~（S59 t110：无条件清，条件发射区快照隔离） |
| 读 uninit / 深层块赋值后读 | visitIdentifier 读 uninit 拒编 "use of uninitialized variable"（复用 t92）；~~分支/循环块内赋值后读拒编不错编~~（S59 t110：if/else 全路径定赋后读已放行） |
| 范围外 | ~~无初始化 Tuple（形状无从推断——元素数/名字表静态不可知，解构槽组无从建，维持拒编 "variable declaration without initializer"，实证）~~（S61 t112 解锁：解构槽组延迟到首赋处按 RHS 形状建）；array 动态域索引读 kind ≥ 2 落既有 CG9 陷阱（elem=Num 哨兵，同 t70/t88）；零新增 collie_rt 接口 |

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
| 范围外 | ~~含 Bool/Str/嵌套(Tup/Arr/Obj)的异质与嵌套元素同质维持拒编（结果类型静态不可定且无统一表示）~~（含 Bool/Str 的异质即本行解锁；嵌套 Tup/Arr/Obj 与函数位容器元素已于 S119 t172 一并入 tags+bits 物化路径，读出即 Dyn 按运行期 tag 消费）；空 tuple 维持拒编；~~get() 动态键的同一面（数值系异质/全 Num 同质）未同步解锁~~（已于 S57 t108 解锁）；零新增 collie_rt 接口 |

**S57 降级补充（t108 实现）：数值系异质 tuple 动态键 get()**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t.get(k)`（k 运行期字符串，t 元素全 ∈ {Int/Double/Num}、≥1 非空名） | visitMethodCall Tup+get 动态键路径新增数值系分支（含全 Num 同质——原 "this element type" 拒编面一并解锁，与 S56 t107 索引同源机制）：逐元素 to_num 后物化 **tags+bits 双 int 数组**（均 kind 0），names 数组复用一份、同一键两次 rt_tuple_get 取回 make_num 拼 Num 动态值；未命中陷阱在首次 get（"Undefined tuple field '<key>'"，核心消息同 t84 既定分歧）；结果型 Num 走既有路径 |
| 同质 4 类/常量键（回归） | 保持 t84 单数组静态元素类型路径与 t68 编译期解析不变 |
| 守卫重构 | 同 S56：homogeneous/all_numeric 双标志一次扫描，两条既有拒编消息分工保留；无命名字段/非 Str 键守卫位置与消息不变 |
| 范围外 | 含 Bool/Str/嵌套的异质与嵌套元素同质维持拒编；空 tuple/无命名字段/非 Str 键维持拒编；零新增 collie_rt 接口（复用 rt_tuple_get 单 i64 返回调两次） |

**S58 降级补充（t109 实现）：类字段无初始值（构造器前缀定赋子集）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `class C { integer x; ... }`（字段无初值） | register_class_layout 不再拒编，置 CGField.uninit 标志，槽按声明类型照常布局；自引用类字段仍因未注册自然拒编 |
| `new C(...)` 实例化守卫 | 实例化类自身构造器体须以 `this.f = expr`（RHS 递归走查不含 this/base——new 期间实例未逃逸，仅此三类节点可观察本实例）覆盖全部 uninit 字段（含继承来的，不经 base 直赋即可），否则拒编 "not definitely assigned at start of constructor"；~~须为顶部连续前缀~~（S74 t125 放宽：定赋语句之间可夹杂不观察 this 的表达式语句）、~~仅顶层直线~~（S75 t126 放宽：递归下钻裸块与双臂 if，各臂副本求交，见下） |
| uninit 字段初始化 | visitNew 跳过初值求值，存 `Constant::getNullValue`（null/0/{0,0}）零值占位——守卫保证赋值前不可观察，占位值任意且安全（解释器 none 同样不可观察） |
| 有初值字段/既有面（回归） | 前缀内重赋有初值字段不破坏前缀；字段初始化→实参求值→构造器调用三段顺序不变 |
| 范围外 | none 可观察面维持拒编：无构造器/~~前缀被打断（如先 print 后赋值）~~（S74 t125 放行 this-free 语句夹杂）/~~定赋在控制流块内（if/while 等，非顶层直线不定赋）~~（S75 t126 放行裸块与双臂 if；单臂 if/循环/switch 仍拒编）/打断语句观察 this（读已定赋字段也保守拒编）/RHS 含 this 均拒编不错编；零新增 collie_rt 接口 |

**S59 降级补充（t110 实现）：无初始化变量定赋区域跟踪**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `x = v` 清除规则 | 三处赋值路径（通用/Arr/Obj）**无条件清 uninit**——codegen 单遍线性发射，同一发射区内发射顺序即支配顺序，赋值后同区后续读运行期必已过存储；t92 的 `scopes_.size()==decl_depth` 同块深度代理删除（无大括号 if 分支/三目臂不推作用域，同深度清除泄漏致两处既有错编洞，本轮实证并修复） |
| 八个条件发射区 | if 分支/while 条件+体/for 条件+体+增量（初始化定赋外泄合法）/do-while 体+条件（break 可跳体尾，保守隔离；~~体必执行一次但非全体支配~~ S62 t113：体内无本层 break/continue 时体末清除集外泄）/switch 候选链+各 body（逐段 restore：后段候选不支配前段 body、body 间互为替代路径）/三目两臂/multi-match 各臂+default 臂/短路 RHS：进区前 `uninit_snapshot()`、出区 `uninit_restore()`，区内清除不外泄 |
| 快照句柄 | `vector<pair<size_t, string>>`（scopes_ 下标+变量名）而非裸 CGVar*——区内 visitBlock 的 `scopes_.emplace_back()` 可触发 vector 扩容重分配（MSVC unordered_map 移动非 noexcept 时元素被拷贝），裸指针悬垂写旧存储失效；快照变量均居外层作用域，区内只 push/pop 更深层，按句柄重寻址恒命中 |
| if/else 汇合点交集 | 两分支各自记录清除集（分支后对比快照）并 restore，汇合点对 `(then_terminated || then_cleared) && (else_terminated || else_cleared)` 的变量清 uninit——运行期必走其一，两支均定赋则汇合后读安全；无 else 的单 if 不构成全路径，不取交集 |
| 终止支检测 | return/break/continue 后 visitReturn 等把插入点切到无前驱死块（ret.dead 等），getTerminator() 恒空——`branch_terminated` 补判 `bb->hasNPredecessors(0)`；终止支不达汇合点不约束交集（else 支 return 则仅 then 支定赋即可放行） |
| 既有错编洞修复 | 无大括号 if 分支 `if (c > 5) x = 99;`（parse_statement 不强制块、visitIf 不推作用域）与三目臂内赋值 `c > 5 ? (x = 9) : 0`——旧深度代理下同深度清 uninit 泄漏，零值槽 vs 解释器 none 错编；新机制下均转拒编 |
| 范围外 | 单支 if（含无 else 带块）/循环体/三目臂/switch body/短路 RHS 内赋值后**区外**读维持拒编不错编（运行期可能未经赋值，解释器 none）；~~do-while 体内赋值后区外读维持拒编~~（S62 t113 放行：体必执行一次且无本层 break/continue 时体末定赋外泄）；零新增 collie_rt 接口 |

**S62 降级补充（t113 实现）：do-while 体定赋外泄**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `do { x = v; ... } while (c);` 体内无条件定赋 | 体必执行一次——体末无条件定赋线性支配 cond→end（body_bb 无旁路进入 cond_bb）；visitDoWhile 体后**不** uninit_restore，体末清除集（到达出口必定赋集）外泄放行区外读 |
| has_loop_escape 逃逸扫描 | 新增递归 helper：体含本层 break/continue 时体末清除集不再支配出口（break 直接跳 end、continue 绕过体尾赋值）——保守隔离（restore）；下探 block/if、**不**下探嵌套 while/for/do-while/switch（其内 break/continue 绑定内层，不影响本体支配） |
| 体内 if/else 全路径定赋 | 体必执行一次 + visitIf 汇合点交集（t110）双重保证：体末 uninit 状态已是"两分支均定赋"的交集，无逃逸时直接外泄，区外读安全 |
| 嵌套 do-while | 外层体内内层循环**之后**的无条件定赋仍支配外层出口（内层循环自含 break/continue 不影响外层，has_loop_escape 不下探内层） |
| 范围外 | 体含本层 break/continue 时保守隔离维持拒编不错编（实证 p2：解释器可跑出值而 codegen 拒编）；while/for 体（可零次执行）维持拒编；零新增 collie_rt 接口 |

**S60 降级补充（t111 实现）：number → integer/decimal 窄化**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `f(n)`（n 为 number，形参 integer） | coerce_call_arg 新增 Num→Int 分支：`num_to_int_checked(num)`——ICmpNE(tag, 0) 分支 numnarrow.trap 调 `collie_rt_trap_num_narrow()`（无参，stderr "runtime error: Type mismatch: cannot assign decimal value to 'integer' variable" + exit(1)，核心消息对齐解释器 coerce_to_declared，位置前缀缺失属既定 CG 陷阱分歧）+ unreachable，cont 块返 num_bits 即 i64；六个调用点（函数/方法×2/构造器/base 构造器/base 方法）共用零改动 |
| `g(n)`（形参 decimal） | Num→Double 复用 to_double 既有 Num 分支（t90 tag select 免分支）：整数态 SIToFP 加宽、小数态 bitcast，双态皆合法无陷阱（对齐解释器 KW_DECIMAL 分支整数态加宽小数表示） |
| `integer i = n;` / `i = n;` / `this.count = n;` | coerce_for_slot 同样两分支：变量初始化/赋值/字段初始值/字段写全路径生效 |
| `function f() integer { return n; }` | visitReturn 新增 Num→Int（num_to_int_checked）与 Num→Double（to_double）两分支，对齐解释器返回值 coerce_to_declared 校验 |
| 范围外 | number→byte/word 由语义分析器前端静态拒绝（"Cannot initialize variable of type 'KW_BYTE' with value of type 'KW_NUMBER'"，两端一致非差分面，实证）；小数态窄化 integer 为运行期陷阱面非差分成功面（实证 p2：stdout 一致、陷阱消息核心一致）；新增 1 个 collie_rt 接口 collie_rt_trap_num_narrow |

**S61 降级补充（t112 实现）：无初始化 Tuple 变量声明**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `Tuple t;` 无初始化声明 | visitVarDecl 无初始化分支加 KW_TUPLE：形状（元素数/名字表）声明处无从推断，**不建槽**——CGVar 仅记 type=Tup + uninit，tup 保持 -1 哨兵（解构槽组延迟到首赋处建） |
| `t = (1, 2, 3);` 首次赋值 | visitAssign Tup 分支 `tup < 0` 识别首赋：RHS 非 Tup 拒编；create_tuple_var 按 RHS 形状建解构槽组（顶层升全局槽/函数内 entry alloca，嵌套递归，t68/t76 既有机制）、写 var->tup、清 uninit——形状自此固化 |
| 同形重赋 / 异形重赋 | 重赋路径走既有 store_tuple_var 逐槽写（形状不一致拒编不错编——解释器动态可行面，范围外）；补无条件清 uninit（t110 定赋区域跟踪：条件发射区已快照隔离，清除不外泄） |
| 条件区内首赋 | uninit_restore 只恢复 uninit 标志不恢复 tup——区内首赋建槽后区外读仍拒编（槽组已建但不可读，安全）；if/else 全路径定赋汇合点交集清除放行（t110）；区内首赋固化形状后区外同形重赋清 uninit 放行、异形拒编 |
| 范围外 | 换形状重赋维持拒编（形状固化，解释器动态可行但拒编不错编，实证 p3）；tuple 进函数签名/进数组既有拒编不变；零新增 collie_rt 接口 |

**S63 降级补充（t114 实现）：同质 Arr/Obj 元素 tuple 非常量索引与动态键 get()**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `t[i]`（i 变量/表达式，t 同质 Arr 元素 tuple） | visitIndex Tup 非常量守卫放行 `elem == Arr`（同时仍拒 Tup——无 LLVM 承载）；复用 t83 单数组物化路径 rt_arr_new(kind 4)+逐元素 elem_to_bits(PtrToInt)/rt_arr_set、rt_arr_get(动态 idx) 取 bits、bits_to_elem(IntToPtr) 还原内层数组 ptr；结果经 `tuple_dynamic_result` 回填内层 elem 供下游再索引/print/length |
| `t[i]`（t 同质 Obj 元素 tuple） | 守卫放行 `elem == Obj`（kind 5 实例数组，t100 机制）；结果经 `tuple_dynamic_result` 回填 cls 供下游字段读/方法调用（方法按对象头类 id 动态分派） |
| `t.get(k)`（k 运行期字符串，t 同质 Arr/Obj 命名 tuple） | visitMethodCall get() 动态键守卫同样放行 Arr/Obj，物化 names+values 后 `collie_rt_tuple_get` 扫描取 bits，结果同走 `tuple_dynamic_result` 回填元数据 |
| `tuple_dynamic_result` 结果元数据回填 | 单数组物化只给 {ptr, elem}，Arr 结果扫描各内层 elem：统一则回填、不一致降 Num 动态域哨兵（t94 同规则）；Obj 结果扫描各 cls：统一则回填、不一致逐个 nearest_common_ancestor 合流（t93/t100 同规则），~~无公共祖先拒编 "non-constant tuple access on incompatible instance elements" 不错编~~（该面已于 S119 t172 解锁——调用侧按 `tuple_obj_merged_cls` 判假回退 tags+bits Dyn 路径，helper 内保留同一判定作保守防御） |
| 范围外 | ~~含 Bool/Str 的异质 tuple 动态访问（结果类型运行期才定，需动态标记值，实证 p2/p4）~~（已于 S85 t138 以 CGType::Dyn 解锁）；~~同质 Tup 元素（嵌套 tuple 无单数组承载）维持拒编~~（嵌套 tuple 位与 Arr/Obj/Fn 容器位元素已于 S119 t172 入 tags+bits 物化路径，读出即 Dyn）；零新增 collie_rt 接口 |

**S64 降级补充（t115 实现）：实例数组同继承树互赋（upcast/downcast）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `array a = [new Dog(..)]; a = [new Animal(..)];`（下溯：var->cls=Dog ← Animal 元素数组） | visitAssign Arr 分支 Obj 元素异类守卫（原无条件拒）镜像标量 Obj：`v.cls != var->cls && !is_subclass_of(v.cls, var->cls) && !is_subclass_of(var->cls, v.cls)` 才拒——同树 upcast/downcast 放行，指针拷贝 store，var->cls 保持不变（不收敛 NCA） |
| `array a = [new Animal(..)]; a = [new Dog(..)];`（上溯：var->cls=Animal ← Dog 元素数组） | 同守卫放行；后续 arr[i].field 按 var->cls 偏移解码（Animal 级字段偏移沿链前缀一致），arr[i].method() 按对象头类 id 动态分派（覆写沿实际类生效） |
| 深链 / 多轮互赋 | Animal 槽 ← Puppy（跨两级 is_subclass_of 真）；同树链内 Dog 槽 ← Puppy 上溯 ← Animal 下溯反复互赋，var->cls 恒为声明元素类不变 |
| 范围外 | 兄弟类/无关类（互不为子类）整体互赋维持拒编不错编（特有字段偏移错位可能错值，实证 p3：解释器可跑而 codegen 拒编）；元素级整槽写 `arr[i]=x` 异类仍走 visitIndexAssign 既有守卫；零新增 collie_rt 接口 |

**S65 降级补充（t116 实现）：tuple 内实例元素同继承树互赋（同形状重赋 upcast/downcast）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `Tuple t = (new Animal(..), 1); t = (new Dog(..), 2);`（上溯：Animal 元素槽 ← Dog） | store_tuple_var Obj 元素守卫（原 `e.cls != var.cls` 无条件拒）镜像标量 Obj L1151 / 实例数组 t115 L1128：`e.cls != var.cls && !is_subclass_of(e.cls, var.cls) && !is_subclass_of(var.cls, e.cls)` 才拒——同树 upcast/downcast 放行，指针拷贝 store，槽 var.cls 保持不变（不收敛 NCA） |
| `Tuple d = (new Dog(..), 0); d = (new Animal(..), 9);`（下溯：Dog 元素槽 ← Animal） | 同守卫放行；后续 t[i].field 按槽声明类 var.cls 偏移解码（load_tuple_var 恒用 var.cls，Animal 级字段偏移沿链前缀一致），t[i].method() 按对象头类 id 动态分派 |
| stored 元数据回填 | `stored.elems.push_back({e.value, CGType::Obj, e.elem, var.cls})`——保留 var.cls 而非 e.cls，与槽解码一致（避免同树互赋后元数据异类导致下游二次解码错位；标量 Obj L1161 / Arr L1144 同规则） |
| 深链 / 命名 / 嵌套 / 全局 / 局部 | Animal 槽 ← Puppy 跨两级；命名 tuple 元素 .name/.get；嵌套 tuple 内 Obj 元素同树互赋（递归 store_tuple_var 自然覆盖）；全局 tuple 函数内同树重赋；函数内局部 tuple 同树互赋——全部同形状重赋前提 |
| 范围外 | 换形状重赋维持拒编（t68 形状固化，S61 t112 范围外）；兄弟类/无关类（互不为子类）元素互赋维持拒编不错编（实证 p3：解释器可跑而 codegen 拒编）；零新增 collie_rt 接口 |

**S66 降级补充（t117 实现）：tuple 变量换形状重赋（重建解构槽组）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `Tuple t = (1, 2); t = (3, 4, 5);`（扩容） | visitAssign Tup 分支同形状重赋（t68 逐槽写）外新增换形状路径：tuple_shape_matches 递归同形判定（元素数+名字表逐层一致、嵌套递归，元素类型不参与），不一致则 create_tuple_var 重建槽组——递归建全部子槽、嵌套换形状一并解决，旧槽组成死存储不影响运行期输出；var->tup 换新下标、清 uninit |
| `Tuple t = (1, 2, 3); t = (4, 5);`（缩容/整值打印） | 同路径重建；读路径 load_tuple_var 按新槽组逐槽 load，print/索引/字段/get/length 全按新形状工作 |
| 条件发射区内换形状 | CondDepthGuard RAII（构造 +1 析构 -1，C++ 栈帧作用域即区内发射作用域）跟踪 cond_depth_，8 个 t110 位点（if/while/for/do-while/switch/三目/multi-match/短路 RHS）区内换形状拒编不错编——区外读可能按新形状解码旧槽组错值（实证 p3） |
| 全局槽组跨函数换形状 | has_global_slot 递归查嵌套子槽组任一 GlobalVariable + fn_gen_at 基准（t106 同规则）：函数内写全局槽（链底副本降级不外传顶层）或声明后已有函数体快照旧槽组拒编；声明在后的全局槽不受影响（实证 p4） |
| 循环回边 | 无需单独守卫（与 t106 不同）：循环体即条件发射区（已拒），且重建是新槽组、旧读旧槽互不污染——与 Arr 共享单 ptr 槽不同 |
| 槽组名唯一化 | 重建槽组名追加 "#N"（tuple_rebuild_seq_ 递增）防全局符号同名冲突（verifyModule 失败） |
| 无初始化 Tuple 首赋后换形状 | t112 首赋建槽组后换形状重赋走同路径（var->tup ≥ 0 分支，t112+t117 同路径） |
| 范围外 | 条件发射区内换形状 / 全局槽组跨函数换形状维持拒编不错编（实证 p3/p4：解释器可跑而 codegen 拒编）；零新增 collie_rt 接口 |

**S67 降级补充（t118 实现）：实例数组单槽写同继承树互赋（arr[i] = x upcast/downcast）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `array d = [new Dog(..)]; d[0] = new Animal(..);`（下溯单槽写） | visitIndexAssign Obj 元素守卫（原仅放行 upcast `is_subclass_of(v.cls, object.cls)`，t100）镜像整体互赋 t115 追加 downcast `!is_subclass_of(object.cls, v.cls)` 才拒——同树任一方放行，rt_arr_set 直存位模式（运行期写读，无静态形状错配，无需 t117 类守卫） |
| `array a = [new Animal(..)]; a[0] = new Dog(..);`（上溯单槽写） | 同守卫放行（t100 既有路径回归）；跨两级 Puppy 同理 |
| 写入后访问 | d[0].field 按 object.cls（声明元素类）偏移解码——同树内父类级字段偏移沿链前缀一致；d[0].method() 按对象头类 id 动态分派（下溯写入的父类实例分派到父类方法） |
| 引用别名 / 函数内局部 / 循环内交替 | 单槽写即运行期写共享底层存储（rt_arr_set），别名经一者写入另一者可见；函数内局部/if 分支内/循环内多轮交替写均自然放行（无静态元数据变更） |
| 范围外 | 兄弟类/无关类（互不为子类）单槽写维持拒编不错编（特有字段偏移错位可能错值，实证 p3：解释器可跑而 codegen 拒编）；零新增 collie_rt 接口 |

**S68 降级补充（t119 实现）：混合类实例数组字面量 NCA 收敛**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `array m = [new Dog(..), new Cat(..)];`（兄弟混合） | visitArrayLiteral 元素类守卫（原要求全部同类 `v.cls != elem_cls` 无条件拒，t100）改为最近公共祖先收敛：nearest_common_ancestor(elem_cls, v.cls) 有公共祖先则 elem_cls = NCA（逐元素收敛），无公共祖先（空串）收敛 cls 空动态 object 元素（S84 t137，方法走 t133 分派、字段走 t142 类 id switch 分派） |
| `array a = [new Animal(..), new Dog(..)];`（下溯混合） | 同路径 NCA=Animal；跨两级 Puppy 同理（收敛到最近公共祖先） |
| `array deep = [new Dog(..), new Puppy(..)];`（深链混合） | NCA=Dog（跨两级收敛）；多元素 [Animal, Dog, Puppy, Cat] 逐元素收敛最终 NCA=Animal |
| 写入后访问 | m[i].field 按 NCA 偏移解码——同树内父类级字段偏移沿链前缀一致；m[i].method() 按对象头类 id 动态分派（兄弟子类各自分派到自己的方法） |
| 下游协同 | 整体互赋（t115）/单槽写（t118）在 NCA 数组上自然放行（var->cls 保持 NCA）；print 整数组 rt_arr_to_str kind 5 `<object>`（t100）对齐解释器递归转串；== 恒不等（t82 常量折叠对齐 values_equal） |
| 范围外 | ~~无公共祖先混合（跨树无公共字段前缀，如 [Dog, Robot]）维持拒编不错编~~（已于 S84 t137 解锁——收敛 cls 空动态 object 元素，方法走 t133 分派、print/len/== 经 kind 5 全承载，~~字段访问维持拒编~~字段读写已于 S89 t142 解锁）；零新增 collie_rt 接口 |

**S69 降级补充（t120 实现）：object 动态类型声明 Obj 初始值（静态初始类名）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `object x = new Dog(..);`（核心） | visitVarDecl 新增 KW_OBJECT 分支（原落 declared_cgtype default 拒编 "variable type 'object'"）：emit 初始值，~~非 Obj 拒编 "initializing 'object' variable with non-object value"~~（该拒编自 S93 t146 起按承载集放宽，S107 t160 收束为 `is_dyn_carrier`＝标量 ∪ Obj ∪ Arr，Tup 等仍拒编）；建 Obj 槽 store，scopes_ 登记 {slot, Obj, Int, init.cls}——槽记初始值类名（静态初始类名近似） |
| `x.sound()` / `x.name` / `x.describe()` | 零改动即工作：方法按对象头类 id 动态分派（t86/t102 全树收集）、字段按 cls 前缀偏移解码，与类名声明 t60 同规则 |
| `x = new Animal(..)` / `x = new Puppy(..)`（同树重赋） | 走 visitAssign Obj 既有守卫（t86/t103：同树 upcast/downcast 放行、var->cls 保持初始类名），指针拷贝 store |
| `object y = x;` / `object y = dogVar;`（互初始化） | 初始值为 Obj 表达式（CGType::Obj + cls）自然放行，指针拷贝即引用别名（字段写经别名可见） |
| 函数内局部 / 循环内重赋 | 与类名变量同路径；if 分支内重赋经 t110 定赋区域跟踪保护后读 |
| 范围外 | 非 Obj 初始值（string/number/none 等需动态值表示，官方示例 main.collie 的 object 用法属此，实证 p4 拒编）；无初始化 `object x;` 已由 S70 t121 解锁（首赋吸收 RHS 类名，非 Obj 值首赋仍拒编）；兄弟类/跨树重赋走 visitAssign 既有守卫拒编不错编（实证 p3）；零新增 collie_rt 接口 |

**S70 降级补充（t121 实现）：无初始化 object 变量声明（首赋吸收 RHS 类名）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `object x;`（无初始化声明） | visitVarDecl 无初始化分支新增 KW_OBJECT case（原落 "variable declaration without initializer" 拒编）：建 Obj 槽（llvm_type_of(Obj)）+ type=Obj + cls 留空占位 + uninit=true，镜像 t101 无初始化类类型变量分支（cls 不设即默认空串） |
| `x = new Dog(..);`（首赋） | visitAssign Obj 分支守卫前识别 `var->cls.empty() && v.type == Obj` 首赋吸收 RHS 类名（镜像 t112 首赋按 RHS 建槽模式）——吸收后走既有同树守卫（t86/t103：同树 upcast/downcast 放行、var->cls 保持吸收类）；非 Obj 值首赋仍拒编 "assigning incompatible value"（实证 p4） |
| `x.sound()` / `x.name`（首赋后访问） | 与类名声明同规则：方法按对象头类 id 动态分派、字段按吸收类前缀偏移解码；uninit 期读仍拒编（t92/t110 定赋区域跟踪） |
| if/else 双分支各首赋 + 汇合点读 | t110 汇合点两分支清除集交集清 uninit 放行读；两分支类同树（如 Dog/Puppy 子类 upcast）自然放行——then 分支先吸收、else 分支按吸收后 cls 判定 |
| 函数内局部 / 循环体内声明+首赋 | 与 t101 类类型变量同路径；循环体内每迭代新建槽，同块直线赋值后读放行 |
| 互初始化 `object b; b = a;` | 首赋 RHS 为 Obj 表达式（CGType::Obj + cls）自然吸收 a 的类名，指针拷贝即引用别名（字段写经别名可见） |
| 全局无初始化 object | 顶层直线首赋吸收后函数内同树重赋走既有守卫；函数外读按吸收类解码 |
| 范围外 | 非 Obj 值首赋（string/number/none 需动态值表示）维持拒编不错编（实证 p4）；if 无 else 分支内首赋后区外读维持 uninit 拒编不错编（t110 保守隔离，实证 p5）；条件区内首赋后跨树赋值只可能保守拒编不错编（cls 吸收持久化——t110 快照只隔离 uninit 标志，与 t101 类类型变量分支内赋值行为一致）；object 函数形参/返回值仍拒编；零新增 collie_rt 接口 |

**S71 降级补充（t122 实现）：number/decimal 值作下标**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `a[i]` / `a[i] = v` / `s[j]` / `t[k]`（i/j/k 为 number/decimal 整数态） | 三处原 "non-integer index" 静态拒编守卫（visitIndex tuple 非常量臂、visitIndex 数组/字符串臂、visitIndexAssign）改调新增 `index_to_int(index, line, col)`：Int 直接透传（静态整数下标零开销）；Double/Num 经 `to_double` 取 double 视图，`llvm.floor` 后 FCmpOEQ 判整数态——非整数态（含 NaN，FCmpOEQ 对 NaN 恒假）走新增陷阱 bb 调 `collie_rt_trap_index_integer()` + unreachable，整数态 clamp `[-4e18, 4e18]`（maxnum/minnum，防 FPToSI poison；±Infinity/超大值随后落 rt 越界陷阱）再 FPToSI 转 i64；调用点回填 `index.value` 并置 `index.type = Int`，下游 rt_arr_get/set、rt_str_index 全链路无需改动 |
| 负 number 下标 `a[-1]` | 归一为 i64 后交 rt `collie_rt_arr_norm_index` 既有负索引回绕（-1 = 最后一个元素），与解释器 normalize_index `i += size` 一致 |
| 非整数态下标 `a[1.5]`（范围外，rc=1） | 运行期调 `collie_rt_trap_index_integer` 打 stderr "Index must be an integer" 后 exit(1)，对齐解释器 normalize_index `raw != floor(raw)` 抛 "Index must be an integer"（裸消息同越界陷阱约定，省略行列前缀）——拒编不错编（不静默 floor 取错值）；错误面 rc=1 不在差分成功门禁内 |
| Bool/Str 等其余下标类型（范围外） | index_to_int default 臂维持拒编 "non-integer index" 不错编；新增 1 个 collie_rt 接口（collie_rt_trap_index_integer） |

**S72 降级补充（t123 实现）：类方法/构造器重载 first-wins**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 类内同名不同参的方法/构造器重载 | register_class_methods 原 "method overloading" 拒编改为：本类内同名键（`<类名>.<方法名>`）首个登记后，其余同名成员 `continue` 跳过（first-wins）——仅首个同名成员建 llvm::Function、入 dispatch 表；visitClass 体生成遍加 `stmt` 指针守卫（`if (m.stmt != method) continue;`），仅首个登记成员（stmt 指针相等）生成体，避免同一 llvm::Function 被重复建 entry 块（无效 IR）。镜像解释器 find_method 沿声明序首个同名者胜、call_class_method 仅对首个做元数检查、后续重载从不被调用 |
| 体内 `this.m()` 自调、父类多重载 + 子类覆写首个 | 沿用既有单态化机制（分派表 dispatch 存覆写解析后映射、继承副本沿链复制）：首个重载即分派表登记项，自调与继承/覆写交互与解释器动态分派逐字节对齐 |
| 调用非首个重载 `c.add(1,2)`（首个为单参，范围外 rc=1） | 解释器 RuntimeError "add() expects 1 argument(s), got 2"；codegen 首个（单参）签名元数不符拒编 "method arity mismatch"——两端同为报错（rc=1），拒编不错编；错误面不在差分成功门禁内 |

**S73 降级补充（t124 实现）：顶层控制流块内函数声明**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 顶层 if/while/for/do-while/switch/裸块内的 `function` 声明 | generate 阶段三原仅 `dynamic_cast<FunctionStmt>` 直登顶层函数，其余语句忽略（块内声明落 visitFunction "function declaration outside top level" 拒编）。改为：非函数顶层语句以 `std::to_string(top_idx)`（顶层语句序号）作合成前缀调 declare_nested_in 递归下钻各控制流块，登记为块作用域嵌套函数键 `<序号>.<名>`——数字前缀非法标识符首字符，天然不与用户顶层键冲突；visitFunction 首访按 t91 嵌套机制建原型 + 声明处绑定 fn_key 到块作用域 |
| 块内调用、嵌套块内声明、块内递归、块内调顶层函数 | 复用既有嵌套机制：声明处 `scopes_.back()[name] = {fn_key=key}`；递归经函数体生成时外层链 fn_key 拷入链底支持（t91 lines 3011-3017）；块内调顶层函数按顶层键解析 |
| 块退出后调用、块外作用域可见性（范围外 rc=1） | 块作用域绑定，块退出即失效——镜像解释器 `env_.define` 于执行到声明处、块退出即出栈；块外调用两端同为语义层 Undefined 报错（rc=1），拒编不错编；错误面不在差分成功门禁内 |
| 同一顶层语句内兄弟块同名（then/else 各声明 `f`） | declare_nested_in 对同一顶层语句子树透传同一合成前缀 → 键相同 → declare_function "function overloading" 拒编不错编；兄弟顶层语句各得不同序号前缀，同名函数互不冲突可共存 |

**S74 降级补充（t125 实现）：构造器无初值字段顶层直线定赋（可夹杂 this-free 语句）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 构造器体内先执行 print/无 this 调用，后定赋 uninit 字段 | visitNew uninit 字段守卫原要求构造器体顶部**连续前缀**全为 `this.f=<不观察 this>` 定赋，遇任何非定赋语句即 `break` 拒编。改为遍历顶层语句：`this.f=expr`（object 为 This、RHS 不观察 this）照旧从 pending 移除；非定赋的**表达式语句**经 `expr_observes_this(es->expression())` 判——不观察 this 则安全跳过（实例未逃逸，无 this 即无从触及任何 uninit 字段），继续扫描后续定赋 |
| 定赋语句间夹杂多条 this-free 语句 | 同上：每条 this-free 表达式语句跳过，pending 逐个被后续 `this.f=` 清空；`if (pending.empty()) break;` 于全部定赋后提前退出（后续语句无从暴露 none，即使观察 this 亦安全） |
| 打断语句观察 this / 非表达式语句（范围外 rc=1 或拒编） | 观察 this 的非定赋语句（如 `print(this.v)` 读未赋字段、`this.m()` 方法调用）或非表达式语句（if/while/for/return/vardecl，可能条件赋值或读未赋字段）→ `break` → pending 非空 → 拒编 "not definitely assigned at start of constructor"；镜像解释器字段先绑 none，若赋值前可读则 none 可观察——拒编不错编 |
| 定赋在控制流块内（范围外） | `if(c){this.v=n;}` 的赋值在 IfStmt 子块非顶层直线 → 顶层遍历遇 IfStmt（非表达式语句）break → 拒编（运行期 c 假则 none 可观察，不定赋）；错误面不在差分成功门禁内 |

**S75 降级补充（t126 实现）：构造器无初值字段经控制流定赋（if/else 双臂 + 嵌套裸块）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 构造器体内 if/else 双臂各赋同字段、嵌套裸块内定赋 | visitNew uninit 字段守卫从 S74 的顶层直线遍历抽为文件局部递归助手 `ctor_definite_assign_stmt`（匿名命名空间，仅依赖 AST + `expr_observes_this`，不触类成员）：裸块 BlockStmt 原地递归（必执行，等同直线）；IfStmt 当 `!expr_observes_this(condition)` 且 `else_branch()!=nullptr`（双臂齐备）时，then/else 各在 pending **副本**上递归，随后求交——仅"两臂皆删"的字段才离开 pending（任一臂副本仍含则保守保留） |
| 继承来的 uninit 字段经双臂 if 定赋 | 同上：pending 含继承字段（`cls.fields` 全量），双臂覆盖即清空；`base()` 委托不影响守卫（守卫只走实例化类自身构造器体） |
| 单臂 if / 一臂缺赋 / 条件观察 this（范围外 rc=1） | 缺 else（`else_branch()==nullptr`）、或某臂未赋同字段（求交后仍在 pending）、或 `expr_observes_this(condition)` → 保守停止 → pending 非空 → 拒编 "not definitely assigned at start of constructor"；镜像解释器：单臂/条件读 this 时某路径 none 可观察——拒编不错编 |
| 循环 / switch / return / 声明 / 观察 this 的非定赋语句（范围外） | `ctor_definite_assign_stmt` 仅识别 BlockStmt、双臂 IfStmt、this-free 表达式语句与 `this.f=<this-free>` 定赋；其余（While/For/DoWhile/Switch/Return/VarDecl，非定赋且观察 this 的表达式）一律停止（不定赋/可能读未赋字段），落拒编；循环体虽必执行但迭代次数不定、且循环内 this-free 判定复杂，保守不递归 |

**S76 降级补充（t127 实现）：裸块内类声明（class-in-bare-block）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 顶层裸块内 `class C {...}`（含嵌套裸块） | generate 第一遍类收集改经 `collect_classes_in_order`（按执行序：顶层 ClassStmt 直收、BlockStmt 递归下钻、其余语句不下钻），收得列表按序走 `register_class_layout` 全量 → 块内类置 `ready=false` → `register_class_methods` 全量；第二遍 visitClass 走到声明语句置 `ready=true` 并生成方法体——裸块恒执行，编译期注册与解释器"执行到声明语句才登记"时序等价 |
| 块内声明类的可见性（全局扁平表） | 解释器 `classes_` 为全局 map（非块作用域），块外 `new` 可见；codegen `classes_` 同为全局表，块外 new 与字段/方法访问按类名解析，两端一致 |
| if/while/for/do-while/switch 体内类声明（范围外） | 不收集 → 第二遍 visitClass 查表失败 → 拒编 "class declaration outside top level"；此类声明执行期条件性，`if(false){class Foo{}} new Foo()` 解释器 Undefined class rc=1，编译期无条件注册会让 codegen 静默通过（错编方向），故维持拒编 |
| 函数/方法体内类声明（范围外） | `in_function_` 守卫照旧 → 拒编 "class declaration inside function"（t60 既有面） |
| new 早于声明块（范围外） | 语义层已按序登记 `declared_classes_`，前向 `new Foo()` 两端同为 "Undefined class"/"Invalid type" rc=1；codegen 侧另置 `ready` 执行序守卫作防御（第二遍生成序即语句序） |

**S77 降级补充（t128 实现）：函数体内类声明（class-in-function）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 函数体内直线/裸块位置 `class C {...}` | `collect_classes_in_order` 增 FunctionStmt 分支递归下钻函数体（自身亦递归嵌套函数体），收集类按序进 `register_class_layout`/`register_class_methods`（保持预注册，方法体前向引用后续类的既有能力不变），并记 `ready=false` + `owner_fn=宿主函数`；第二遍生成函数体走到声明语句时 visitClass 置 `ready=true` 并生成方法体——函数被调用即体内顺序执行，与解释器"调用时登记 `classes_`"等价 |
| 体内类的字段/方法/继承与 upcast | 与顶层类同机制（struct 布局、方法单态化 `collie.C.D.m`、对象头 id 动态分派）；体内类可继承顶层类并覆写方法，upcast 后按动态 id 分派 |
| 方法体在函数体内生成（现场保护） | gen_method_body 收尾原复位顶层（`in_function_=false`、清空 this/类上下文）改为保存恢复 `in_function_`/`current_fn_`/`current_this_`/`current_class_name_`/`current_defining_class_`/返回类型三件套（同 visitFunction 的 saved 机制）——否则函数体内声明类会破坏外层函数语境；顶层调用时保存值即空值，行为不变 |
| 宿主可见性（范围外） | 函数内声明的类仅宿主函数体内可实例化：visitNew 宿主守卫 `owner_fn != nullptr && owner_fn != current_fn_` → 拒编 "declared inside function 'F' is not visible here"；函数是否被调用属运行期事实，静态无从判定，函数外/其它函数体内 new 一律拒编不错编 |
| if/while/for/do-while/switch 体内类声明（范围外） | 收集阶段不下钻条件/循环体 → 未进类表 → visitClass 拒编 "class declaration inside function"（函数内）/"class declaration outside top level"（顶层块内）；执行期条件性，编译期无条件注册即错编方向 |

**S78 降级补充（t129 实现）：类方法/构造器体内类声明（class-in-method）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| 类方法/构造器体内直线/裸块 `class C {...}` | `collect_classes_in_order` 收得 ClassStmt 后递归其 `members()` 内每个 FunctionStmt 的体（方法/构造器），宿主记为该方法 stmt，仍不下钻 if/while/for/switch；方法被调用即体内顺序执行，宿主守卫经 t130 统一为 `scope_stack_`（`owner_scope` = 该方法 stmt，生成期 `ScopePushGuard` 压栈） |
| 生成体语境（宿主匹配） | gen_method_body 置 `current_fn_ = &stmt`（t129，仅函数语境标记）并 `ScopePushGuard` 压入 `scope_stack_`（t130），收尾随 `saved_fn`/RAII 还原——方法体内声明类的场景必然出现"方法体内生成另一个类的方法体"，不还原会把 `scope_stack_` 残留为内层方法，使宿主函数后续语句的 new 误判宿主 |
| 继承单态化副本重访 | 父类方法体按子类分派上下文再生成一份（t61/t104），体内类声明语句随之重访；`bodies_generated` 保护使类方法体只生成一次（重复建 entry 会产生无效 IR），`ready` 仍置真 |
| 跨方法/顶层使用（范围外） | 方法体内类仅宿主方法体内可实例化：另一方法体或顶层 new 落宿主守卫（t130 `scope_contains` 比对 `scope_stack_`）拒编 "declared inside function 'F' is not visible here"（方法是否被调用/调用次序属运行期事实，静态无从判定） |

**S79 降级补充（t130 实现）：控制流块内类声明（class-in-block）**：

| Collie 构造 | LLVM IR 降级 |
|------------|--------------|
| `if/while/for/do-while` 各分支块、`switch` 各 case 块内 `class C {...}` | `collect_classes_in_order` 增 If/While/For/DoWhile/Switch 分支，递归收集其块内直线/裸块/控制流类声明，宿主记为**该块 Stmt***（不再下钻块内更深的条件块——更深块递归时以更深块为 owner）；类随全体收得列表一并 `register_class_layout`/`register_class_methods`（保持预注册），`ready` 置 false；第二遍生成到声明语句（在各自条件块 IR 内）置 `ready=true` 并生成方法体 |
| 作用域栈 `scope_stack_` | 进入函数/方法体（`ScopePushGuard scope_stack_, &stmt`）、`if/while/for/do-while` 各分支、`switch` 各 case 体时压入对应 Stmt*（FunctionStmt 或 BlockStmt），离开（含提前 return）由 RAII 弹出；方法体/函数体内再生成类方法体时栈自然嵌套 |
| 可见性守卫 | `visitNew`：`owner_scope != nullptr && !scope_contains(owner_scope)` → 拒编 "declared inside a block/function 'X' is not visible here"；仅当声明所属作用域仍在生成栈中（即当前位于其内或内层）时该类方可见 |
| 跨块/作用域外使用（范围外） | 块是否执行属运行期事实（如 `if(false){class C{}} new C()` 解释器可能 Undefined class rc=1），静态无从判定 → 块外/异块 new 一律保守拒编不错编；块内前向 new 仍由 `ready` 守卫拦截 |


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
| CG3 | 运行期类型校验（coerce_to_declared 五处）在编译产物中缺失 | 语义层静态保证覆盖的部分可省；动态部分（object/窄化）随 collie_rt 补（number→integer 小数态陷阱已于 t111 补齐） |
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
