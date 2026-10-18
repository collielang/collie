/**
 * @file code_generator.h
 * @brief AST → LLVM IR 代码生成器（M6 t49–t52，S1–S5 子集）
 *
 * 设计文档：compiler/codegen/README.md（类型映射/降级映射/阶段范围）。
 * 支持面：print、字符串/整数/小数/布尔字面量、算术 + - * / %、一元负号；
 * S3（t50）：变量声明/赋值（integer/decimal/bool/string）、比较、
 * 短路 && || 与 !、if/else、while、块作用域遮蔽；
 * S4（t51）：for/do-while/break/continue、二元三元表达式 a ? x : y；
 * S5（t52）：顶层函数声明/调用/return/递归（两遍：先建原型再生成函数体）；
 * S13（t60）：单类无继承——字段/构造器/方法/this（LLVM struct + 隐藏首参降级）；
 * S14（t61）：继承——字段 base-first 合并、方法按分派类单态化生成（collie.C.D.m）、
 * base(...)/base.method() 静态解析、实例作函数参数/返回值；
 * S15（t62）：number tagged 双表示（{i64 tag, i64 bits}，CG5 收窄）——
 * 算术/比较/转串下沉 collie_rt 单点对齐解释器语义。
 * S22（t69）：char/character（Str 承载，运行期即字符串）、byte/word（i64 承载 +
 * 赋值点范围陷阱）与位运算 & | ^ ~ << >>（i64 域，移位量 0-63 运行时检查）。
 * 遇到范围外的 AST 节点显式抛 CodeGenError，绝不静默错编。
 */
#pragma once

#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "../parser/ast.h"

namespace collie {

/**
 * @brief 代码生成期错误（不支持的构造、超出 i64 的整数字面量等）
 */
class CodeGenError : public std::runtime_error {
public:
    CodeGenError(const std::string& message, size_t line, size_t column)
        : std::runtime_error(message), line_(line), column_(column) {}

    size_t line() const { return line_; }
    size_t column() const { return column_; }

private:
    size_t line_;
    size_t column_;
};

/**
 * @brief 树遍历代码生成器：顶层语句列表 → llvm::Module（@main 收拢）
 *
 * 与解释器同构的访问者实现；表达式结果经成员 last_value_ 侧信道传递
 * （accept 无返回值），并附带 CGType 供 print 选格式符/算术选指令。
 */
class CodeGenerator : public ExprVisitor, public StmtVisitor {
public:
    CodeGenerator();

    /// @brief 生成整个程序模块（顶层语句收拢进 @main），verifyModule 门禁失败抛 CodeGenError
    void generate(const std::vector<std::unique_ptr<Stmt>>& statements,
                  const std::string& module_name);

    /// @brief 输出 .ll 文本（生成后调用）；驱动再调 LLVM 自带 clang 把 .ll 编成本地二进制
    std::string emit_ir() const;

    // ---- ExprVisitor ----
    void visitLiteral(const LiteralExpr& expr) override;
    void visitIdentifier(const IdentifierExpr& expr) override;
    void visitBinary(const BinaryExpr& expr) override;
    void visitUnary(const UnaryExpr& expr) override;
    void visitAssign(const AssignExpr& expr) override;
    void visitCall(const CallExpr& expr) override;
    void visitTuple(const TupleExpr& expr) override;
    void visitTernary(const TernaryExpr& expr) override;
    void visitMultiMatch(const MultiMatchExpr& expr) override;
    void visitArrayLiteral(const ArrayLiteralExpr& expr) override;
    void visitIndex(const IndexExpr& expr) override;
    void visitIndexAssign(const IndexAssignExpr& expr) override;
    void visitMethodCall(const MethodCallExpr& expr) override;
    void visitProperty(const PropertyExpr& expr) override;
    void visitPropertyAssign(const PropertyAssignExpr& expr) override;
    void visitNew(const NewExpr& expr) override;
    void visitThis(const ThisExpr& expr) override;
    void visitBaseCall(const BaseCallExpr& expr) override;
    void visitBaseMethodCall(const BaseMethodCallExpr& expr) override;

    // ---- StmtVisitor ----
    void visitExpression(const ExpressionStmt& stmt) override;
    void visitVarDecl(const VarDeclStmt& stmt) override;
    void visitBlock(const BlockStmt& stmt) override;
    void visitIf(const IfStmt& stmt) override;
    void visitWhile(const WhileStmt& stmt) override;
    void visitFor(const ForStmt& stmt) override;
    void visitDoWhile(const DoWhileStmt& stmt) override;
    void visitSwitch(const SwitchStmt& stmt) override;
    void visitFunction(const FunctionStmt& stmt) override;
    void visitReturn(const ReturnStmt& stmt) override;
    void visitClass(const ClassStmt& stmt) override;
    void visitBreak(const BreakStmt& stmt) override;
    void visitContinue(const ContinueStmt& stmt) override;

private:
    /// @brief 生成值的编译期类型标记（选 printf 格式符与算术指令用）
    enum class CGType {
        Int,     // i64（integer，妥协点 CG1）
        Double,  // double（decimal）
        Num,     // {i64 tag, i64 bits}（number tagged 双表示：tag 0=整数/1=小数，t62）
        Dyn,     // {i64 tag, i64 bits}（异质 tuple 非常量访问的动态标量承载，t138：
                 // tag 复用数组 kind 编码 0=int/1=dec/2=bool/3=str/5=object(t144)/
                 // 6=tribool；仅 print/toString/插值可消费，其余消费点拒编不错编）
        Bool,    // i1
        Tri,     // i8（tribool 三态编码 False=0 < Unset=1 < True=2，Kleene min/max，t65）
        Str,     // ptr → 常量字符串
        Arr,     // ptr → collie_rt 数组对象（t59，元素类型另记于 elem 字段）
        Obj,     // ptr → 类实例字段块（t60，类名另记于 cls 字段）
        Tup,     // 虚值：无 LLVM 承载（t68 静态展开，元素另记于 tuple_values_，value 恒 nullptr）
        TupDyn,  // ptr → collie_rt 动态 tuple 对象（t140：三元不同形状合流/进函数签名，
                 // 形状运行期才定；names(kind3)+values(kind7) 堆对象，仅 print/toString/
                 // 插值/length/非常量索引/动态 get()/== 消费，边界经 Dyn 既有面拒编）
        Fn,      // 函数值（t143）：ptr 承载 llvm::Function* 自身（静态可知，无运行期
                 // 对象）；仅 print/toString/插值/字符串拼接/==·!= 常量折叠（对齐
                 // 解释器 values_equal 无 Function 分支恒 false）/object 声明变量
                 // 槽存取消费；传参/返回/进容器/字段/关系比较落点拒编不错编；
                 // 间接调用经变量语义层已拦（callee 需 FUNCTION 符号），非差分面
        Void,    // 无值（none 返回函数的调用结果，S5 t52）
    };

    struct CGValue {
        llvm::Value* value = nullptr;
        CGType type = CGType::Int;
        CGType elem = CGType::Int; // 仅 type == Arr 时有意义：元素类型（t59）
        std::string cls;           // 仅 type == Obj 时有意义：类名（t60）
        int tup = -1;              // 仅 type == Tup 时有意义：tuple_values_ 下标（t68）
    };

    /// @brief 动态承载槽写侧的运行期类型守卫种类（t187/t188）：无初值声明槽整槽
    /// 换 `{tag,bits}` 承载后，赋入位必须满足声明类型的运行期校验，规则与解释器
    /// `coerce_to_declared` 的对应分支同源——命中即整槽保留，失配落
    /// `collie_rt_trap_dyn_convert` 陷阱退出（`object`／类名声明槽走该函数的
    /// `default:` 不校验分支，故置 None）
    enum class DynGuard : uint8_t {
        None,  // 无守卫
        Array, // array 声明槽（t187）：tag ∈ {4,7}
        Tuple, // Tuple 声明槽（t188）：tag == 8
    };

    /// @brief 变量存储槽（entry 块 alloca；顶层变量为 GlobalVariable，t73）
    /// + 编译期类型（S3 t50）
    struct CGVar {
        llvm::Value* slot = nullptr;
        CGType type = CGType::Int;
        CGType elem = CGType::Int; // 仅 type == Arr 时有意义：元素类型（t59）
        std::string cls;           // 仅 type == Obj 时有意义：类名（t60）
        int tup = -1;              // 仅 type == Tup 时有意义：tuple_vars_ 下标（slot 恒 nullptr，t68）
        long long bit_max = 0;     // byte/word 声明的范围上限 255/65535，0 即非位类型（t69）
        std::string fn_key;        // 非空即嵌套函数绑定（t91）：functions_ 改编键，slot 恒 nullptr
        bool uninit = false;       // 无初始化声明（t92/t110）：读拒编；直线区域赋值清，
                                   // 条件发射区（分支/循环体/三目臂等）快照隔离，
                                   // if/else 汇合点取两分支清除集交集
        // dyn_guard 仅 type == Dyn 有意义：无初值声明槽整槽换动态承载后的写侧
        // 运行期类型守卫种类（t187/t188），与解释器 coerce_to_declared 的对应
        // 分支同规则。静态落点（有初值 array/Tuple 变量、形参、返回值、类字段）
        // 由 dyn_to_arr_slot / dyn_to_tuple_slot 同规则把关，不需要此标记
        DynGuard dyn_guard = DynGuard::None;
        int cell_id = 0;           // 变量单元唯一标识（t141）：闭包按引用捕获的身份键——
                                   // 每个真实存储槽（形参/局部）创建时分配自增非零值，
                                   // 捕获项在宿主侧登记此 id，被捕获方在体内以收到的
                                   // ptr 形参重绑定同一 id；调用点比对 lookup 所得 CGVar
                                   // 的 cell_id 与捕获登记值，一致才传该槽指针（一致即
                                   // 同一单元：宿主 alloca、自递归转发 ptr、多层嵌套逐层
                                   // 转发），不一致即名字被内层局部遮蔽 → 保守拒编不错编。
                                   // 0 = 未分配（全局/函数绑定/元组等不可按单槽捕获者）
        size_t loop_depth = 0;     // 仅 type == Arr 有意义：声明时 loops_.size()（t106）——
                                   // 异型互赋降级仅限同层（循环回边下赋值前生成的
                                   // 静态读代码会按旧 elem 解码错值，跨层拒编不错编）
        size_t fn_gen_at = 0;      // 仅 type == Arr 有意义：声明时 fn_gen_count_（t106）——
                                   // 声明后有函数/方法体生成即可能链底快照旧 elem，
                                   // 全局槽异型互赋降级不回溯，拒编不错编
    };

    /// @brief tuple 静态展开值（t68）：元素值 + 平行名字表（无运行时对象，
    /// 元素类型/个数/名字编译期全可知——语义层对 tuple 元素零追踪，codegen 自建）
    struct CGTuple {
        std::vector<CGValue> elems;
        std::vector<std::string> names; // 与 elems 等长；无名元素为空串
    };

    /// @brief tuple 变量的解构槽组（t68）：每元素一个独立 CGVar 槽（嵌套
    /// tuple 元素递归指向 tuple_vars_ 子条目），同形状重赋值逐槽写
    struct CGTupleVar {
        std::vector<CGVar> slots;
        std::vector<std::string> names; // 形状的一部分：重赋值须名字表一致
    };

    /// @brief 循环上下文（S4 t51）：break/continue 跳转目标块
    /// continue_target：while/do-while 为条件块，for 为增量块（无增量则条件块）
    struct LoopContext {
        llvm::BasicBlock* continue_target = nullptr;
        llvm::BasicBlock* break_target = nullptr;
    };

    /// @brief 顶层函数信息（S5 t52）：两遍处理第一遍建原型登记于此；
    /// 类方法/构造器复用同结构（param_types 不含隐藏的 this 首参，t60）
    struct CGFunction {
        llvm::Function* fn = nullptr;
        std::vector<CGType> param_types;
        std::vector<std::string> param_cls; // 与 param_types 平行；仅 Obj 位有意义（t61）
        CGType ret_type = CGType::Void; // Void 即 none 返回
        std::string ret_cls;            // 仅 ret_type == Obj 有意义（t61）
        long long ret_bit_max = 0;      // byte/word 返回类型范围上限 255/65535，0 即非位类型（t97）
        /// 闭包按引用捕获项（t141）：非空即在签名尾部追加等长 ptr 形参（宿主槽地址），
        /// 体内以收到的 ptr 重绑定这些名字（cell_id 透传），调用点据 cell_id 一致性传参。
        /// 空即无捕获——现有嵌套函数（仅用形参/全局/自递归/兄弟）走原路径零改动。
        struct Capture {
            std::string name;   // 被捕获的外层变量名
            int cell_id = 0;    // 宿主单元 id（与 CGVar.cell_id 对应，调用点一致性校验键）
            CGType type = CGType::Int;
            CGType elem = CGType::Int; // 仅 type==Arr 有意义
            std::string cls;           // 仅 type==Obj 有意义
            long long bit_max = 0;     // byte/word 范围上限
            bool uninit = false;       // 宿主登记时仍为无初始化态（t141）：体内读该捕获
                                       // 落 visitIdentifier uninit 守卫保守拒编（解释器此值
                                       // 可能为 none，槽零值会错值），写则放行
            DynGuard dyn_guard = DynGuard::None; // 宿主声明槽的写侧守卫种类（t187/t188）：
                                                 // 被捕获方体内的写同走该守卫
        };
        std::vector<Capture> captures;
    };

    /// @brief 单态化方法实例（t61）：分派类 C 视角下 (定义类 D, 方法 m) 的
    /// 函数 collie.C.D.m；defining 供 base 解析，stmt 供体生成
    struct CGMethod : CGFunction {
        std::string defining;              // 定义类 D（base 解析起点）
        const FunctionStmt* stmt = nullptr; // 方法 AST（体生成用）
    };

    /// @brief 类字段信息（t60）：声明顺序即 struct 布局顺序
    struct CGField {
        std::string name;
        CGType type = CGType::Int;
        const VarDeclStmt* decl = nullptr; // 初始值表达式/错误位置取自声明节点
        std::string cls;                   // 仅 type == Obj 时有意义：类名（t72）
        long long bit_max = 0;             // byte/word 字段的范围上限 255/65535，0 即非位类型（t87）
        bool uninit = false;               // 无初始值字段（t109/S132）：本树内并非每个类都必被定赋时槽升级为 Dyn、实例化点预存 tag 10 空位
        // 遮蔽链（t182）：子类重名声明并入本槽时被覆写的历史声明，base-first
        // 顺序且仅登记带初值者——实例化点按序求值并逐个覆写同一槽（解释器实例
        // 字段是 name→value 扁平表，同名键父类初值仍求值、末次写入胜）
        std::vector<const VarDeclStmt*> shadowed;
        // 动态承载槽（t184/S131 + t185/S132）：本继承树内该字段名有承载不一致的
        // 遮蔽声明、或无初值且 none 可观察，物理槽类型/类名/上限三字段被
        // resolve_dyn_slots 统一改判为 Dyn 的 {tag,bits}（type=Dyn、cls/bit_max
        // 清空）；写侧校验不用这三项，按本类生效声明现算 field_carrier_of，与
        // 解释器 find_field 的「子类优先沿链」同源
        bool dyn_slot = false;
    };

    /// @brief 类信息（t60/t61）：注册遍登记合并布局与单态化方法原型，
    /// visitClass 生成全部实例的方法体
    struct CGClass {
        const ClassStmt* stmt = nullptr;
        std::string super;                  // 父类名，空即无继承（t61）
        uint64_t id = 0;                    // 类 id（注册序，t86）：对象头部存储，动态分派 switch 用
        llvm::StructType* type = nullptr;   // 头部 i64 类 id + 字段（字段 GEP 下标 = 逻辑下标 + 1，t86）
        std::vector<CGField> fields;                          // 父链 base-first 合并后顺序 = 字段下标
        std::unordered_map<std::string, unsigned> field_index; // 字段名 → 逻辑下标（GEP 需 +1 跳头部）
        std::unordered_map<std::string, std::string> dispatch; // 方法名 → 实例键 "D.m"（覆写解析后，含构造器）
        std::unordered_map<std::string, CGMethod> instances;   // "D.m" → 本类分派上下文的单态化实例
        bool bodies_generated = false;  // 方法体是否已生成（t128）：单态化副本重访不重建 entry
        const Stmt* owner_scope = nullptr;  // 声明所属作用域（t130）：顶层/裸块类为 nullptr
                                            // （恒可见）；函数/方法体类为该 FunctionStmt；
                                            // if/while/for/do-while/switch 各 case 体类为其 BlockStmt。
                                            // new 仅当 owner_scope 在 scope_stack_ 中（含嵌套）时可见
        bool ready = true;  // 执行序是否已到声明处（t127）：裸块内类声明注册期置 false，
                            // 第二遍 visitClass 走到声明语句时置 true——对齐解释器
                            // "执行到声明语句才注册"；new 早于声明处即拒编不错编
    };

    /// @brief 求值一个表达式子树，返回其 IR 值（accept + 侧信道取回）
    CGValue emit(const Expr* expr);

    /// @brief print 内建：逐参调用 collie_rt 垫片接口（空格分隔 + 换行），输出对齐解释器
    void gen_print(const CallExpr& expr);

    /// @brief 逻辑 && / ||：短路求值（与解释器对齐），仅 bool 域（tribool 属后续）
    void gen_logical(const BinaryExpr& expr);

    /// @brief 声明类型 token → CGType（S3 支持 integer/decimal/bool/string，其余报缺口）
    CGType declared_cgtype(const Token& type_token);

    /// @brief 声明类型 token → CGType + 类名（t61）：IDENTIFIER 查类表降 Obj+cls，
    /// 未注册类名/其余类型拒编；函数/方法签名处用（变量声明另有前置分支）
    void declared_signature_type(const Token& type_token, CGType& type_out,
                                 std::string& cls_out);

    /// @brief CGType → 对应的 LLVM 存储类型
    llvm::Type* llvm_type_of(CGType type);

    /// @brief 在函数 entry 块创建 alloca（IR 规范位置，利于后续 mem2reg）
    llvm::AllocaInst* create_entry_alloca(llvm::Type* type, const std::string& name);

    /// @brief 变量建槽（t73）：顶层（非函数体且作用域深度 1）建零初始化
    /// GlobalVariable（跨函数共享，初始值仍在 @main 按源序 store），其余走 entry alloca
    llvm::Value* create_var_slot(llvm::Type* type, const std::string& name);

    /// @brief 待存值对齐槽类型：仅 integer→decimal 隐式提升（与语义层一致），其余不匹配报错；
    /// slot_cls 仅槽为 Obj 时有意义（字段路径严格同类校验，t72——其余调用点 Obj 另有前置分支）
    /// allow_dyn_unbox：Dyn 动态承载值是否允许按运行期 tag 守卫拆入类型化槽（t147）；
    /// 仅 tuple 逐槽写路径关false（解释器 tuple 元素无类型校验，拆槽会陷阱对方可运行面）；
    /// dyn_target_name 非空时作陷阱消息里的落点类型名（byte/word 槽传 "byte"/"word"）
    llvm::Value* coerce_for_slot(const CGValue& v, CGType slot_type, const Token& where,
                                 const std::string& slot_cls = "",
                                 bool allow_dyn_unbox = true,
                                 const char* dyn_target_name = nullptr);

    /// @brief Dyn 拆槽目标判定（t147）：解释器 coerce_to_declared 有运行期
    /// 校验的六个标量落点；Arr 落点由 dyn_to_arr_slot 单独处理（t165），
    /// Tup/TupDyn/Fn/Void 需静态形状或运行期表示，Dyn 值进这些落点维持拒编
    /// 不错编（Obj 落点由 dyn_unbox_to_obj 单独处理，t158）
    static bool is_dyn_unbox_target(CGType t);

    /// @brief 位类型落点名（t147）：bit_max 255/65535 即 byte/word，0 即非位
    /// 类型返回 nullptr（Dyn 拆槽陷阱消息沿用槽类型本名）
    static const char* bit_type_name(long long bit_max);

    /// @brief 动态承载值拆槽（t147）：CGType::Dyn（{tag,bits}，kind 7 元素读出
    /// 或 object 动态槽值）存进 integer/decimal/number/bool/tribool/string 类型化
    /// 槽前，按解释器 coerce_to_declared 的接受表插运行期守卫——tag 命中即还原
    /// 静态表示（bits→i64/double/ptr、bool→i1、tribool→i8、数值/布尔→串经 rt
    /// 转串垫片），未命中调 collie_rt_trap_dyn_convert 报错退出（解释器同为
    /// 运行期报错，stdout 无增量）；target_name 非空时作陷阱消息的落点类型名
    /// （byte/word 落点）；调用侧保证 slot_type 属六类之一
    llvm::Value* dyn_unbox_to(llvm::Value* dyn, CGType slot_type, size_t line,
                              size_t column, const char* target_name = nullptr);

    /// @brief 动态承载值进类名（Obj 非空 cls）落点（t158）：解释器 coerce_to_declared
    /// 对类名 default 直存不校验，codegen 需静态布局才可解码——运行期守卫 tag==5
    /// 且对象头类 id ∈ {声明类 ∪ 其后代类}（字段沿继承链前缀偏移、方法按 id 动态
    /// 分派）时还原实例 ptr，其余落 collie_rt_trap_dyn_convert（消息用声明类名作
    /// 落点类型）；调用侧保证 cls 已注册
    llvm::Value* dyn_unbox_to_obj(llvm::Value* dyn, const std::string& cls,
                                  size_t line, size_t column);

    /// @brief 动态承载值进 array 落点（t165）：解释器 coerce_to_declared 的 KW_ARRAY
    /// 分支只要求 is_array()，元素 kind 随数组对象运行期自带——运行期守卫 tag ∈
    /// {4, 7}（同质嵌套数组位 t85/t160、异质数组位 t139）时还原 opaque ptr，其余位
    /// 落 collie_rt_trap_dyn_convert（落点类型名 "array"，消息核心即解释器
    /// "Type mismatch: cannot assign <kind> to 'array' variable"）；调用侧须把落点
    /// 记录的 elem 降 Num 动态域哨兵（t70），读写按运行期 kind 产出 Dyn
    llvm::Value* dyn_to_arr_slot(llvm::Value* dyn);

    /// @brief 动态承载值写 array 声明的动态承载槽守卫（t187）：与 dyn_to_arr_slot
    /// 同一 tag ∈ {4,7} 判定与同一 collie_rt_trap_dyn_convert("array") 陷阱，
    /// 但整槽 {tag,bits} 原样返回（位内 kind 随数组对象自带，读侧按运行期 tag 分派）
    llvm::Value* dyn_guard_arr(llvm::Value* dyn);

    /// @brief 动态承载值写 Tuple 声明的动态承载槽守卫（t188）：与 dyn_to_tuple_slot
    /// 同一 tag == 8 判定与同一 collie_rt_trap_dyn_convert("Tuple") 陷阱，
    /// 但整槽 {tag,bits} 原样返回（tuple 位本就等价于堆 tuple 对象 ptr + tag 8）
    llvm::Value* dyn_guard_tuple(llvm::Value* dyn);

    /// @brief 按声明槽的守卫种类对 Dyn 右值施加写侧守卫（t187/t188）：右值的静态
    /// 类型已能保证满足声明时原样返回（不打无用的运行期分支）
    llvm::Value* dyn_guard_store(llvm::Value* boxed, DynGuard guard, CGType rhs);

    /// @brief 动态承载值进 Tuple 落点（t179）：解释器 coerce_to_declared 的 KW_TUPLE
    /// 分支只要求 is_tuple()——运行期守卫 tag == 8（t170 的 tuple 位）时把 bits 还原
    /// 为堆 tuple 对象 ptr，其余位落 collie_rt_trap_dyn_convert（落点类型名 "Tuple"，
    /// 消息核心即解释器 "Type mismatch: cannot assign <kind> to 'Tuple' variable"）
    llvm::Value* dyn_to_tuple_slot(llvm::Value* dyn);

    /// @brief 同名方法在各定义者间的签名是否全等（t86 防御谓词化，t183）：全等时
    /// 调用侧沿用既有静态分派（实参/返回值按基线签名对齐、结果静态类型），
    /// 否则交 emit_mismatched_dispatch 逐臂按各自签名动态拆槽
    static bool signatures_consistent(const std::vector<const CGClass*>& defs,
                                      const std::string& name,
                                      const CGMethod& base);

    /// @brief 跨类同名异签名方法的动态分派（t183）：解释器 call_class_method 先
    /// 求值实参、再按**动态命中方法**的形参逐个 coerce_to_declared，返回值同按该法
    /// 声明类型转换——异签名并非语义错误，只是结果静态类型不可定。形态取「实参
    /// 一次 to_dyn 打框 → 每个定义者臂按自身签名经 coerce_call_arg 的 Dyn 源分支
    /// 运行期拆槽后调用 → 返回值逐臂打框，在 {i64,i64} PHI 合流为 Dyn」，
    /// 消费侧即落全部既有 tag 分派臂；全臂返回 none 时结果仍记 Void。
    /// dyn_tag/dyn_bits 非空表示接收者来自 toString 拆分（t159），未命中臂的兜底
    /// 按运行期 tag 转串；调用侧保证 defs 已按类 id 排序且元数与 arguments 相符
    void emit_mismatched_dispatch(const std::vector<const CGClass*>& defs,
                                  const std::string& name, llvm::Value* receiver,
                                  const std::vector<CGValue>& arguments,
                                  llvm::Value* clsid, llvm::Value* dyn_tag,
                                  llvm::Value* dyn_bits, size_t line,
                                  size_t column);

    /// @brief 三目/`==?` 臂的静态类型合流（t93/t94/t65 规则）：成功时 result_*
    /// 即合流结果并返回 Ok；失败只报告失败种类（此时 result_* 不可用），由调用方决定
    /// 「全臂动态承载 → 合流为 Dyn」回退或维持原拒编消息
    enum class ArmMerge { Ok, NoCommonAncestor, Incompatible };
    ArmMerge merge_static_arm_types(const std::vector<CGValue>& values,
                                     CGType& result_type, CGType& result_elem,
                                     std::string& result_cls);

    /// @brief 全部臂是否落 is_dyn_carrier 承载集（标量/实例位/数组位）：静态合流
    /// 失败时的动态合流准入（t168）——Tup/Fn/Void 臂返回 false 维持原拒编
    static bool arms_all_dyn_carrier(const std::vector<CGValue>& values);

    /// @brief 动态承载值参与算术的数值化（t148）：Dyn（{tag,bits}）侧插运行期
    /// 守卫——tag < 2（整数态/小数态）时与 Num 同构直接透传，其余（bool/串/
    /// tribool/实例/数组位）落 collie_rt_trap_non_numeric_arith 报错退出
    /// （解释器 eval_arithmetic 的 "Arithmetic operands must be numbers" 同
    /// 运行期报错，陷阱路径 stdout 无增量）；非 Dyn 侧走既有 to_num 加宽
    llvm::Value* dyn_num_operand(const CGValue& v, const Token& op);

    /// @brief 含 Dyn 操作数的 '+'（t148）：解释器任一侧为字符串即两侧
    /// to_string 拼接，而 Dyn 侧是否字符串只有运行期可知——按 tag==3 分派
    /// 两路（拼接：rt_concat 结果打 tag 3；算术：双侧数值守卫后下沉
    /// rt_num_arith），合流为 CGType::Dyn 值
    llvm::Value* dyn_concat_or_add(const CGValue& lhs, const CGValue& rhs,
                                   const Token& op);

    /// @brief 动态承载值参与比较的侧盒化（t149）：把比较的一侧统一成
    /// {tag,bits}（与 Num/Dyn 同构）交运行期分派——Dyn/Num 原样透传、Int 打
    /// tag 0、Double 打 tag 1（位模式）、Str 打 tag 3 存 ptr 位模式（编码同
    /// 数组 kind）；其余侧（bool/tribool/Obj/Arr/Tup/Fn/Void）由调用侧先行
    /// 拒编，此处落 unsupported 防御
    llvm::Value* dyn_cmp_box(const CGValue& v, const Token& op);

    /// @brief 由内向外逐层查找变量；未找到返回 nullptr（语义层已保证先声明，防御用）
    CGVar* lookup_var(const std::string& name);

    /// @brief 定赋区域跟踪（t110）：条件发射区进入前快照全部 uninit 变量，
    /// 退出时恢复——区内赋值清的 uninit 不外泄（运行期可能未执行）；
    /// if/else 汇合点另对两分支清除集取交集（终止支不达汇合点计全集）。
    /// 句柄为 (scopes_ 下标, 变量名)——区内 visitBlock 可能扩容 scopes_
    /// 重分配（MSVC unordered_map 移动非 noexcept 时元素被拷贝），
    /// 裸 CGVar* 会悬垂
    std::vector<std::pair<size_t, std::string>> uninit_snapshot();
    void uninit_restore(const std::vector<std::pair<size_t, std::string>>& snap);

    /// @brief do-while 体是否含本层 break/continue（t113）：体必执行一次，
    /// 本层无逃逸时体末清除集支配循环出口可外泄（不 restore）；下探
    /// block/if 但不下探嵌套循环/switch（其内 break/continue 绑定内层）
    bool has_loop_escape(const Stmt* s);

    /// @brief 三元表达式：两分支 bool/tribool 条件（unset 走 false 分支）+
    /// 三分支 tribool 形式 a ? x : y : z 三路分派（t65）；分支值 PHI 汇合
    /// （类型不同时 int→double/num 提升、bool→tribool 加宽）
    void gen_ternary(const TernaryExpr& expr);

    /// @brief `==?` 多路匹配（t64）：级联比较块链（首命中 + 惰性求值）+
    /// merge 块 PHI；无默认分支仅 tribool 目标穷尽三态合法（链尾静态
    /// 不可达 unreachable，t65），其余无默认拒编
    void gen_multi_match(const MultiMatchExpr& expr);

    /// @brief `==?` 候选相等比较（t64）：复用 == 的降级出 i1
    /// （Str×Str strcmp==0、任一 Tri 双方加宽三态 icmp（t65）、任一 Num 走
    /// rt_num_cmp op 0、Bool×Bool icmp、Int/Double icmp/fcmp 含混型提升）；
    /// 其余类型组合拒编
    llvm::Value* gen_match_eq(const CGValue& target, const CGValue& cand,
                              const Token& op);

    /// @brief tuple 相等比较（t75）：静态展开逐元素深比较出 i1（对齐解释器
    /// values_equal Tuple 分支：先比元素数再比名字表，形状不一致编译期即
    /// 常量 false）；元素同域标量复用四路降级 And 链合并、嵌套 tuple 递归、
    /// 异型配对/Obj 恒 false（kind 不等/无 Instance 分支）；双 Arr 元素
    /// 下沉 rt_arr_eq 深比较（t79，动态长度静态展开不可达）
    llvm::Value* gen_tuple_eq(const CGValue& lhs, const CGValue& rhs,
                              const Token& op);

    /// @brief tuple 合流静态展开（t95）：三元/==? 各支同为 tuple 且形状一致
    /// （元素数+名字表递归一致）时逐元素 PHI 合流为新鲜 CGTuple，返回
    /// tuple_values_ 下标；元素类型合并复用标量合流规则（数值提升 Num/Double、
    /// Tri/Bool 加宽、Arr elem 不等降 Num 哨兵 t94、Obj cls 求 NCA t93），
    /// 嵌套 tuple 递归；形状/名字不一致或元素不可合并拒编不错编。各支末块
    /// 由本函数补对齐指令与 Br 至 merge_bb，返回时插入点已在 merge_bb
    int merge_tuple_arms(const std::vector<int>& tups,
                         const std::vector<llvm::BasicBlock*>& ends,
                         llvm::BasicBlock* merge_bb, const char* what,
                         size_t line, size_t column);

    /// @brief tuple 变量同形状判定（t117）：var_idx 槽组与 val_idx 展开值
    /// 元素数+名字表递归一致（嵌套 Tup 元素递归）——一致走逐槽写，
    /// 不一致即换形状重赋（重建槽组）
    bool tuple_shape_matches(int var_idx, int val_idx) const;

    /// @brief tuple 槽组是否含全局槽（t117）：顶层槽组升 GlobalVariable（t76），
    /// 换形状重建会新建全局符号——函数体链底快照旧槽组不回溯，递归查含
    /// GlobalVariable 任一子槽即真（嵌套递归）
    bool has_global_slot(int var_idx) const;

    /// @brief 顶层函数建原型（第一遍，S5 t52）：同名重载拒编；none 返回降 void；
    /// prefix 非空即嵌套函数（t91）：注册键/符号名改编为 prefix.name（用户
    /// 标识符无 '.'，与顶层名天然不冲突），并递归下探函数体登记更深嵌套
    void declare_function(const FunctionStmt& stmt, const std::string& prefix = "");

    /// @brief 递归下探语句子树登记嵌套函数原型（t91，第一遍）：Block/If/
    /// While/For/DoWhile/Switch 各分支体全覆盖；命中 FunctionStmt 即以
    /// prefix 改编名 declare_function 并进 nested_fns_ 注册表
    void declare_nested_in(const Stmt* s, const std::string& prefix);

    /// @brief 闭包按引用捕获的第二遍物化（t141）：嵌套函数体生成前，若其体
    /// 引用宿主（普通函数/块作用域函数，非类方法）形参或局部，则收集这些自由
    /// 变量、按宿主 scope 解析为捕获项，并用「原参数 + 等长 ptr 捕获参数」重建
    /// llvm 函数原型（替换第一遍建的无捕获原型）。捕获为空则不重建（走原路径）。
    /// 类方法体内嵌套不收集捕获（单态化副本各帧槽不同，跨副本 ptr 会悬垂/错元，
    /// 保守维持标识符不可见拒编，同 t104）。元组/TupDyn? 元组槽非单一 ptr 亦跳过
    void build_closure_prototype(const FunctionStmt& stmt, const std::string& key);

    /// @brief 作用域守卫（t130）：构造压栈、析构出栈，配合 scope_stack_ 维护声明作用域；
    /// 进入函数/方法体、if/while/for/do-while 各分支、switch 各 case 体时 RAII 压入
    /// 对应 Stmt*（FunctionStmt 或 BlockStmt），离开（含提前 return/异常）自动弹出
    struct ScopePushGuard {
        std::vector<const Stmt*>& stk;
        explicit ScopePushGuard(std::vector<const Stmt*>& s, const Stmt* o) : stk(s) {
            stk.push_back(o);
        }
        ~ScopePushGuard() {
            if (!stk.empty()) stk.pop_back();
        }
    };
    /// @brief owner_scope 是否在当前 scope_stack_ 中（t130，含嵌套）：new 仅当声明
    /// 所属作用域仍在生成栈中（即当前位于其内或内层）时可见
    bool scope_contains(const Stmt* owner) const {
        for (const Stmt* e : scope_stack_)
            if (e == owner) return true;
        return false;
    }

    /// @brief 按执行序收集类声明（t127/t128/t130）：顶层 ClassStmt 直接收；裸块
    /// BlockStmt 恒执行、函数体被调用即顺序执行，二者递归下钻；if/while/for/
    /// do-while 各分支、switch 各 case 体为条件执行块，亦递归收集（宿主记为各自
    /// 块，仅限"声明与 new 同块"由 scope_stack_ 守卫——编译期无条件注册会让
    /// codegen 比解释器宽松：解释器条件为假不登记，属错编方向）；非顶层类名记入
    /// deferred_classes（值 = 宿主 Stmt*，裸块内为外层 owner）供 ready/owner_scope 标记
    void collect_classes_in_order(
        const std::vector<std::unique_ptr<Stmt>>& stmts,
        std::vector<const ClassStmt*>& out,
        std::unordered_map<std::string, const Stmt*>& deferred_classes,
        const Stmt* owner = nullptr);

    /// @brief 类布局注册（第一遍阶段一，t60/t61）：父链字段 base-first 合并 +
    /// 自身追加登记字段描述符；同名字段遮蔽按承载一致合并（t182）、承载不一致
    /// 记入 dyn_slots_ 待 resolve_dyn_slots 升级（S131）；范围外字段类型拒编；
    /// 无初值字段（S132）与可观察 none 的判定都在 resolve_dyn_slots 统一做，
    /// 注册遍只记 uninit 标记。父类须先声明（合并依赖父类已注册）。
    /// struct 本体延后到 finalize_class_layout 建（槽升级要回改祖先槽类型）
    void register_class_layout(const ClassStmt& stmt);

    /// @brief 动态承载槽升级（第一遍阶段一之后，S131/S132）：解释器实例字段是
    /// name→value 一张扁平表、读写按**动态类**声明逐笔 coerce_to_declared，故
    /// 一个字段名只有一份存储——把 dyn_slots_ 里的字段名在所属继承树的**全部**
    /// 持有者（祖先/兄弟/后代，前缀偏移须继续对齐）统一升级为 CGType::Dyn 的
    /// {tag,bits} 物理槽。两个来源：承载不一致的同名遮蔽（S131）、无初值字段
    /// 在本树内并非每个类都必被定赋因而 none 可观察（S132）。
    /// 声明各自的校验承载由 field_carrier_of 现算，不受本升级影响
    void resolve_dyn_slots();

    /// @brief 无初值字段槽是否「实例化即必被定赋」（S132）：root 树内每个类都要
    /// 满足——本类合并布局里该字段生效声明带初值（解释器 visitNew 沿链 base-first
    /// 逐层写扁平表，末次写入胜），否则本类构造器（不继承、父构造器不隐式调用）
    /// 体必须在走查到尾前定赋该字段；走查中断或字段未清空即 none 可观察
    bool tree_always_assigns_field(const std::string& root,
                                   const std::string& field_name) const;

    /// @brief 类 struct 建体（第一遍阶段一之后，S131）：按 cls.fields 的物理类型
    /// 拼装（元素 0 恒 i64 类 id 头部）——须在 resolve_dyn_slots 之后，
    /// 因槽升级要把祖先槽改判 Dyn
    void finalize_class_layout(const ClassStmt& stmt);

    /// @brief 字段声明的承载（t87/t162/t179 映射单点，S131 抽出）：类名 → Obj + cls、
    /// object → Dyn、Tuple → TupDyn、byte/word → Int + bit_max，其余走 declared_cgtype；
    /// 未注册类名拒编。注册遍与读写校验点（visitNew/visitPropertyAssign）共用，
    /// 避免遮蔽槽物理类型与声明承载分叉后两处各算一遍漂移
    void field_carrier_of(const VarDeclStmt& field, CGType& type_out,
                          std::string& cls_out, long long& bit_max_out);

    /// @brief 类所在继承树的根（t182/t184）：沿 CGClass.super 上溯；
    /// 要求 name 已在 classes_ 中（注册进行中的类传其父类名）
    std::string class_root_of(const std::string& name) const;

    /// @brief 字段写入值（S131）：按**本类生效声明**的承载（field_carrier_of）做
    /// 转换与 byte/word 范围校验（对齐解释器 coerce_to_declared 用 find_field 沿链
    /// 命中的那份声明，而非槽的物理表示），再在遮蔽冲突槽（dyn_slot）上打框为
    /// {tag,bits}；返回值的 type/cls/elem 即落槽与赋值表达式值应携带的元数据
    CGValue field_slot_store(const CGField& field, const VarDeclStmt& decl,
                             const CGValue& v, const Token& where);

    /// @brief 两字段声明的校验承载是否一致（S131）：遮蔽冲突标志 + 类型 + 类名 +
    /// 位类型上限全等；写侧多定义者分派用它决定能否沿用单一转换
    bool field_carrier_equal(const CGField& a, const CGField& b);

    /// @brief 类方法单态化原型（第一遍阶段二，t61）：继承父类全部实例的
    /// 本类副本 collie.C.D.m + 自身方法 collie.C.C.m（覆写即 dispatch 改指）
    void register_class_methods(const ClassStmt& stmt);

    /// @brief 类方法/构造器函数体生成（第二遍，t60/t61）：与 visitFunction 同机制，
    /// 另维护 current_this_/current_class_name_/current_defining_class_ 供 this/base 解析
    void gen_method_body(const CGClass& cls, const CGMethod& method);

    /// @brief 实参按形参类型对齐（t61 提取）：仅 integer→decimal 提升；
    /// Obj 要求类名严格相等（向上转型拒编，静态 cls 即动态类的前提）
    llvm::Value* coerce_call_arg(const CGValue& a, CGType want,
                                 const std::string& want_cls, size_t line,
                                 size_t column);

    /// @brief 隐式 number/bool → string 转换（t131，对齐解释器 coerce_to_declared
    /// KW_STRING 分支）：number/bool 隐式转 string，返回 i8*；其余类型调用方须先
    /// 判类型（tri/arr/obj/tup 不经此处，维持拒编）
    llvm::Value* coerce_to_string_value(const CGValue& v);

    /// @brief 沿继承链自 start 类向上查首个自身定义了 mname 的类（t61，
    /// base 解析用，对齐解释器 find_method 的自子向父查找）；未找到返空串
    std::string find_defining_class(const std::string& start,
                                    const std::string& mname);

    /// @brief sub 是否为 ancestor 的（真）后代类（t86，向上转型判定）：
    /// 沿 CGClass.super 链自 sub 向上查找；sub == ancestor 时返回 false
    bool is_subclass_of(const std::string& sub, const std::string& ancestor);

    /// @brief 求 a 与 b 的最近公共祖先类（t93，分支实例合流用）：含自身
    /// 端点（a 即 b 祖先返 a、反之返 b）；无公共祖先返回空串由调用方拒编
    std::string nearest_common_ancestor(const std::string& a, const std::string& b);

    /// @brief 把 Int/Bool 值提升为 double（算术混型时用）
    llvm::Value* to_double(const CGValue& v);

    /// @brief Bool/Tri → i8 三态编码（t65）：bool 加宽 select 2/0，Tri 透传；
    /// 其余类型拒编（编码 False=0 < Unset=1 < True=2）
    llvm::Value* to_tri(const CGValue& v);

    /// @brief number 值组装（t62）：tag + bits → {i64, i64} first-class struct
    llvm::Value* make_num(llvm::Value* tag, llvm::Value* bits);

    /// @brief number 值拆解（t62）：extractvalue 取 tag / bits 分量
    llvm::Value* num_tag(llvm::Value* num);
    llvm::Value* num_bits(llvm::Value* num);

    /// @brief Dyn 实例位守卫（t145）：CGType::Dyn（kind 7 {tag,bits}）上做
    /// 字段读/写或方法调用前，运行期校验 tag==5（实例位）并经 IntToPtr 还原
    /// 实例 ptr（交既有空 cls 动态分派 t142/t133）；非实例位落既有
    /// undefined method/property 陷阱报错退出（解释器同为运行期报错；
    /// 标量自有成员如数值 .abs 亦落陷阱，拒错编从陷阱不静默错值）
    llvm::Value* dyn_obj_guard(const CGValue& v, const std::string& member,
                               bool is_method);

    /// @brief 类实例字段读（t60/t102/t103/t142，t170 自 visitProperty 抽出）：
    /// 单类树直 GEP + load，多类树按对象头类 id switch 分派 PHI 合流；
    /// trap_when_undefined 仅在 Dyn 接收者的 tag==5 分派臂传 true——无定义者时
    /// 落运行期 undefined property 陷阱并返回常量 Dyn 占位（该块已 terminate，
    /// 不再发指令），静态接收者维持原拒编不错编
    CGValue read_obj_field(const CGValue& object, const std::string& name,
                           size_t line, size_t column, bool trap_when_undefined);

    /// @brief Dyn 实例位类 id 取址拆分（t159）：接收者为 CGType::Dyn 且该方法
    /// 存在类定义（须走 t133 空 cls 动态分派）时，tag==5 才解引用对象头取类 id，
    /// 其余标量位以哨兵 id 落分派 default 臂（避免对非指针位模式解引用）；返回
    /// join 块上的类 id PHI，调用点同时以 IntToPtr(bits) 作 this 实参（哨兵路径
    /// 不会调用任何方法，该 ptr 不被解引用）
    llvm::Value* dyn_obj_clsid_or_sentinel(llvm::Value* tag, llvm::Value* bits);

    /// @brief Int/Double/Num → Num（t62）：integer/decimal→number 加宽保持
    /// 原表示打 tag（对齐解释器 coerce_to_declared），Num 原样返回
    llvm::Value* to_num(const CGValue& v);

    /// @brief 动态承载组装（t146）：标量/Num/Dyn → Dyn {tag,bits}（tag 复用
    /// arr_kind_of 编码，Obj 按 tag 5、Arr 按 tag 4 存 ptr 位模式，同 t144/t160
    /// kind7）；Tup/TupDyn 按 tag 8 存 t140 堆对象 ptr 位模式（t170，静态 tuple
    /// 先物化）；Fn 按 tag 9 存函数 ptr 位模式（t171）；其余类型（Void 等）经
    /// arr_kind_of/elem_to_bits 的拒编守卫兜底
    llvm::Value* to_dyn(const CGValue& v, size_t line = 0, size_t column = 0);

    /// @brief toNumber 降级（t63，对齐解释器 to_number_value）：Bool → 0/1
    /// 整数表示、Str 解析下沉 collie_rt（失败返 NaN）、Int/Double/Num 复用
    /// to_num、Dyn 下沉 rt_dyn_to_number 按运行期 tag 分派（t155）；
    /// 其余参数拒编（解释器此处为运行期报错）
    llvm::Value* to_number_num(const CGValue& v, size_t line, size_t column);

    /// @brief 动态承载值（Dyn）的 toNumber 降级（t155）：下沉 rt_dyn_to_number
    /// 按运行期 tag 分派，结果 Num {tag,bits}；method_form = true 为 `d.toNumber()`
    /// 方法形式（实例位消息走解释器的方法查找失败分支），false 为内建形式
    llvm::Value* dyn_to_number(const CGValue& v, bool method_form);

    /// @brief tribool 三态判定方法降级（t65，对齐解释器 call_tribool_method）：
    /// isTrue/isFalse/isUnset 与对应三态常量 icmp 出 Bool；接收者为 i8 三态编码，
    /// 静态 tribool 与动态承载值 tag 6 同编码故两路共用（t157）
    void gen_tribool_method(llvm::Value* tri, const std::string& name);

    /// @brief number 专属方法降级（t67，对齐解释器 call_number_method）：
    /// abs/integerPart/decimalPart → 接收者同型数值，is* 谓词 → Bool；
    /// Int 纯 IR（abs 走 checked_int_arith 陷阱）、Double 走 fabs/trunc/floor
    /// intrinsic + fcmp、Num tag 分支两路 PHI（整数态保持整数态）
    void gen_number_method(const CGValue& object, const std::string& name,
                           size_t line, size_t column);

    /// @brief subString 下标参数降级（t105 引入，t154 提取为成员供静态串接收者
    /// 与动态承载值接收者两路共用）：Int 直传，Double/Num 按解释器语义做
    /// NaN / 精确 -1.0 特判 + floor + clamp 后转 i64
    llvm::Value* sub_string_index(const CGValue& v, bool is_end,
                                  size_t line, size_t column);

    /// @brief number 算术运行时调用（t62）：op 编码见 collie_rt_num_arith
    /// （0=+ 1=- 2=* 3=/ 4=% 5=一元负号）；结果经 entry alloca 出参写回
    llvm::Value* call_num_arith(int op, llvm::Value* a, llvm::Value* b);

    /// @brief i64 带溢出检查的加/减/乘（CG1 t58）：s*.with.overflow intrinsic，
    /// 溢出分支调 collie_rt 陷阱报错退出，插入点落在继续块后返回结果值
    llvm::Value* checked_int_arith(llvm::Intrinsic::ID id, llvm::Value* lhs,
                                   llvm::Value* rhs, const llvm::Twine& name);

    /// @brief byte/word 赋值点范围检查（t69）：无符号比较 (u64)v > max 一次
    /// 覆盖负数与超上限，越界分支调 collie_rt 陷阱（对齐解释器
    /// coerce_to_declared 的 "Value out of range"）；返回原值
    llvm::Value* check_bit_range(llvm::Value* v, long long max_val,
                                 const char* type_name);

    /// @brief number → integer 窄化检查（t111）：tag 1（小数态）分支调
    /// collie_rt 陷阱（对齐解释器 coerce_to_declared "cannot assign decimal
    /// value to 'integer' variable"）；整数态返 bits 即 i64 值
    llvm::Value* num_to_int_checked(llvm::Value* num);

    /// @brief 索引值归一化为 i64（t122）：Int 透传；Double/Num 镜像解释器
    /// normalize_index——转 double 视图，非整数态（含 NaN）调 collie_rt 陷阱
    /// "Index must be an integer"，整数态 floor 后 clamp ±4e18 防 FPToSI poison
    /// （±Infinity 随后落 rt 越界陷阱）再 FPToSI；负索引回绕/越界由 rt 侧统一处理。
    /// 其余类型（Bool/Str 等）拒编不错编
    llvm::Value* index_to_int(const CGValue& index, size_t line, size_t col);
    /// double 视图下标降为 i64（t122 逻辑，t150 抽为 helper 供 Num/Double 与
    /// Dyn 小数态共用）：非整数态落 rt_trap_index_integer_ 后 clamp+FPToSI
    llvm::Value* index_from_double(llvm::Value* d);

    /// @brief 把任意标量值转为字符串 ptr（S7 t54：拼接/toString 用，对齐 Value::to_string）
    llvm::Value* to_str(const CGValue& v, const Token& where);

    /// @brief 数组元素值 → 8 字节槽位模式 i64（t59）：Int 直存/Double bitcast/
    /// Bool zext/Str ptrtoint（collie_rt 数组对象槽统一为 i64）
    llvm::Value* elem_to_bits(const CGValue& v);

    /// @brief 异质 tuple 动态访问的 Dyn 物化（t138）：tags+bits 双 int 数组，
    /// tag 复用数组 kind 编码（0=int/1=dec/2=bool/3=str/6=tribool，Num/Dyn 元素取
    /// 运行期分量，Arr/Obj 走 elem_to_bits 的 4/5 位模式，Fn/Tup/TupDyn 经 to_dyn
    /// 取 9/8 位模式，t172）；返回 {tags 数组, bits 数组}，调用侧同一索引/键两次取回拼 Dyn
    std::pair<llvm::Value*, llvm::Value*> materialize_dyn_scalars(const CGTuple& t,
                                                                  size_t line,
                                                                  size_t column);

    /// @brief 8 字节槽位模式 i64 → 数组元素值（t59，elem_to_bits 的逆变换）
    llvm::Value* bits_to_elem(llvm::Value* bits, CGType elem);

    /// @brief 同质 Arr/Obj 元素 tuple 动态访问结果元数据回填（t114）：单数组
    /// 物化读回的 bits 经 bits_to_elem 还原后，结果 CGType 恒为 elem，但 Arr
    /// 需回填统一内层 elem、Obj 需回填统一 cls 供下游索引/成员访问。内层 elem
    /// 不一致降 Num 动态域哨兵（t94 同规则）；类不一致取最近公共祖先（t93/t100
    /// 同规则），无公共祖先拒编不错编
    CGValue tuple_dynamic_result(llvm::Value* bits, CGType elem, const CGTuple& t,
                                 size_t line, size_t column);

    /// @brief 同质实例位 tuple 的统一静态 cls（t114）：逐元素取最近公共祖先，
    /// 跨树无共同祖先返回空串——调用侧据此把非常量索引/取键回退 Dyn 承载路径
    /// （t172，读出 tag 5 后字段/方法按对象头类 id 运行期分派，对齐解释器不校验类）
    std::string tuple_obj_merged_cls(const CGTuple& t);

    /// @brief 静态展开 tuple → 动态 tuple 堆对象 ptr（t140）：names 建 kind 3 数组
    /// （无名槽存空串）、values 建 kind 7 动态数组（tag/bits 编码同 to_dyn 单点），
    /// 交 collie_rt_tuple_new 打包。元素含非 dyn_carrier（如 Void）即拒编不错编
    llvm::Value* materialize_tuple_dynamic(int tup_idx, size_t line, size_t column);

    /// @brief 各合流支 tuple 能否走静态 PHI 合流（t140 同形 + t164 叶位可合流）：
    /// 元素数+名字表逐层一致、嵌套位全支同为 tuple，且各叶位类型按 merge_tuple_arms
    /// 规则可合并——真则静态展开（t95），假则回退动态 tuple 合流
    bool tuple_arms_static_mergeable(const std::vector<int>& tups);

    /// @brief 元素 CGType → collie_rt 数组 kind 编码（0=Int/1=Double/2=Bool/3=Str，t59）
    int arr_kind_of(CGType elem);
    /// @brief 异质动态标量数组（kind 7，t139）可承载元素判定：
    /// ∈ {Int/Double/Num/Bool/Str/Tri/Dyn}；Arr/Obj/Tup 不入（各有专属路径/维持拒编），
    /// 数组字面量与动态承载槽处另放的 Obj/Arr 位见 `is_dyn_carrier`（t160）
    static bool is_dyn_scalar(CGType t);
    /// @brief kind 7 动态承载全集判定（t160）：is_dyn_scalar ∪ {Obj, Arr}——
    /// 数组字面量混排与动态承载槽另放行的实例位（tag 5）/嵌套数组位（tag 4）；
    /// tuple 物化（t140）不用本判定，含数组/实例元素的 tuple 维持拒编
    static bool is_dyn_carrier(CGType t);
    /// @brief AST 层常量整数解析（t68 tuple 索引用）：整数字面量与一元负号包
    /// 字面量（-1 的 AST 为 UnaryExpr('-')+LiteralExpr，emit 会走溢出检查出非常量）
    bool const_int_of(const Expr* e, long long& out);

    /// @brief tuple 展开值登记（t68）：入 tuple_values_ 返回下标
    int register_tuple(CGTuple t);

    /// @brief tuple 变量解构槽创建（t68）：逐元素 entry alloca + store 初始值，
    /// 嵌套 tuple 元素递归建子槽组；返回 tuple_vars_ 下标（参数按值防递归引用失效）
    int create_tuple_var(CGTuple t, const std::string& name);

    /// @brief tuple 变量读取（t68）：逐槽 load 重组展开值（嵌套递归），
    /// 返回指向新登记 tuple_values_ 条目的虚值
    CGValue load_tuple_var(int var_idx, const std::string& name);

    /// @brief tuple 变量同形状重赋值（t68）：元素数/名字表一致才逐槽写
    ///（标量元素经 coerce_for_slot 加宽、Arr/Obj 同 elem/cls、嵌套递归）；
    /// 返回存入后展开值的 tuple_values_ 下标（赋值表达式的值）
    int store_tuple_var(int var_idx, const CGValue& v, const Token& where);

    /// @brief tuple 转串（t68）：静态展开拼接 "(1, 2)" / "(name: v)" 格式
    ///（对齐 Value::to_string Tuple 分支；常量段编译期合并后 rt_concat 链）
    llvm::Value* tuple_to_str(const CGValue& v, const Token& where);

    /// @brief 不支持的构造统一报错出口
    [[noreturn]] void unsupported(const std::string& what, size_t line, size_t column);

    llvm::LLVMContext context_;
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;
    /// collie_rt 垫片打印接口（S6 t53）：print 逐参调用，输出对齐解释器
    llvm::FunctionCallee rt_print_str_;   // void(ptr)
    llvm::FunctionCallee rt_print_i64_;   // void(i64)
    llvm::FunctionCallee rt_print_f64_;   // void(double)
    llvm::FunctionCallee rt_print_bool_;  // void(i32)
    llvm::FunctionCallee rt_print_sep_;   // void()
    llvm::FunctionCallee rt_print_newline_; // void()
    /// collie_rt 字符串运行时（S7 t54）：拼接与标量转串（malloc 串不 free，缺口 CG6）
    llvm::FunctionCallee rt_concat_;       // ptr(ptr, ptr)
    llvm::FunctionCallee rt_i64_to_str_;   // ptr(i64)
    llvm::FunctionCallee rt_f64_to_str_;   // ptr(double)
    llvm::FunctionCallee rt_bool_to_str_;  // ptr(i32)，返静态串
    /// collie_rt 字符串比较（S7 t55）：strcmp 语义，六种比较共用
    llvm::FunctionCallee rt_strcmp_;       // i32(ptr, ptr)
    /// collie_rt 字符串 length/索引（S8 t56）：UTF-8 码点，对齐解释器
    llvm::FunctionCallee rt_str_len_;      // i64(ptr)
    llvm::FunctionCallee rt_str_index_;    // ptr(ptr, i64)，越界运行期报错退出
    /// collie_rt 字符串方法（S10 t57）：trim 系列与 subString 码点区间
    llvm::FunctionCallee rt_str_trim_;      // ptr(ptr, i32 mode)，mode 0=两端/1=左/2=右
    llvm::FunctionCallee rt_str_substring_; // ptr(ptr, i64, i64)，end==-1 取 length
    /// collie_rt 整数溢出陷阱（CG1 t58）：i64 算术溢出时报错退出，不静默回绕
    llvm::FunctionCallee rt_trap_int_overflow_; // void()
    /// collie_rt byte/word 范围与移位量陷阱（t69）：越界报错退出
    llvm::FunctionCallee rt_trap_bit_range_;    // void(ptr name, i64 max, i64 got)
    llvm::FunctionCallee rt_trap_shift_count_;  // void()
    /// collie_rt number 窄化陷阱（t111）：Num 小数态窄化 integer 报错退出
    llvm::FunctionCallee rt_trap_num_narrow_;   // void()
    /// collie_rt 非整数索引陷阱（t122）：number/decimal 下标非整数态报错退出
    llvm::FunctionCallee rt_trap_index_integer_; // void()
    /// collie_rt 非数值位动态承载值作下标的类型陷阱（t150）：消息对齐解释器
    /// normalize_index "Index must be a number, got <kind>"
    llvm::FunctionCallee rt_trap_index_kind_;    // void(i64 tag)
    /// collie_rt 未定义方法/属性陷阱（t102，S39 残余面）：静态类无此成员但
    /// 后代类有，upcast 分派 default 臂（动态类即静态类本身）报错退出，
    /// 消息对齐解释器 "Undefined method/property 'X' on object"
    llvm::FunctionCallee rt_trap_undefined_method_;    // void(ptr name)
    llvm::FunctionCallee rt_trap_undefined_property_;  // void(ptr name)
    /// collie_rt 动态承载值进类型化槽的类型陷阱（t147）：Dyn 值 tag 不在目标
    /// 槽接受表内时报错退出，消息对齐解释器 "Type mismatch: cannot assign
    /// <kind> to '<type>' variable"
    llvm::FunctionCallee rt_trap_dyn_convert_;  // void(i64 tag, ptr type_name)
    /// collie_rt 动态承载值参与算术的类型陷阱（t148）：Dyn 侧 tag 非数值态
    /// （0/1 以外）时报错退出，消息对齐解释器
    /// "Arithmetic operands must be numbers"
    llvm::FunctionCallee rt_trap_non_numeric_arith_; // void()
    /// collie_rt 动态承载值长度面（t152）：Dyn 上的 `.length`（mode 0）与
    /// len()（mode 1）按运行期 tag 分派取长度，非串/数组位落解释器同消息陷阱
    llvm::FunctionCallee rt_dyn_len_;  // i64(i64 tag, i64 bits, i64 mode)
    /// collie_rt 动态承载值索引读（t153）：`dyn[i]` 按运行期 tag 分派
    /// （3=串 UTF-8 码点索引、4/7=数组按元素 kind 读、其余落解释器同消息陷阱），
    /// 经出参写回 {tag,bits}
    llvm::FunctionCallee rt_dyn_index_;  // void(i64 tag, i64 bits, i64 idx, ptr otag, ptr obits)
    /// collie_rt 动态承载值索引写（t153）：`dyn[i] = v` 按运行期 tag 分派
    /// （4/7=数组写复用 arr_set_num 的 kind 对齐与 CG7 陷阱、3=串不可变陷阱、
    /// 其余=Only arrays can be index-assigned 陷阱）
    llvm::FunctionCallee rt_dyn_set_;  // void(i64 tag, i64 bits, i64 idx, i64 vtag, i64 vbits)
    /// collie_rt 动态承载值串方法（t154）：`dyn.trim()` / `dyn.subString(...)`
    /// 按运行期 tag 分派（3=串复用既有垫片，其余落解释器同消息陷阱）
    llvm::FunctionCallee rt_dyn_str_trim_;      // ptr(i64 tag, i64 bits, i64 mode, ptr name)
    llvm::FunctionCallee rt_dyn_str_substring_; // ptr(i64 tag, i64 bits, i64 start, i64 end, ptr name)
    /// collie_rt 动态承载值 toNumber（t155）：按运行期 tag 复刻解释器
    /// to_number_value 接受表（0/1 透传、2→0/1、3 串解析、其余报错退出），
    /// 结果经出参写回 {tag,bits}；mode 1 = 方法形式（实例位消息不同）
    llvm::FunctionCallee rt_dyn_to_number_; // void(i64 tag, i64 bits, ptr otag, ptr obits, i64 mode)
    /// collie_rt 动态承载值 number 专属方法接收者守卫（t156）：解释器进 number
    /// 方法段先判 is_number()，放行 tag 0/1（与 Num 表示重合），其余位复刻两类
    /// 消息（实例位 Undefined method / 其余 only supported on numbers）后退出
    llvm::FunctionCallee rt_dyn_num_guard_;   // void(i64 tag, ptr name)
    /// collie_rt 动态承载值 tribool 方法接收者守卫（t157）：解释器进 tribool 方法
    /// 段先判 is_tribool()，仅放行 tag 6（bits 即 i8 三态编码零扩展），其余位
    /// （含 bool 位）复刻两类消息后退出
    llvm::FunctionCallee rt_dyn_tri_guard_;   // void(i64 tag, ptr name)
    /// collie_rt 动态承载值判等（t149）：按 tag 复刻解释器 values_equal 接受表
    /// （tribool×bool 加宽、Number 跨 tag double 视图、串内容、数组深比较、
    /// 实例位恒不等、kind 失配恒 false），返 0/1，无陷阱臂
    llvm::FunctionCallee rt_dyn_eq_;     // i32(i64 atag, i64 abits, i64 btag, i64 bbits)
    /// collie_rt 动态承载值关系比较（t149）：双侧数值态走 num_cmp、同串走
    /// strcmp，其余落 "Comparison operands must be both numbers or both
    /// strings" 陷阱退出，返 0/1
    llvm::FunctionCallee rt_dyn_rel_;    // i32(i64 op, i64 atag, i64 abits, i64 btag, i64 bbits)
    /// collie_rt 数组运行时（t59）：不透明 ptr 数组对象，8 字节槽存位模式（引用语义）
    llvm::FunctionCallee rt_arr_new_;    // ptr(i64 len, i64 kind)
    llvm::FunctionCallee rt_arr_get_;    // i64(ptr, i64)，负索引/越界处理在运行期
    llvm::FunctionCallee rt_arr_set_;    // void(ptr, i64, i64 bits)，同上索引规则
    llvm::FunctionCallee rt_arr_len_;    // i64(ptr)
    llvm::FunctionCallee rt_arr_to_str_; // ptr(ptr)，[1, 2, 3] 格式对齐 Value::to_string
    llvm::FunctionCallee rt_arr_kind_;   // i64(ptr)，动态域索引读拼 number 用（t70）
    llvm::FunctionCallee rt_arr_set_num_; // void(ptr, i64, i64 tag, i64 bits)，动态域索引写（t70，含 CG7 陷阱）
    llvm::FunctionCallee rt_arr_set_dyn_; // void(ptr, i64 i, i64 tag, i64 bits)，kind 7 异质槽写（t139）
    llvm::FunctionCallee rt_arr_get_dyn_; // void(ptr, i64 i, ptr otag, ptr obits)，kind 7 异质槽读（t139）
    llvm::FunctionCallee rt_arr_eq_;     // i64(ptr, ptr)，数组深比较返 1/0（t79）
    llvm::FunctionCallee rt_tuple_get_;  // i64(ptr names, ptr vals, ptr key)，tuple 动态键 get（t84）
    /// collie_rt 动态 tuple 运行时对象（t140）：三元不同形状合流/进函数签名
    llvm::FunctionCallee rt_tuple_new_;    // ptr(i64 len, ptr names, ptr values)
    llvm::FunctionCallee rt_tuple_len_;    // i64(ptr t)
    llvm::FunctionCallee rt_tuple_get_dyn_; // void(ptr t, i64 i, ptr otag, ptr obits)
    llvm::FunctionCallee rt_tuple_key_dyn_; // void(ptr t, ptr key, ptr otag, ptr obits)
    // 动态承载值的 get(key)（t170）：void(i64 tag, i64 bits, ptr key, ptr otag, ptr obits)
    llvm::FunctionCallee rt_dyn_tuple_get_; // 动态承载值的 get(key)（t170） // void(ptr t, ptr key, ptr otag, ptr obits)
    llvm::FunctionCallee rt_tuple_eq_;     // i64(ptr, ptr)，tuple 深比较返 1/0
    llvm::FunctionCallee rt_tuple_to_str_; // ptr(ptr t)，malloc 新串
    /// collie_rt 类实例分配（t60）：字段块 malloc，布局读写全在 codegen 侧
    llvm::FunctionCallee rt_obj_new_;    // ptr(i64 size)
    /// collie_rt number 运行时（t62，CG5 收窄）：tagged 双表示，语义单点对齐解释器
    llvm::FunctionCallee rt_num_arith_;  // void(i64 op, i64, i64, i64, i64, ptr otag, ptr obits)
    llvm::FunctionCallee rt_num_cmp_;    // i32(i64 op, i64, i64, i64, i64)，返 0/1
    llvm::FunctionCallee rt_num_to_str_; // ptr(i64 tag, i64 bits)，malloc 新串
    llvm::FunctionCallee rt_print_num_;  // void(i64 tag, i64 bits)，格式对齐 to_string
    /// collie_rt toNumber 字符串解析（t63）：复刻解释器 to_number_value，失败返 NaN
    llvm::FunctionCallee rt_str_to_num_; // void(ptr s, ptr otag, ptr obits)
    CGValue last_value_;
    /// 作用域栈：块进出 push/pop，支持遮蔽（与解释器 Environment 对齐）
    std::vector<std::unordered_map<std::string, CGVar>> scopes_;
    /// tuple 静态展开注册表（t68）：CGValue/CGVar 以 tup 下标引用（只增不删）
    std::vector<CGTuple> tuple_values_;
    std::vector<CGTupleVar> tuple_vars_;
    /// tuple 换形状重建序号（t117）：create_tuple_var 顶层槽组名 "name.N" 升
    /// 全局后重建会同名符号冲突（verifyModule 失败），重建槽组名追加序号
    /// 后缀唯一化（alloca 重名由 LLVM 自动改号，全局不自动）；旧槽组成
    /// 死存储不影响运行期输出
    int tuple_rebuild_seq_ = 0;
    /// 循环上下文栈：break/continue 查最内层跳转目标（S4 t51）
    std::vector<LoopContext> loops_;
    /// 顶层函数表（S5 t52）：名字 → 原型；第一遍填充，递归/前向调用天然可用
    std::unordered_map<std::string, CGFunction> functions_;
    /// 嵌套函数注册表（t91）：声明节点 → functions_ 改编键（outer.inner）；
    /// 第一遍递归下探填充，visitFunction 据此区分嵌套/顶层路径
    std::unordered_map<const FunctionStmt*, std::string> nested_fns_;
    /// 同名函数「最后定义胜出」表（t131）：基名 → 胜出函数 AST 节点；对齐解释器
    /// env_.define 覆盖语义——同名后定义遮蔽前定义，调用恒取最后定义者
    std::unordered_map<std::string, const FunctionStmt*> winning_stmt_;
    /// 函数声明节点 → functions_ 基名键（t131）：visitFunction 据此定位原型；
    /// 被遮蔽节点据 winning_stmt_ 跳过体生成（前定义从不调用）
    std::unordered_map<const FunctionStmt*, std::string> fn_stmt_to_key_;
    /// 类表（t60）：类名 → struct 布局 + 方法原型；第一遍注册，前向 new 可用
    std::unordered_map<std::string, CGClass> classes_;
    /// 需统一为动态承载 {tag,bits} 的槽（t184/S131 + t185/S132）：键为
    /// "继承树根.字段名"，注册遍收集承载不一致遮蔽、resolve_dyn_slots 收集可观察
    /// none 的无初值字段，消费后把该树内全部持有者的槽升级为 Dyn
    std::set<std::string> dyn_slots_;
    /// 当前方法的 this 实参与所属类名（t60）：仅方法体生成期非空
    llvm::Value* current_this_ = nullptr;
    std::string current_class_name_;
    /// 当前方法的定义类（t61）：base 按定义类的父类解析（继承方法的副本
    /// 内 defining ≠ 分派类，与解释器 call_class_method 的 defining_class 同义）
    std::string current_defining_class_;
    /// 当前是否在生成函数体（顶层 return / 嵌套函数拒编用）
    bool in_function_ = false;
    /// 当前正在生成体的函数（t128）：函数/方法语境标记（顶层 return / 嵌套函数拒编用）；
    /// 顶层（@main）为 nullptr
    const FunctionStmt* current_fn_ = nullptr;
    /// 作用域栈（t130）：生成期维护的声明作用域链，栈底→栈顶对应外层→内层；
    /// 进入函数/方法体、if/while/for/do-while 各分支、switch 各 case 体时压入对应
    /// Stmt*（FunctionStmt 或 BlockStmt），离开弹出。CGClass::owner_scope 必在栈中
    /// （含嵌套）该类方可实例化——对齐解释器"块级作用域执行到才登记"
    std::vector<const Stmt*> scope_stack_;
    /// 已生成的函数/方法体计数（t106）：函数体链底按值快照声明在先的
    /// 全局槽静态 elem，全局数组槽异型互赋降级不回溯已生成代码——
    /// 声明后计数增长即拒编（CGVar.fn_gen_at 对比）
    size_t fn_gen_count_ = 0;
    /// 变量单元 id 自增分配器（t141）：每个形参/局部建槽时取号，供闭包
    /// 按引用捕获的身份一致性校验（见 CGVar::cell_id）；从 1 起，0 留作未分配
    int next_cell_id_ = 1;
    /// 条件发射区深度（t117）：uninit_snapshot 各调用点配 CondDepthGuard RAII
    /// 自增自减（C++ 栈帧作用域 == 区内发射作用域，区末无用户语句发射）——
    /// tuple 换形状重赋在区内的形状运行期才定，区外读按生成时形状解码
    /// 错值，区内换形状拒编不错编；嵌套区经调用栈自然累加
    int cond_depth_ = 0;
    /// 当前函数的返回类型（visitReturn 校验/提升用；Void 即 none）
    CGType current_ret_type_ = CGType::Void;
    /// 返回类型为 Obj 时的类名（t61）：visitReturn 严格同类校验用
    std::string current_ret_cls_;
    /// 返回类型为 byte/word 时的范围上限 255/65535（t97）：visitReturn 插
    /// check_bit_range 陷阱用；0 即非位类型返回
    long long current_ret_bit_max_ = 0;
};

} // namespace collie
