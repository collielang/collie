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
        Bool,    // i1
        Tri,     // i8（tribool 三态编码 False=0 < Unset=1 < True=2，Kleene min/max，t65）
        Str,     // ptr → 常量字符串
        Arr,     // ptr → collie_rt 数组对象（t59，元素类型另记于 elem 字段）
        Obj,     // ptr → 类实例字段块（t60，类名另记于 cls 字段）
        Tup,     // 虚值：无 LLVM 承载（t68 静态展开，元素另记于 tuple_values_，value 恒 nullptr）
        Void,    // 无值（none 返回函数的调用结果，S5 t52）
    };

    struct CGValue {
        llvm::Value* value = nullptr;
        CGType type = CGType::Int;
        CGType elem = CGType::Int; // 仅 type == Arr 时有意义：元素类型（t59）
        std::string cls;           // 仅 type == Obj 时有意义：类名（t60）
        int tup = -1;              // 仅 type == Tup 时有意义：tuple_values_ 下标（t68）
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
        bool uninit = false;       // 无初始化声明（t92）：读拒编，同块赋值后清
        size_t decl_depth = 0;     // 仅 uninit 有意义：声明时 scopes_.size()（t92）
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
    llvm::Value* coerce_for_slot(const CGValue& v, CGType slot_type, const Token& where,
                                 const std::string& slot_cls = "");

    /// @brief 由内向外逐层查找变量；未找到返回 nullptr（语义层已保证先声明，防御用）
    CGVar* lookup_var(const std::string& name);

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

    /// @brief 顶层函数建原型（第一遍，S5 t52）：同名重载拒编；none 返回降 void；
    /// prefix 非空即嵌套函数（t91）：注册键/符号名改编为 prefix.name（用户
    /// 标识符无 '.'，与顶层名天然不冲突），并递归下探函数体登记更深嵌套
    void declare_function(const FunctionStmt& stmt, const std::string& prefix = "");

    /// @brief 递归下探语句子树登记嵌套函数原型（t91，第一遍）：Block/If/
    /// While/For/DoWhile/Switch 各分支体全覆盖；命中 FunctionStmt 即以
    /// prefix 改编名 declare_function 并进 nested_fns_ 注册表
    void declare_nested_in(const Stmt* s, const std::string& prefix);

    /// @brief 类布局注册（第一遍阶段一，t60/t61）：父链字段 base-first 合并 +
    /// 自身追加建 struct；同名字段遮蔽/无初值字段/范围外字段类型拒编；
    /// 父类须先声明（合并依赖父类已注册）
    void register_class_layout(const ClassStmt& stmt);

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

    /// @brief Int/Double/Num → Num（t62）：integer/decimal→number 加宽保持
    /// 原表示打 tag（对齐解释器 coerce_to_declared），Num 原样返回
    llvm::Value* to_num(const CGValue& v);

    /// @brief toNumber 降级（t63，对齐解释器 to_number_value）：Bool → 0/1
    /// 整数表示、Str 解析下沉 collie_rt（失败返 NaN）、Int/Double/Num 复用
    /// to_num；其余参数拒编（解释器此处为运行期报错）
    llvm::Value* to_number_num(const CGValue& v, size_t line, size_t column);

    /// @brief number 专属方法降级（t67，对齐解释器 call_number_method）：
    /// abs/integerPart/decimalPart → 接收者同型数值，is* 谓词 → Bool；
    /// Int 纯 IR（abs 走 checked_int_arith 陷阱）、Double 走 fabs/trunc/floor
    /// intrinsic + fcmp、Num tag 分支两路 PHI（整数态保持整数态）
    void gen_number_method(const CGValue& object, const std::string& name,
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

    /// @brief 把任意标量值转为字符串 ptr（S7 t54：拼接/toString 用，对齐 Value::to_string）
    llvm::Value* to_str(const CGValue& v, const Token& where);

    /// @brief 数组元素值 → 8 字节槽位模式 i64（t59）：Int 直存/Double bitcast/
    /// Bool zext/Str ptrtoint（collie_rt 数组对象槽统一为 i64）
    llvm::Value* elem_to_bits(const CGValue& v);

    /// @brief 8 字节槽位模式 i64 → 数组元素值（t59，elem_to_bits 的逆变换）
    llvm::Value* bits_to_elem(llvm::Value* bits, CGType elem);

    /// @brief 元素 CGType → collie_rt 数组 kind 编码（0=Int/1=Double/2=Bool/3=Str，t59）
    int arr_kind_of(CGType elem);

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
    /// collie_rt 动态域元素 kind 陷阱（t88，缺口 CG9）：bool/str/嵌套数组经
    /// 透传后索引读出元素静态类型不可定，陷阱退出不错值
    llvm::FunctionCallee rt_trap_arr_kind_;     // void(i64 kind)
    /// collie_rt 数组运行时（t59）：不透明 ptr 数组对象，8 字节槽存位模式（引用语义）
    llvm::FunctionCallee rt_arr_new_;    // ptr(i64 len, i64 kind)
    llvm::FunctionCallee rt_arr_get_;    // i64(ptr, i64)，负索引/越界处理在运行期
    llvm::FunctionCallee rt_arr_set_;    // void(ptr, i64, i64 bits)，同上索引规则
    llvm::FunctionCallee rt_arr_len_;    // i64(ptr)
    llvm::FunctionCallee rt_arr_to_str_; // ptr(ptr)，[1, 2, 3] 格式对齐 Value::to_string
    llvm::FunctionCallee rt_arr_kind_;   // i64(ptr)，动态域索引读拼 number 用（t70）
    llvm::FunctionCallee rt_arr_set_num_; // void(ptr, i64, i64 tag, i64 bits)，动态域索引写（t70，含 CG7 陷阱）
    llvm::FunctionCallee rt_arr_eq_;     // i64(ptr, ptr)，数组深比较返 1/0（t79）
    llvm::FunctionCallee rt_tuple_get_;  // i64(ptr names, ptr vals, ptr key)，tuple 动态键 get（t84）
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
    /// 循环上下文栈：break/continue 查最内层跳转目标（S4 t51）
    std::vector<LoopContext> loops_;
    /// 顶层函数表（S5 t52）：名字 → 原型；第一遍填充，递归/前向调用天然可用
    std::unordered_map<std::string, CGFunction> functions_;
    /// 嵌套函数注册表（t91）：声明节点 → functions_ 改编键（outer.inner）；
    /// 第一遍递归下探填充，visitFunction 据此区分嵌套/顶层路径
    std::unordered_map<const FunctionStmt*, std::string> nested_fns_;
    /// 类表（t60）：类名 → struct 布局 + 方法原型；第一遍注册，前向 new 可用
    std::unordered_map<std::string, CGClass> classes_;
    /// 当前方法的 this 实参与所属类名（t60）：仅方法体生成期非空
    llvm::Value* current_this_ = nullptr;
    std::string current_class_name_;
    /// 当前方法的定义类（t61）：base 按定义类的父类解析（继承方法的副本
    /// 内 defining ≠ 分派类，与解释器 call_class_method 的 defining_class 同义）
    std::string current_defining_class_;
    /// 当前是否在生成函数体（顶层 return / 嵌套函数拒编用）
    bool in_function_ = false;
    /// 当前函数的返回类型（visitReturn 校验/提升用；Void 即 none）
    CGType current_ret_type_ = CGType::Void;
    /// 返回类型为 Obj 时的类名（t61）：visitReturn 严格同类校验用
    std::string current_ret_cls_;
};

} // namespace collie
