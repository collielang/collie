/**
 * @file code_generator.cpp
 * @brief AST → LLVM IR 代码生成器实现（M6 t49–t52，S1–S5 子集）
 *
 * 降级规则见 compiler/codegen/README.md 第四节；语义依据 compiler/SPEC.md。
 */
#include "code_generator.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <set>

#include <llvm/IR/CFG.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace collie {

namespace {

/// number 专属方法名集（t67 建，t156 起两路共用）：解释器 call_number_method
/// 段的十方法面，静态数值接收者与动态承载值接收者按同一判定分派
bool is_number_method_name(const std::string& name) {
    return name == "abs" || name == "integerPart" || name == "decimalPart" ||
           name == "isInteger" || name == "isDecimal" || name == "isNaN" ||
           name == "isInfinity" || name == "isFinite" ||
           name == "isPositive" || name == "isNegative";
}

/// tribool 三态判定方法名集（t65 建，t157 起两路共用）：解释器 tribool 方法段
/// 的三方法面，静态 tribool 接收者与动态承载值接收者按同一判定分派
bool is_tribool_method_name(const std::string& name) {
    return name == "isTrue" || name == "isFalse" || name == "isUnset";
}

/// 条件发射区深度 RAII（t117）：构造 +1 析构 -1（C++ 栈帧作用域即区内发射
/// 作用域——区末汇合/PHI 无用户语句发射，析构时机恒等于区退出）
struct CondDepthGuard {
    int& depth;
    explicit CondDepthGuard(int& d) : depth(d) { ++depth; }
    ~CondDepthGuard() { --depth; }
};

/// 块是否从函数 entry 可达（S5 t52）：return/break 后的 dead 块会被
/// 外层控制流补 br 挂到后续块上，单看前驱数会误判可达，须从 entry 遍历
 bool reachable_from_entry(llvm::BasicBlock* target) {
    llvm::Function* fn = target->getParent();
    std::vector<llvm::BasicBlock*> stack{&fn->getEntryBlock()};
    std::set<llvm::BasicBlock*> visited;
    while (!stack.empty()) {
        llvm::BasicBlock* bb = stack.back();
        stack.pop_back();
        if (!visited.insert(bb).second) continue;
        if (bb == target) return true;
        for (llvm::BasicBlock* succ : llvm::successors(bb)) {
            stack.push_back(succ);
        }
    }
    return false;
}

/// 表达式是否可观察 this（t109）：new 期间实例未逃逸（尚无任何别名），
/// 构造器前缀 RHS 仅 this/base 三类节点可触及本实例，其余递归子树
 bool expr_observes_this(const Expr* e) {
    if (e == nullptr) return false;
    if (dynamic_cast<const ThisExpr*>(e) != nullptr ||
        dynamic_cast<const BaseCallExpr*>(e) != nullptr ||
        dynamic_cast<const BaseMethodCallExpr*>(e) != nullptr) {
        return true;
    }
    if (const auto* b = dynamic_cast<const BinaryExpr*>(e)) {
        return expr_observes_this(b->left()) || expr_observes_this(b->right());
    }
    if (const auto* u = dynamic_cast<const UnaryExpr*>(e)) {
        return expr_observes_this(u->operand());
    }
    if (const auto* a = dynamic_cast<const AssignExpr*>(e)) {
        return expr_observes_this(a->value());
    }
    if (const auto* c = dynamic_cast<const CallExpr*>(e)) {
        if (expr_observes_this(c->callee())) return true;
        for (const auto& arg : c->arguments()) {
            if (expr_observes_this(arg.get())) return true;
        }
        return false;
    }
    if (const auto* tp = dynamic_cast<const TupleExpr*>(e)) {
        for (const auto& el : tp->elements()) {
            if (expr_observes_this(el.get())) return true;
        }
        return false;
    }
    if (const auto* t = dynamic_cast<const TernaryExpr*>(e)) {
        return expr_observes_this(t->condition()) ||
               expr_observes_this(t->then_expr()) ||
               expr_observes_this(t->else_expr()) ||
               expr_observes_this(t->unset_expr());
    }
    if (const auto* m = dynamic_cast<const MultiMatchExpr*>(e)) {
        if (expr_observes_this(m->target()) ||
            expr_observes_this(m->default_expr())) {
            return true;
        }
        for (const auto& br : m->branches()) {
            for (const auto& v : br.values) {
                if (expr_observes_this(v.get())) return true;
            }
            if (expr_observes_this(br.result.get())) return true;
        }
        return false;
    }
    if (const auto* arr = dynamic_cast<const ArrayLiteralExpr*>(e)) {
        for (const auto& el : arr->elements()) {
            if (expr_observes_this(el.get())) return true;
        }
        return false;
    }
    if (const auto* ix = dynamic_cast<const IndexExpr*>(e)) {
        return expr_observes_this(ix->object()) ||
               expr_observes_this(ix->index());
    }
    if (const auto* ia = dynamic_cast<const IndexAssignExpr*>(e)) {
        return expr_observes_this(ia->object()) ||
               expr_observes_this(ia->index()) ||
               expr_observes_this(ia->value());
    }
    if (const auto* mc = dynamic_cast<const MethodCallExpr*>(e)) {
        if (expr_observes_this(mc->object())) return true;
        for (const auto& arg : mc->arguments()) {
            if (expr_observes_this(arg.get())) return true;
        }
        return false;
    }
    if (const auto* p = dynamic_cast<const PropertyExpr*>(e)) {
        return expr_observes_this(p->object());
    }
    if (const auto* pa = dynamic_cast<const PropertyAssignExpr*>(e)) {
        return expr_observes_this(pa->object()) ||
               expr_observes_this(pa->value());
    }
    if (const auto* nw = dynamic_cast<const NewExpr*>(e)) {
        for (const auto& arg : nw->arguments()) {
            if (expr_observes_this(arg.get())) return true;
        }
        return false;
    }
    // Literal/Identifier 叶节点
    return false;
}

/// 构造器体定赋走查（t126）：从 pending（尚未定赋的 uninit 字段名）出发就地
/// 删减——直线 `this.f = <this-free>` 赋值删 f；裸块原地递归（必执行）；this-free
/// 条件且双臂齐备的 if：各臂在 pending 副本上递归后，仅"两臂皆删"的字段才视为
/// 定赋（保留仍存在于任一臂副本者）；不观察 this 的表达式语句跳过（无从触及
/// uninit 字段）。一旦出现观察 this 的非定赋语句、缺 else 的 if、或任何循环 /
/// switch / return / 声明，保守停止（pending 保留 → 上层拒编）——拒编不错编：
/// none 可能被观察或非定赋。
void ctor_definite_assign_stmts(
    const std::vector<std::unique_ptr<Stmt>>& stmts,
    std::vector<std::string>& pending);

void ctor_definite_assign_stmt(const Stmt* s, std::vector<std::string>& pending) {
    if (pending.empty()) return; // 全部定赋：后续无从暴露 none
    if (const auto* blk = dynamic_cast<const BlockStmt*>(s)) {
        ctor_definite_assign_stmts(blk->statements(), pending);
        return;
    }
    if (const auto* ifs = dynamic_cast<const IfStmt*>(s)) {
        // 条件观察 this → 可能在定赋前读 uninit 字段，保守停止
        if (expr_observes_this(ifs->condition())) return;
        const Stmt* els = ifs->else_branch();
        if (els == nullptr) return; // 缺 else：某路径 none 可观察，非定赋
        std::vector<std::string> then_pending = pending;
        std::vector<std::string> else_pending = pending;
        ctor_definite_assign_stmt(ifs->then_branch(), then_pending);
        ctor_definite_assign_stmt(els, else_pending);
        // 求交：仅两臂皆定赋的字段离开 pending（任一臂仍缺则保守保留）
        std::vector<std::string> keep;
        for (const auto& f : pending) {
            bool un_t = std::find(then_pending.begin(), then_pending.end(), f) !=
                        then_pending.end();
            bool un_e = std::find(else_pending.begin(), else_pending.end(), f) !=
                        else_pending.end();
            if (un_t || un_e) keep.push_back(f);
        }
        pending.swap(keep);
        return;
    }
    const auto* es = dynamic_cast<const ExpressionStmt*>(s);
    if (!es) return; // 循环 / switch / return / 声明等：保守停止
    const auto* pa = dynamic_cast<const PropertyAssignExpr*>(es->expression());
    if (pa && dynamic_cast<const ThisExpr*>(pa->object()) != nullptr &&
        !expr_observes_this(pa->value())) {
        const std::string assigned(pa->name().lexeme());
        pending.erase(std::remove(pending.begin(), pending.end(), assigned),
                      pending.end());
        return;
    }
    // 观察 this 的非定赋表达式语句：保守停止；否则（this-free）跳过安全
}

void ctor_definite_assign_stmts(
    const std::vector<std::unique_ptr<Stmt>>& stmts,
    std::vector<std::string>& pending) {
    for (const auto& s : stmts) {
        if (pending.empty()) return;
        ctor_definite_assign_stmt(s.get(), pending);
    }
}

// ---- 闭包自由变量收集（t141）----
// 递归遍历一个函数体子树，收集其中出现的所有标识符名（IdentifierExpr 读、
// AssignExpr 写目标），含更深层嵌套函数体内引用（供逐层按引用转发捕获），但
// 不下探 ClassStmt 成员（类方法非闭包，不捕获宿主局部）。收集到的是"候选名"，
// 是否真为捕获由调用侧按宿主作用域 + cell_id 一致性判定：被宿主遮蔽/命中全局/
// 函数绑定/元组的自然被过滤或成为无害死参。set 保证去重且有序 → 原型签名稳定。
void collect_free_idents_expr(const Expr* e, std::set<std::string>& out);
void collect_free_idents_stmt(const Stmt* s, std::set<std::string>& out);

void collect_free_idents_expr(const Expr* e, std::set<std::string>& out) {
    if (e == nullptr) return;
    if (const auto* id = dynamic_cast<const IdentifierExpr*>(e)) {
        out.insert(std::string(id->name().lexeme()));
        return;
    }
    if (const auto* lit = dynamic_cast<const LiteralExpr*>(e)) {
        (void)lit;
        return;
    }
    if (const auto* bin = dynamic_cast<const BinaryExpr*>(e)) {
        collect_free_idents_expr(bin->left(), out);
        collect_free_idents_expr(bin->right(), out);
        return;
    }
    if (const auto* un = dynamic_cast<const UnaryExpr*>(e)) {
        collect_free_idents_expr(un->operand(), out);
        return;
    }
    if (const auto* asg = dynamic_cast<const AssignExpr*>(e)) {
        out.insert(std::string(asg->name().lexeme()));
        collect_free_idents_expr(asg->value(), out);
        return;
    }
    if (const auto* ter = dynamic_cast<const TernaryExpr*>(e)) {
        collect_free_idents_expr(ter->condition(), out);
        collect_free_idents_expr(ter->then_expr(), out);
        collect_free_idents_expr(ter->else_expr(), out);
        collect_free_idents_expr(ter->unset_expr(), out);
        return;
    }
    if (const auto* mm = dynamic_cast<const MultiMatchExpr*>(e)) {
        collect_free_idents_expr(mm->target(), out);
        for (const auto& br : mm->branches()) {
            for (const auto& v : br.values) collect_free_idents_expr(v.get(), out);
            collect_free_idents_expr(br.result.get(), out);
        }
        collect_free_idents_expr(mm->default_expr(), out);
        return;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(e)) {
        collect_free_idents_expr(call->callee(), out);
        for (const auto& a : call->arguments()) collect_free_idents_expr(a.get(), out);
        return;
    }
    if (const auto* arr = dynamic_cast<const ArrayLiteralExpr*>(e)) {
        for (const auto& el : arr->elements()) collect_free_idents_expr(el.get(), out);
        return;
    }
    if (const auto* idx = dynamic_cast<const IndexExpr*>(e)) {
        collect_free_idents_expr(idx->object(), out);
        collect_free_idents_expr(idx->index(), out);
        return;
    }
    if (const auto* idxa = dynamic_cast<const IndexAssignExpr*>(e)) {
        collect_free_idents_expr(idxa->object(), out);
        collect_free_idents_expr(idxa->index(), out);
        collect_free_idents_expr(idxa->value(), out);
        return;
    }
    if (const auto* mc = dynamic_cast<const MethodCallExpr*>(e)) {
        collect_free_idents_expr(mc->object(), out);
        for (const auto& a : mc->arguments()) collect_free_idents_expr(a.get(), out);
        return;
    }
    if (const auto* prop = dynamic_cast<const PropertyExpr*>(e)) {
        collect_free_idents_expr(prop->object(), out);
        return;
    }
    if (const auto* pa = dynamic_cast<const PropertyAssignExpr*>(e)) {
        collect_free_idents_expr(pa->object(), out);
        collect_free_idents_expr(pa->value(), out);
        return;
    }
    if (const auto* nw = dynamic_cast<const NewExpr*>(e)) {
        for (const auto& a : nw->arguments()) collect_free_idents_expr(a.get(), out);
        return;
    }
    if (const auto* tp = dynamic_cast<const TupleExpr*>(e)) {
        for (const auto& el : tp->elements()) collect_free_idents_expr(el.get(), out);
        return;
    }
    if (const auto* bc = dynamic_cast<const BaseCallExpr*>(e)) {
        for (const auto& a : bc->arguments()) collect_free_idents_expr(a.get(), out);
        return;
    }
    if (const auto* bmc = dynamic_cast<const BaseMethodCallExpr*>(e)) {
        for (const auto& a : bmc->arguments()) collect_free_idents_expr(a.get(), out);
        return;
    }
    // ThisExpr / 其余无标识符子树
}

void collect_free_idents_stmt(const Stmt* s, std::set<std::string>& out) {
    if (s == nullptr) return;
    if (const auto* es = dynamic_cast<const ExpressionStmt*>(s)) {
        collect_free_idents_expr(es->expression(), out);
        return;
    }
    if (const auto* vd = dynamic_cast<const VarDeclStmt*>(s)) {
        // 声明名引入内层局部（遮蔽捕获），不入集合；仅初值表达式含引用
        collect_free_idents_expr(vd->initializer(), out);
        return;
    }
    if (const auto* blk = dynamic_cast<const BlockStmt*>(s)) {
        for (const auto& inner : blk->statements()) collect_free_idents_stmt(inner.get(), out);
        return;
    }
    if (const auto* ifs = dynamic_cast<const IfStmt*>(s)) {
        collect_free_idents_expr(ifs->condition(), out);
        collect_free_idents_stmt(ifs->then_branch(), out);
        collect_free_idents_stmt(ifs->else_branch(), out);
        return;
    }
    if (const auto* wh = dynamic_cast<const WhileStmt*>(s)) {
        collect_free_idents_expr(wh->condition(), out);
        collect_free_idents_stmt(wh->body(), out);
        return;
    }
    if (const auto* fo = dynamic_cast<const ForStmt*>(s)) {
        collect_free_idents_stmt(fo->initializer(), out);
        collect_free_idents_expr(fo->condition(), out);
        collect_free_idents_expr(fo->increment(), out);
        collect_free_idents_stmt(fo->body(), out);
        return;
    }
    if (const auto* dw = dynamic_cast<const DoWhileStmt*>(s)) {
        collect_free_idents_stmt(dw->body(), out);
        collect_free_idents_expr(dw->condition(), out);
        return;
    }
    if (const auto* sw = dynamic_cast<const SwitchStmt*>(s)) {
        collect_free_idents_expr(sw->condition(), out);
        for (const auto& sc : sw->cases()) {
            for (const auto& v : sc.values) collect_free_idents_expr(v.get(), out);
            collect_free_idents_stmt(sc.body.get(), out);
        }
        return;
    }
    if (const auto* ret = dynamic_cast<const ReturnStmt*>(s)) {
        collect_free_idents_expr(ret->value(), out);
        return;
    }
    if (const auto* fn = dynamic_cast<const FunctionStmt*>(s)) {
        // 更深层嵌套：其体内对被本函数（宿主）可见变量的引用须由本函数捕获转发
        for (const auto& inner : fn->body()->statements())
            collect_free_idents_stmt(inner.get(), out);
        return;
    }
    // ClassStmt / Break / Continue：类方法非闭包，不下探收集
}

} // namespace

CodeGenerator::CodeGenerator() : builder_(context_) {}

void CodeGenerator::generate(const std::vector<std::unique_ptr<Stmt>>& statements,
                             const std::string& module_name) {
    module_ = std::make_unique<llvm::Module>(module_name, context_);
    // 显式标记宿主 target triple，免得 clang 编 .ll 时报 override-module 警告
    module_->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));

    // collie_rt 垫片接口声明（S6 t53）：print 逐参调用，输出对齐解释器
    // to_string（整值小数按整数打/±Infinity/NaN 拼写）；链接时带上 collie_rt.lib
    llvm::Type* void_ty = builder_.getVoidTy();
    llvm::Type* ptr_ty = llvm::PointerType::getUnqual(context_);
    rt_print_str_ = module_->getOrInsertFunction(
        "collie_rt_print_str", llvm::FunctionType::get(void_ty, {ptr_ty}, false));
    rt_print_i64_ = module_->getOrInsertFunction(
        "collie_rt_print_i64", llvm::FunctionType::get(void_ty, {builder_.getInt64Ty()}, false));
    rt_print_f64_ = module_->getOrInsertFunction(
        "collie_rt_print_f64", llvm::FunctionType::get(void_ty, {builder_.getDoubleTy()}, false));
    rt_print_bool_ = module_->getOrInsertFunction(
        "collie_rt_print_bool", llvm::FunctionType::get(void_ty, {builder_.getInt32Ty()}, false));
    rt_print_sep_ = module_->getOrInsertFunction(
        "collie_rt_print_sep", llvm::FunctionType::get(void_ty, {}, false));
    rt_print_newline_ = module_->getOrInsertFunction(
        "collie_rt_print_newline", llvm::FunctionType::get(void_ty, {}, false));

    // collie_rt 字符串运行时声明（S7 t54）：拼接与标量转串（malloc 串不 free，缺口 CG6）
    rt_concat_ = module_->getOrInsertFunction(
        "collie_rt_concat", llvm::FunctionType::get(ptr_ty, {ptr_ty, ptr_ty}, false));
    rt_i64_to_str_ = module_->getOrInsertFunction(
        "collie_rt_i64_to_str", llvm::FunctionType::get(ptr_ty, {builder_.getInt64Ty()}, false));
    rt_f64_to_str_ = module_->getOrInsertFunction(
        "collie_rt_f64_to_str", llvm::FunctionType::get(ptr_ty, {builder_.getDoubleTy()}, false));
    rt_bool_to_str_ = module_->getOrInsertFunction(
        "collie_rt_bool_to_str", llvm::FunctionType::get(ptr_ty, {builder_.getInt32Ty()}, false));

    // collie_rt 字符串比较声明（S7 t55）：strcmp 语义，六种比较共用
    rt_strcmp_ = module_->getOrInsertFunction(
        "collie_rt_strcmp",
        llvm::FunctionType::get(builder_.getInt32Ty(), {ptr_ty, ptr_ty}, false));

    // collie_rt 字符串 length/索引声明（S8 t56）：UTF-8 码点，对齐解释器
    rt_str_len_ = module_->getOrInsertFunction(
        "collie_rt_str_len",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty}, false));
    rt_str_index_ = module_->getOrInsertFunction(
        "collie_rt_str_index",
        llvm::FunctionType::get(ptr_ty, {ptr_ty, builder_.getInt64Ty()}, false));

    // collie_rt 字符串方法声明（S10 t57）：trim 系列与 subString 码点区间
    rt_str_trim_ = module_->getOrInsertFunction(
        "collie_rt_str_trim",
        llvm::FunctionType::get(ptr_ty, {ptr_ty, builder_.getInt32Ty()}, false));
    rt_str_substring_ = module_->getOrInsertFunction(
        "collie_rt_str_substring",
        llvm::FunctionType::get(
            ptr_ty, {ptr_ty, builder_.getInt64Ty(), builder_.getInt64Ty()}, false));
    // collie_rt 整数溢出陷阱声明（CG1 t58）：i64 算术溢出时报错退出
    rt_trap_int_overflow_ = module_->getOrInsertFunction(
        "collie_rt_trap_int_overflow",
        llvm::FunctionType::get(builder_.getVoidTy(), false));
    // collie_rt byte/word 范围与移位量陷阱声明（t69）：越界报错退出
    rt_trap_bit_range_ = module_->getOrInsertFunction(
        "collie_rt_trap_bit_range",
        llvm::FunctionType::get(
            builder_.getVoidTy(),
            {ptr_ty, builder_.getInt64Ty(), builder_.getInt64Ty()}, false));
    rt_trap_shift_count_ = module_->getOrInsertFunction(
        "collie_rt_trap_shift_count",
        llvm::FunctionType::get(builder_.getVoidTy(), false));
    // collie_rt number 窄化陷阱声明（t111）：Num 小数态窄化 integer 报错退出
    rt_trap_num_narrow_ = module_->getOrInsertFunction(
        "collie_rt_trap_num_narrow",
        llvm::FunctionType::get(builder_.getVoidTy(), false));
    // collie_rt 非整数索引陷阱声明（t122）：number/decimal 下标非整数态报错退出
    rt_trap_index_integer_ = module_->getOrInsertFunction(
        "collie_rt_trap_index_integer",
        llvm::FunctionType::get(builder_.getVoidTy(), false));
    // collie_rt 非数值位动态下标陷阱声明（t150）：Dyn 承载值 tag≥2 作下标时
    // 报错退出，消息对齐解释器 "Index must be a number, got <kind>"
    rt_trap_index_kind_ = module_->getOrInsertFunction(
        "collie_rt_trap_index_kind",
        llvm::FunctionType::get(builder_.getVoidTy(), {builder_.getInt64Ty()}, false));
    // collie_rt 未定义方法/属性陷阱声明（t102，S39 残余面）：静态类无此成员
    // 但后代类有，upcast 分派 default 臂（动态类即静态类本身）报错退出，
    // 消息对齐解释器 "Undefined method/property 'X' on object"
    rt_trap_undefined_method_ = module_->getOrInsertFunction(
        "collie_rt_trap_undefined_method",
        llvm::FunctionType::get(builder_.getVoidTy(), {ptr_ty}, false));
    rt_trap_undefined_property_ = module_->getOrInsertFunction(
        "collie_rt_trap_undefined_property",
        llvm::FunctionType::get(builder_.getVoidTy(), {ptr_ty}, false));
    // collie_rt 动态承载值进类型化槽的类型陷阱声明（t147）：Dyn 值 tag 与槽
    // 类型不匹配时报错退出，消息对齐解释器 "Type mismatch: cannot assign
    // <kind> to '<type>' variable"
    rt_trap_dyn_convert_ = module_->getOrInsertFunction(
        "collie_rt_trap_dyn_convert",
        llvm::FunctionType::get(builder_.getVoidTy(),
                               {builder_.getInt64Ty(), ptr_ty}, false));
    // collie_rt 动态承载值长度面声明（t152）：Dyn 上的 .length / len() 按运行期
    // tag 分派（3=串码点、4/7=数组元素数、其余按 mode 落解释器同消息陷阱），
    // mode 0 = .length 属性、1 = len() 内建
    rt_dyn_len_ = module_->getOrInsertFunction(
        "collie_rt_dyn_len",
        llvm::FunctionType::get(builder_.getInt64Ty(),
                               {builder_.getInt64Ty(), builder_.getInt64Ty(),
                                builder_.getInt64Ty()},
                               false));
    // collie_rt 动态承载值索引读写声明（t153）：`dyn[i]` / `dyn[i] = v` 按运行期
    // tag 分派（3=串码点索引返单码点子串、4/7=数组读写、其余落解释器同消息陷阱），
    // 读经出参写回 {tag,bits}
    rt_dyn_index_ = module_->getOrInsertFunction(
        "collie_rt_dyn_index",
        llvm::FunctionType::get(
            builder_.getVoidTy(),
            {builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getInt64Ty(),
             ptr_ty, ptr_ty},
            false));
    rt_dyn_set_ = module_->getOrInsertFunction(
        "collie_rt_dyn_set",
        llvm::FunctionType::get(
            builder_.getVoidTy(),
            {builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getInt64Ty(),
             builder_.getInt64Ty(), builder_.getInt64Ty()},
            false));
    // collie_rt 动态承载值串方法声明（t154）：`dyn.trim()` / `dyn.subString(...)`
    // 按运行期 tag 分派（3=串复用既有垫片），其余位落解释器同消息陷阱
    // "Method 'x()' is only supported on strings, got <kind>"
    rt_dyn_str_trim_ = module_->getOrInsertFunction(
        "collie_rt_dyn_str_trim",
        llvm::FunctionType::get(
            ptr_ty,
            {builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getInt64Ty(),
             ptr_ty},
            false));
    rt_dyn_str_substring_ = module_->getOrInsertFunction(
        "collie_rt_dyn_str_substring",
        llvm::FunctionType::get(
            ptr_ty,
            {builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getInt64Ty(),
             builder_.getInt64Ty(), ptr_ty},
            false));
    // collie_rt 动态承载值 toNumber 声明（t155）：按运行期 tag 复刻解释器
    // to_number_value 接受表，结果经出参写回 {tag,bits}；mode 1 = 方法形式
    //（实例位报 Undefined method），mode 0 = 内建形式（实例位报 cannot convert object）
    rt_dyn_to_number_ = module_->getOrInsertFunction(
        "collie_rt_dyn_to_number",
        llvm::FunctionType::get(
            builder_.getVoidTy(),
            {builder_.getInt64Ty(), builder_.getInt64Ty(), ptr_ty, ptr_ty,
             builder_.getInt64Ty()},
            false));
    // collie_rt 动态承载值 number 专属方法接收者守卫（t156）：解释器进 number
    // 方法段先判 is_number()，放行 tag 0/1，其余位按解释器两类消息陷阱退出
    rt_dyn_num_guard_ = module_->getOrInsertFunction(
        "collie_rt_dyn_num_guard",
        llvm::FunctionType::get(
            builder_.getVoidTy(), {builder_.getInt64Ty(), ptr_ty}, false));
    // collie_rt 动态承载值 tribool 方法接收者守卫（t157）：解释器进 tribool 方法
    // 段先判 is_tribool()，放行 tag 6（含 bool 位在内均报错），其余位按解释器
    // 两类消息陷阱退出
    rt_dyn_tri_guard_ = module_->getOrInsertFunction(
        "collie_rt_dyn_tri_guard",
        llvm::FunctionType::get(
            builder_.getVoidTy(), {builder_.getInt64Ty(), ptr_ty}, false));
    // collie_rt 动态承载值参与算术的类型陷阱声明（t148）：Dyn 侧 tag 非数值
    // 态（0/1 以外）时报错退出，消息对齐解释器
    // "Arithmetic operands must be numbers"
    rt_trap_non_numeric_arith_ = module_->getOrInsertFunction(
        "collie_rt_trap_non_numeric_arith",
        llvm::FunctionType::get(builder_.getVoidTy(), false));

    // collie_rt 数组运行时声明（t59）：不透明 ptr 数组对象，8 字节槽存位模式；
    // 指针拷贝即引用语义（对齐解释器 shared_ptr<ArrayStorage>）
    rt_arr_new_ = module_->getOrInsertFunction(
        "collie_rt_arr_new",
        llvm::FunctionType::get(ptr_ty, {builder_.getInt64Ty(), builder_.getInt64Ty()}, false));
    rt_arr_get_ = module_->getOrInsertFunction(
        "collie_rt_arr_get",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty, builder_.getInt64Ty()}, false));
    rt_arr_set_ = module_->getOrInsertFunction(
        "collie_rt_arr_set",
        llvm::FunctionType::get(
            void_ty, {ptr_ty, builder_.getInt64Ty(), builder_.getInt64Ty()}, false));
    rt_arr_len_ = module_->getOrInsertFunction(
        "collie_rt_arr_len",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty}, false));
    rt_arr_to_str_ = module_->getOrInsertFunction(
        "collie_rt_arr_to_str", llvm::FunctionType::get(ptr_ty, {ptr_ty}, false));
    // 动态域数组接口（t70）：签名边界后元素类型静态不可知，读拼 number（kind
    // 即 tag）、写按运行时 kind 对齐（含 CG7 陷阱）
    rt_arr_kind_ = module_->getOrInsertFunction(
        "collie_rt_arr_kind",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty}, false));
    rt_arr_set_num_ = module_->getOrInsertFunction(
        "collie_rt_arr_set_num",
        llvm::FunctionType::get(
            void_ty,
            {ptr_ty, builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getInt64Ty()},
            false));
    // 异质动态标量数组读写（t139，kind 7）：{tag,bits} 步长 2 槽，出参写回
    rt_arr_set_dyn_ = module_->getOrInsertFunction(
        "collie_rt_arr_set_dyn",
        llvm::FunctionType::get(
            void_ty,
            {ptr_ty, builder_.getInt64Ty(), builder_.getInt64Ty(), builder_.getInt64Ty()},
            false));
    rt_arr_get_dyn_ = module_->getOrInsertFunction(
        "collie_rt_arr_get_dyn",
        llvm::FunctionType::get(
            void_ty,
            {ptr_ty, builder_.getInt64Ty(), ptr_ty, ptr_ty}, false));
    // 数组深比较（t79）：C 层先比 len 再逐元素按运行时 kind（数值系混合
    // double 视图、string strcmp），对齐解释器 values_equal Array 分支，返 1/0
    rt_arr_eq_ = module_->getOrInsertFunction(
        "collie_rt_arr_eq",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty, ptr_ty}, false));
    // collie_rt tuple 动态键 get 声明（t84）：names(kind 3)+values 数组按非空名
    // strcmp 查找，命中返 i64 位模式、未命中报错退出
    rt_tuple_get_ = module_->getOrInsertFunction(
        "collie_rt_tuple_get",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty, ptr_ty, ptr_ty}, false));
    // collie_rt 动态 tuple 运行时对象（t140）：三元不同形状合流/进函数签名，
    // names(kind3)+values(kind7) 打包堆对象，len 恒等元素数
    rt_tuple_new_ = module_->getOrInsertFunction(
        "collie_rt_tuple_new",
        llvm::FunctionType::get(ptr_ty,
                                {builder_.getInt64Ty(), ptr_ty, ptr_ty}, false));
    rt_tuple_len_ = module_->getOrInsertFunction(
        "collie_rt_tuple_len",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty}, false));
    rt_tuple_get_dyn_ = module_->getOrInsertFunction(
        "collie_rt_tuple_get_dyn",
        llvm::FunctionType::get(void_ty,
                                {ptr_ty, builder_.getInt64Ty(), ptr_ty, ptr_ty},
                                false));
    rt_tuple_key_dyn_ = module_->getOrInsertFunction(
        "collie_rt_tuple_key_dyn",
        llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty, ptr_ty, ptr_ty}, false));
    // 动态承载值的 get(key)（t170）：tag 8 交 tuple_key_dyn，其余位方法接收者陷阱
    rt_dyn_tuple_get_ = module_->getOrInsertFunction(
        "collie_rt_dyn_tuple_get",
        llvm::FunctionType::get(
            void_ty,
            {builder_.getInt64Ty(), builder_.getInt64Ty(), ptr_ty, ptr_ty, ptr_ty},
            false));
    rt_tuple_eq_ = module_->getOrInsertFunction(
        "collie_rt_tuple_eq",
        llvm::FunctionType::get(builder_.getInt64Ty(), {ptr_ty, ptr_ty}, false));
    rt_tuple_to_str_ = module_->getOrInsertFunction(
        "collie_rt_tuple_to_str", llvm::FunctionType::get(ptr_ty, {ptr_ty}, false));

    // collie_rt 类实例分配声明（t60）：字段块 malloc，struct 布局读写全在 codegen 侧
    rt_obj_new_ = module_->getOrInsertFunction(
        "collie_rt_obj_new",
        llvm::FunctionType::get(ptr_ty, {builder_.getInt64Ty()}, false));

    // collie_rt number 运行时声明（t62，CG5 收窄）：tagged 双表示（tag 0=整数
    // i64 / 1=小数 double 位模式），算术/比较/转串在运行时单点对齐解释器；
    // 结果经出参写回（16 字节 struct 返回在 Win x64 走隐藏指针，出参避开 ABI 错配）
    llvm::Type* i64_ty = builder_.getInt64Ty();
    rt_num_arith_ = module_->getOrInsertFunction(
        "collie_rt_num_arith",
        llvm::FunctionType::get(
            void_ty, {i64_ty, i64_ty, i64_ty, i64_ty, i64_ty, ptr_ty, ptr_ty}, false));
    rt_num_cmp_ = module_->getOrInsertFunction(
        "collie_rt_num_cmp",
        llvm::FunctionType::get(builder_.getInt32Ty(),
                                {i64_ty, i64_ty, i64_ty, i64_ty, i64_ty}, false));
    // collie_rt 动态承载值比较（t149）：tag 运行期才定，判等复刻解释器
    // values_equal 接受表，关系比较数值/同串两路 + 其余陷阱
    rt_dyn_eq_ = module_->getOrInsertFunction(
        "collie_rt_dyn_eq",
        llvm::FunctionType::get(builder_.getInt32Ty(),
                                {i64_ty, i64_ty, i64_ty, i64_ty}, false));
    rt_dyn_rel_ = module_->getOrInsertFunction(
        "collie_rt_dyn_rel",
        llvm::FunctionType::get(builder_.getInt32Ty(),
                                {i64_ty, i64_ty, i64_ty, i64_ty, i64_ty}, false));
    rt_num_to_str_ = module_->getOrInsertFunction(
        "collie_rt_num_to_str",
        llvm::FunctionType::get(ptr_ty, {i64_ty, i64_ty}, false));
    rt_print_num_ = module_->getOrInsertFunction(
        "collie_rt_print_num",
        llvm::FunctionType::get(void_ty, {i64_ty, i64_ty}, false));
    // toNumber 字符串解析声明（t63）：复刻解释器 to_number_value 的 string
    // 分支，失败返 NaN；结果经出参写回（同 num_arith 的 ABI 规避）
    rt_str_to_num_ = module_->getOrInsertFunction(
        "collie_rt_str_to_num",
        llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty, ptr_ty}, false));

    // 第一遍（S5 t52 / t60）：顶层函数建原型、类注册 struct 布局与方法原型，
    // 递归与前向引用天然可用
    functions_.clear();
    nested_fns_.clear();
    winning_stmt_.clear();
    fn_stmt_to_key_.clear();
    classes_.clear();
    shadow_conflicts_.clear();
    in_function_ = false;
    current_this_ = nullptr;
    current_class_name_.clear();
    // 类声明收集（t127/t128）：按执行序——顶层类与裸块内类一并收（裸块恒
    // 执行，递归下钻），函数体内直线/裸块类声明亦收（t128，函数被调用即
    // 登记，解释器类表全局持久）；if/while/for/switch 体内类声明不收（执行期
    // 条件性，编译期无条件注册会使 codegen 比解释器宽松，属错编方向）
    std::vector<const ClassStmt*> class_decls;
    // 非顶层类 → 声明所属作用域（裸块内沿用外层 owner；函数/方法体为该函数；
    // if/while/for/do-while/switch 各 case 体为其块）：键存在即"延迟就绪"
    std::unordered_map<std::string, const Stmt*> deferred_classes;
    collect_classes_in_order(statements, class_decls, deferred_classes);
    // 非顶层类：注册期置未就绪——对齐解释器"执行到声明语句才注册"，
    // 声明语句之前的 new 在 codegen 侧亦须拒编（不然 codegen 能跑、解释器
    // Undefined class rc=1，两端不一致）
    for (const ClassStmt* c : class_decls) {
        register_class_layout(*c);
    }
    // 遮蔽冲突槽升级 + struct 建体（S131）：register_class_layout 只登记字段
    // 描述符，因为承载不一致的同名遮蔽要把该树内祖先/兄弟的槽一并改判 Dyn
    // （父子前缀偏移必须继续对齐），布局须待全树字段合并完毕才能定
    resolve_shadow_conflicts();
    for (const ClassStmt* c : class_decls) {
        finalize_class_layout(*c);
    }
    for (const auto& entry : deferred_classes) {
        auto bit = classes_.find(entry.first);
        if (bit == classes_.end()) continue;
        bit->second.ready = false;
        // 声明所属作用域（t130）：仅该作用域内（含嵌套）可实例化；裸块内类
        // owner 为 nullptr（顶层语境走到声明块后即可用），函数/方法/控制流块
        // 内类 owner 为该 Stmt*——作用域外 new 保守拒编（是否被调用/块是否执行
        // 属运行期事实，静态无从判定）
        bit->second.owner_scope = entry.second;
    }
    for (const ClassStmt* c : class_decls) {
        // 阶段二：方法单态化原型（签名可引用任意已注册类，t61）
        register_class_methods(*c);
    }
    size_t top_idx = 0;
    for (const auto& stmt : statements) {
        // 阶段三：函数原型（参数/返回值可为类实例，需全部类先就位，t61）
        if (const auto* fn_stmt = dynamic_cast<const FunctionStmt*>(stmt.get())) {
            declare_function(*fn_stmt);
        } else {
            // 顶层控制流块（if/while/for/do/switch/裸块）内的函数声明（t124）：
            // 以顶层语句序号作合成前缀，复用 declare_nested_in 递归登记为块作用域
            // 嵌套函数——数字前缀非法标识符首字符，天然不与用户函数键冲突；可见性
            // 由 visitFunction 声明处按块作用域绑定（对齐解释器"执行到声明处
            // env_.define"，块退出即失效；块外调用两端同为语义层 Undefined 报错）
            declare_nested_in(stmt.get(), std::to_string(top_idx));
        }
        ++top_idx;
    }

    // 顶层语句收拢进 @main（与解释器"脚本式执行"语义对齐）
    auto* main_type = llvm::FunctionType::get(builder_.getInt32Ty(), /*isVarArg=*/false);
    auto* main_fn = llvm::Function::Create(
        main_type, llvm::Function::ExternalLinkage, "main", module_.get());
    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", main_fn));

    // 顶层作用域（块进出时 push/pop，支持遮蔽）
    scopes_.clear();
    scopes_.emplace_back();
    loops_.clear();

    for (const auto& stmt : statements) {
        stmt->accept(*this);
    }
    builder_.CreateRet(builder_.getInt32(0));

    // verifyModule 门禁：生成的 IR 非法即报错，不产出坏模块
    std::string verify_errors;
    llvm::raw_string_ostream verify_stream(verify_errors);
    if (llvm::verifyModule(*module_, &verify_stream)) {
        throw CodeGenError("internal codegen error, invalid IR:\n" + verify_stream.str(),
                           0, 0);
    }
}

std::string CodeGenerator::emit_ir() const {
    std::string out;
    llvm::raw_string_ostream stream(out);
    module_->print(stream, nullptr);
    return stream.str();
}

CodeGenerator::CGValue CodeGenerator::emit(const Expr* expr) {
    expr->accept(*this);
    return last_value_;
}

void CodeGenerator::unsupported(const std::string& what, size_t line, size_t column) {
    throw CodeGenError("codegen: not yet supported: " + what, line, column);
}

// ---------------- 表达式 ----------------

void CodeGenerator::visitLiteral(const LiteralExpr& expr) {
    const Token& tok = expr.token();
    std::string lexeme(tok.lexeme());
    switch (tok.type()) {
        case TokenType::LITERAL_STRING:
            // lexer 已解码转义，lexeme 即最终字符串值
            last_value_ = {builder_.CreateGlobalString(lexeme), CGType::Str};
            return;
        case TokenType::LITERAL_CHAR:
        case TokenType::LITERAL_CHARACTER:
            // char/character 字面量（t69）：解释器运行期即 string（打印裸
            // 字符/字典序比较/可拼接），lexeme 为解码后裸字符，Str 承载即对齐
            last_value_ = {builder_.CreateGlobalString(lexeme), CGType::Str};
            return;
        case TokenType::LITERAL_NUMBER: {
            // 与解释器 visitLiteral 相同的字面量分类约定（interpreter.cpp）
            if (lexeme == "Infinity") {
                last_value_ = {llvm::ConstantFP::getInfinity(builder_.getDoubleTy()),
                               CGType::Double};
                return;
            }
            if (lexeme == "NaN") {
                last_value_ = {llvm::ConstantFP::getNaN(builder_.getDoubleTy()),
                               CGType::Double};
                return;
            }
            const bool is_hex = lexeme.size() > 1 && lexeme[0] == '0' &&
                                (lexeme[1] == 'x' || lexeme[1] == 'X');
            if (is_hex || lexeme.find_first_of(".eEf") == std::string::npos) {
                // 整数字面量：i64 承载（妥协点 CG1：超 i64 报错而非任意精度）
                errno = 0;
                char* end = nullptr;
                const long long v = std::strtoll(lexeme.c_str(), &end, is_hex ? 16 : 10);
                if (errno == ERANGE || (end && *end != '\0')) {
                    throw CodeGenError(
                        "codegen: integer literal out of i64 range (gap CG1): " + lexeme,
                        tok.line(), tok.column());
                }
                last_value_ = {builder_.getInt64(static_cast<uint64_t>(v)), CGType::Int};
                return;
            }
            // 小数字面量（含 '.'/'e'/'f'）：strtod 解析自然停在 'f' 后缀处
            last_value_ = {llvm::ConstantFP::get(builder_.getDoubleTy(),
                                                 std::strtod(lexeme.c_str(), nullptr)),
                           CGType::Double};
            return;
        }
        case TokenType::KW_TRUE:
            last_value_ = {builder_.getInt1(true), CGType::Bool};
            return;
        case TokenType::KW_FALSE:
            last_value_ = {builder_.getInt1(false), CGType::Bool};
            return;
        case TokenType::LITERAL_BOOL:
            last_value_ = {builder_.getInt1(lexeme == "true"), CGType::Bool};
            return;
        case TokenType::KW_UNSET:
            // unset 字面量（t65）：i8 三态编码取 1（False=0 < Unset=1 < True=2）
            last_value_ = {builder_.getInt8(1), CGType::Tri};
            return;
        default:
            unsupported("literal '" + lexeme + "'", tok.line(), tok.column());
    }
}

void CodeGenerator::visitBinary(const BinaryExpr& expr) {
    const Token& op = expr.op();

    // && / || 需短路求值（与解释器对齐，interpreter.cpp visitBinary），
    // 必须先于下方的两侧急切求值处理
    if (op.type() == TokenType::OP_AND || op.type() == TokenType::OP_OR) {
        gen_logical(expr);
        return;
    }

    CGValue lhs = emit(expr.left());
    CGValue rhs = emit(expr.right());

    // 仅数值算术在范围内（比较/逻辑/位运算属 S3+）；Num 也是数值（t62）
    auto require_numeric = [&](const CGValue& v) {
        if (v.type != CGType::Int && v.type != CGType::Double &&
            v.type != CGType::Num) {
            unsupported("non-numeric operand of '" + std::string(op.lexeme()) + "'",
                        op.line(), op.column());
        }
    };

    // number 参与的算术/比较（t62）：任一侧为 Num 即双方 to_num 后下沉
    // collie_rt，双整数精确/混合 double 视图等语义单点对齐解释器
    const bool has_num = lhs.type == CGType::Num || rhs.type == CGType::Num;

    // Dyn 参与算术（t148）：kind 7 数组元素/异质 tuple 非常量索引/动态 tuple
    // 索引/object 动态承载槽读出的 {tag,bits} 与 Num 同构（tag 0=整数 1=小数
    // 2=bool 3=串 5=实例…），tag 运行期才定——按解释器 eval_arithmetic 的
    // 运行期分派插守卫：'+' 任一 Dyn 侧为字符串位即拼接（双侧 to_string，
    // 结果 Dyn -tag 3），否则双侧须数值态（tag < 2），不符落
    // collie_rt_trap_non_numeric_arith（解释器同为 "Arithmetic operands must
    // be numbers" 报错退出，陷阱路径 stdout 无增量）；静态非数值侧（bool/
    // tribool/Obj/Arr 等）维持原拒编（解释器同为运行期报错，两端皆不产出值）
    const bool has_dyn = lhs.type == CGType::Dyn || rhs.type == CGType::Dyn;
    const auto dyn_arith_ok = [](CGType ty) {
        return ty == CGType::Int || ty == CGType::Double ||
               ty == CGType::Num || ty == CGType::Dyn;
    };
    auto require_dyn_arith = [&]() {
        if (!dyn_arith_ok(lhs.type) || !dyn_arith_ok(rhs.type)) {
            unsupported("non-numeric operand of '" + std::string(op.lexeme()) + "'",
                        op.line(), op.column());
        }
    };

    switch (op.type()) {
        case TokenType::OP_DIVIDE: {
            // '/' 恒产小数（SPEC §4，Python 式 true division）；
            // 除零走 IEEE 754 得 ±Infinity/NaN（t33），fdiv 天然满足
            if (has_dyn) {
                require_dyn_arith();
                llvm::Value* a = dyn_num_operand(lhs, op);
                llvm::Value* b = dyn_num_operand(rhs, op);
                last_value_ = {call_num_arith(3, a, b), CGType::Num};
                return;
            }
            require_numeric(lhs);
            require_numeric(rhs);
            if (has_num) {
                last_value_ = {call_num_arith(3, to_num(lhs), to_num(rhs)), CGType::Num};
                return;
            }
            last_value_ = {builder_.CreateFDiv(to_double(lhs), to_double(rhs), "divtmp"),
                           CGType::Double};
            return;
        }
        case TokenType::OP_MODULO: {
            // '%' floor 取模（SPEC §4，结果符号与除数一致）：
            //   r = srem(a, b); r 非零且与 b 异号时 r += b（select 无分支实现）
            if (has_dyn) {
                // Dyn 参与（t148）：双侧数值守卫后下沉运行时（双整数 BigInt
                // floor 取模、混合走 double，对齐解释器 eval_arithmetic）
                require_dyn_arith();
                llvm::Value* a = dyn_num_operand(lhs, op);
                llvm::Value* b = dyn_num_operand(rhs, op);
                last_value_ = {call_num_arith(4, a, b), CGType::Num};
                return;
            }
            if (has_num) {
                // number 参与（t62）：下沉运行时（双整数 floor 取模、除零落
                // double 路径 NaN，对齐解释器 eval_arithmetic）
                require_numeric(lhs);
                require_numeric(rhs);
                last_value_ = {call_num_arith(4, to_num(lhs), to_num(rhs)), CGType::Num};
                return;
            }
            if (lhs.type == CGType::Double || rhs.type == CGType::Double) {
                // decimal 参与（t80）：FRem 语义即 fmod（截断取余），floor 修正
                // 对齐解释器 eval_arithmetic——r 非零且与除数异号时 r += b；
                // 除零 FRem 天然 NaN，且 NaN 使 ONE 比较为 false 不触发修正
                // （与解释器 b==0.0 提前返 NaN 殊途同归）；-0.0 == 0.0 使
                // nonzero 为 false 同解释器 r != 0.0 判定
                require_numeric(lhs);
                require_numeric(rhs);
                llvm::Value* l = to_double(lhs);
                llvm::Value* r = to_double(rhs);
                llvm::Value* rem = builder_.CreateFRem(l, r, "fremtmp");
                llvm::Value* fzero =
                    llvm::ConstantFP::get(builder_.getDoubleTy(), 0.0);
                llvm::Value* nonzero = builder_.CreateFCmpONE(rem, fzero);
                llvm::Value* rem_neg = builder_.CreateFCmpOLT(rem, fzero);
                llvm::Value* rhs_neg = builder_.CreateFCmpOLT(r, fzero);
                llvm::Value* sign_diff = builder_.CreateICmpNE(rem_neg, rhs_neg);
                llvm::Value* need_fix = builder_.CreateAnd(nonzero, sign_diff);
                llvm::Value* fixed = builder_.CreateFAdd(rem, r, "fremfix");
                last_value_ = {builder_.CreateSelect(need_fix, fixed, rem,
                                                     "floorfmod"),
                               CGType::Double};
                return;
            }
            if (lhs.type != CGType::Int || rhs.type != CGType::Int) {
                unsupported("'%' on non-integer operands", op.line(), op.column());
            }
            // INT64_MIN % -1 会触发 x86 idiv 硬件陷阱（srem UB，CG1 t58）；
            // 数学结果为 0（解释器 BigInt floor_mod 同），select 换安全除数 1：
            // srem(INT64_MIN, 1) = 0，后续 need_fix 不触发，结果自然为 0
            llvm::Value* int_min = llvm::ConstantInt::get(
                builder_.getInt64Ty(), llvm::APInt::getSignedMinValue(64));
            llvm::Value* neg_one =
                llvm::ConstantInt::getSigned(builder_.getInt64Ty(), -1);
            llvm::Value* is_edge = builder_.CreateAnd(
                builder_.CreateICmpEQ(lhs.value, int_min),
                builder_.CreateICmpEQ(rhs.value, neg_one));
            llvm::Value* safe_rhs =
                builder_.CreateSelect(is_edge, builder_.getInt64(1), rhs.value, "safediv");
            llvm::Value* rem = builder_.CreateSRem(lhs.value, safe_rhs, "remtmp");
            llvm::Value* zero = builder_.getInt64(0);
            llvm::Value* nonzero = builder_.CreateICmpNE(rem, zero);
            llvm::Value* rem_neg = builder_.CreateICmpSLT(rem, zero);
            llvm::Value* rhs_neg = builder_.CreateICmpSLT(rhs.value, zero);
            llvm::Value* sign_diff = builder_.CreateICmpNE(rem_neg, rhs_neg);
            llvm::Value* need_fix = builder_.CreateAnd(nonzero, sign_diff);
            // remfix 不会溢出：|rem| < |rhs| 且二者异号，rem+rhs 落在 (-|rhs|, |rhs|)
            llvm::Value* fixed = builder_.CreateAdd(rem, rhs.value, "remfix");
            last_value_ = {builder_.CreateSelect(need_fix, fixed, rem, "floormod"),
                           CGType::Int};
            return;
        }
        case TokenType::OP_PLUS:
        case TokenType::OP_MINUS:
        case TokenType::OP_MULTIPLY: {
            // '+' 任一侧为 string 即拼接（与解释器一致，非 string 侧隐式转串）；
            // 走 collie_rt_concat（malloc 出新串，缺口 CG6：不 free）
            if (op.type() == TokenType::OP_PLUS &&
                (lhs.type == CGType::Str || rhs.type == CGType::Str)) {
                last_value_ = {builder_.CreateCall(
                                   rt_concat_, {to_str(lhs, op), to_str(rhs, op)}, "concattmp"),
                               CGType::Str};
                return;
            }
            if (has_dyn) {
                // Dyn 参与（t148）：'+' 需运行期分派字符串位（解释器任一侧
                // 字符串即拼接，结果类型只有运行期才定，故出 CGType::Dyn）；
                // -/* 双侧强制数值守卫，结果 Num
                require_dyn_arith();
                if (op.type() == TokenType::OP_PLUS) {
                    last_value_ = {dyn_concat_or_add(lhs, rhs, op), CGType::Dyn};
                    return;
                }
                const int op_code = op.type() == TokenType::OP_MINUS ? 1 : 2;
                llvm::Value* a = dyn_num_operand(lhs, op);
                llvm::Value* b = dyn_num_operand(rhs, op);
                last_value_ = {call_num_arith(op_code, a, b), CGType::Num};
                return;
            }
            require_numeric(lhs);
            require_numeric(rhs);
            if (has_num) {
                // number 参与（t62）：下沉运行时（双整数精确 + i64 溢出走
                // CG1 陷阱，混合走 double，对齐解释器 eval_arithmetic）
                const int op_code = op.type() == TokenType::OP_PLUS    ? 0
                                  : op.type() == TokenType::OP_MINUS   ? 1 : 2;
                last_value_ = {call_num_arith(op_code, to_num(lhs), to_num(rhs)),
                               CGType::Num};
                return;
            }
            if (lhs.type == CGType::Double || rhs.type == CGType::Double) {
                llvm::Value* l = to_double(lhs);
                llvm::Value* r = to_double(rhs);
                llvm::Value* v = op.type() == TokenType::OP_PLUS ? builder_.CreateFAdd(l, r)
                               : op.type() == TokenType::OP_MINUS ? builder_.CreateFSub(l, r)
                                                                  : builder_.CreateFMul(l, r);
                last_value_ = {v, CGType::Double};
            } else {
                // i64 域（CG1 t58）：带溢出检查，溢出报错退出不静默回绕
                llvm::Value* v =
                    op.type() == TokenType::OP_PLUS
                        ? checked_int_arith(llvm::Intrinsic::sadd_with_overflow,
                                            lhs.value, rhs.value, "addtmp")
                    : op.type() == TokenType::OP_MINUS
                        ? checked_int_arith(llvm::Intrinsic::ssub_with_overflow,
                                            lhs.value, rhs.value, "subtmp")
                        : checked_int_arith(llvm::Intrinsic::smul_with_overflow,
                                            lhs.value, rhs.value, "multmp");
                last_value_ = {v, CGType::Int};
            }
            return;
        }
        case TokenType::OP_EQUAL:
        case TokenType::OP_NOT_EQUAL:
        case TokenType::OP_LESS:
        case TokenType::OP_LESS_EQ:
        case TokenType::OP_GREATER:
        case TokenType::OP_GREATER_EQ: {
            // 比较运算（S3 t50）：bool 仅支持 ==/!=；数值混型提升为 double 后 fcmp，
            // 纯整数走 icmp；!= 用 UNE（NaN != NaN 为 true，IEEE 语义与解释器一致）
            const TokenType t = op.type();
            // none 相等（t81）：双 Void 恒 true（对齐解释器 values_equal None
            // 分支）；Void × 非 Void 恒 false（语义层已拦截 "Incomparable
            // operand types"，此分支为防御性双保险）；两侧已 emit 副作用保序
            if ((lhs.type == CGType::Void || rhs.type == CGType::Void) &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                llvm::Value* eq = builder_.getInt1(lhs.type == rhs.type);
                last_value_ = {t == TokenType::OP_EQUAL
                                   ? eq
                                   : builder_.CreateNot(eq, "cmptmp"),
                               CGType::Bool};
                return;
            }
            // tuple 相等（t75）：任一侧 tuple 族且 ==/!= 时深比较；tuple 族 ×
            // 非承载、非 tuple 静态侧恒 false（对齐解释器 values_equal kind 不等，
            // 语义层不允许 Tuple 变量与 object/标量互比）；tuple 族 × Dyn 承载位
            // 走 rt_dyn_eq（t179，见下）；
            // 动态 tuple（t140）：两侧皆 tuple 族时静态侧先物化为动态对象，
            // 交 rt_tuple_eq 深比较（len+名字表+元素，含 t164 的数组位/实例位与
            // t170 的嵌套 tuple 位）；tuple 关系比较落下方 require_numeric 拒编
            //（解释器同样不支持）
            {
                const auto is_tup = [](CGType ty) {
                    return ty == CGType::Tup || ty == CGType::TupDyn;
                };
                if ((is_tup(lhs.type) || is_tup(rhs.type)) &&
                    (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                    llvm::Value* eq = nullptr;
                    if (is_tup(lhs.type) && is_tup(rhs.type)) {
                        if (lhs.type == CGType::Tup && rhs.type == CGType::Tup) {
                            eq = gen_tuple_eq(lhs, rhs, op);
                        } else {
                            llvm::Value* l = lhs.type == CGType::TupDyn
                                                 ? lhs.value
                                                 : materialize_tuple_dynamic(
                                                       lhs.tup, op.line(), op.column());
                            llvm::Value* r = rhs.type == CGType::TupDyn
                                                 ? rhs.value
                                                 : materialize_tuple_dynamic(
                                                       rhs.tup, op.line(), op.column());
                            llvm::Value* c = builder_.CreateCall(
                                rt_tuple_eq_, {l, r}, "tupeqdyn");
                            eq = builder_.CreateICmpNE(c, builder_.getInt64(0), "cmptmp");
                        }
                    } else if (has_dyn) {
                        // tuple 族 × Dyn 承载位（t179）：Tuple 型类字段读出为
                        // TupDyn，进 object 槽即 tag 8 位，两者回比在解释器是
                        // values_equal 的 tuple 分支（按名字表与元素深比较）——
                        // 原 else 分支把它折成常量 false 属静默错值。改法与 t169
                        // 的 Arr 折叠修正同形：tuple 族侧经 to_dyn 打 tag 8（静态
                        // 展开侧先物化），两侧交既有 rt_dyn_eq 的 case 8 深比较，
                        // 非 tuple 承载位天然落 kind 不等 → false，与解释器同源
                        llvm::Value* a = to_dyn(lhs, op.line(), op.column());
                        llvm::Value* b = to_dyn(rhs, op.line(), op.column());
                        llvm::Value* c = builder_.CreateCall(
                            rt_dyn_eq_, {num_tag(a), num_bits(a), num_tag(b),
                                         num_bits(b)},
                            "dyneq");
                        eq = builder_.CreateICmpNE(c, builder_.getInt32(0), "cmptmp");
                    } else {
                        eq = builder_.getInt1(false);
                    }
                    last_value_ = {t == TokenType::OP_EQUAL
                                       ? eq
                                       : builder_.CreateNot(eq, "cmptmp"),
                                   CGType::Bool};
                    return;
                }
            }
            // 数组相等（t79）：Arr × Arr 下沉 rt_arr_eq 深比较（先比 len 再逐
            // 元素按运行时 kind，对齐解释器 values_equal Array 分支）；
            // Arr × 非 Arr 整体比较语义层已拦截（"Incomparable operand types"），
            // 关系比较落下方 require_numeric 拒编（解释器同样不支持）
            if (lhs.type == CGType::Arr && rhs.type == CGType::Arr &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                llvm::Value* c = builder_.CreateCall(
                    rt_arr_eq_, {lhs.value, rhs.value}, "arreqtmp");
                llvm::Value* eq =
                    builder_.CreateICmpNE(c, builder_.getInt64(0), "cmptmp");
                last_value_ = {t == TokenType::OP_EQUAL
                                   ? eq
                                   : builder_.CreateNot(eq, "cmptmp"),
                               CGType::Bool};
                return;
            }
            // 实例（Obj）/函数值（Fn）相等（t82/t143）：解释器 values_equal 无
            // Instance/Function 分支落 default 恒 false（含同一实例 a==a、同一
            // 函数 add==add——实证 false），故 ==/!= 常量折叠：同族与跨族
            // （kind 不等语义层多已拦截，此为双保险）均恒 false；关系比较
            // 落下方 require_numeric 拒编（解释器同为运行期报错，非差分面）
            if ((lhs.type == CGType::Obj || rhs.type == CGType::Obj ||
                 lhs.type == CGType::Fn || rhs.type == CGType::Fn) &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                llvm::Value* eq = builder_.getInt1(false);
                last_value_ = {t == TokenType::OP_EQUAL
                                   ? eq
                                   : builder_.CreateNot(eq, "cmptmp"),
                               CGType::Bool};
                return;
            }
            // string × string（S7 t55）：call collie_rt_strcmp 后与 0 做对应 icmp；
            // 逐字节字典序与解释器 std::string 比较一致（eval_comparison/values_equal）；
            // 混型（Str × 非 Str）落入下方 require_numeric 拒编，解释器运行期也报错
            if (lhs.type == CGType::Str && rhs.type == CGType::Str) {
                llvm::Value* c = builder_.CreateCall(rt_strcmp_, {lhs.value, rhs.value}, "strcmptmp");
                llvm::Value* zero = builder_.getInt32(0);
                llvm::Value* v = t == TokenType::OP_EQUAL      ? builder_.CreateICmpEQ(c, zero, "cmptmp")
                               : t == TokenType::OP_NOT_EQUAL  ? builder_.CreateICmpNE(c, zero, "cmptmp")
                               : t == TokenType::OP_LESS       ? builder_.CreateICmpSLT(c, zero, "cmptmp")
                               : t == TokenType::OP_LESS_EQ    ? builder_.CreateICmpSLE(c, zero, "cmptmp")
                               : t == TokenType::OP_GREATER    ? builder_.CreateICmpSGT(c, zero, "cmptmp")
                                                               : builder_.CreateICmpSGE(c, zero, "cmptmp");
                last_value_ = {v, CGType::Bool};
                return;
            }
            // tribool 三态判等（t65）：任一侧 tribool 时双方限 tribool/bool
            // （bool 加宽为三态后 icmp，对齐解释器 values_equal），仅 ==/!=；
            // 关系比较落下方 require_numeric 拒编（解释器运行期也报错）
            if ((lhs.type == CGType::Tri || rhs.type == CGType::Tri) &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                if ((lhs.type != CGType::Tri && lhs.type != CGType::Bool) ||
                    (rhs.type != CGType::Tri && rhs.type != CGType::Bool)) {
                    unsupported("comparison of tribool with this value type",
                                op.line(), op.column());
                }
                llvm::Value* l = to_tri(lhs);
                llvm::Value* r = to_tri(rhs);
                llvm::Value* v = t == TokenType::OP_EQUAL
                                     ? builder_.CreateICmpEQ(l, r, "cmptmp")
                                     : builder_.CreateICmpNE(l, r, "cmptmp");
                last_value_ = {v, CGType::Bool};
                return;
            }
            if (lhs.type == CGType::Bool && rhs.type == CGType::Bool &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                llvm::Value* v = t == TokenType::OP_EQUAL
                                     ? builder_.CreateICmpEQ(lhs.value, rhs.value, "cmptmp")
                                     : builder_.CreateICmpNE(lhs.value, rhs.value, "cmptmp");
                last_value_ = {v, CGType::Bool};
                return;
            }
            // Dyn 参与比较（t149）：kind 7 数组元素/异质与动态 tuple 索引/
            // object 动态承载槽读出的 {tag,bits}，值 kind 只有运行期可知——
            // 等值下沉 rt_dyn_eq（复刻解释器 values_equal：tribool×bool 加宽、
            // Number 跨 tag double 视图、串内容、数组深比较、实例位与 kind 失配
            // 恒 false，该路径解释器不报错）；关系比较下沉 rt_dyn_rel（双侧数值
            // 态走 num_cmp、同串走 strcmp，其余落 "Comparison operands must be
            // both numbers or both strings" 陷阱，与解释器 eval_comparison 同为
            // 运行期报错，陷阱路径 stdout 无增量）。静态侧仅 Int/Double/Num/Str
            // 可盒化为比较表示，其余（含 Obj/Arr/Tup——语义层多已同拒）维持拒编
            // 不错编：既有 Obj/Fn/Tup/Arr 等值折叠臂在本分支之前，行为不变
            if (has_dyn) {
                const auto cmp_side_ok = [](CGType ty) {
                    return ty == CGType::Dyn || ty == CGType::Int ||
                           ty == CGType::Double || ty == CGType::Num ||
                           ty == CGType::Str;
                };
                if (!cmp_side_ok(lhs.type) || !cmp_side_ok(rhs.type)) {
                    unsupported("comparison of dynamic value with this type",
                                op.line(), op.column());
                }
                // 求值顺序固定先 lhs 后 rhs（同 t148，避开参数求值顺序未定）
                llvm::Value* a = dyn_cmp_box(lhs, op);
                llvm::Value* b = dyn_cmp_box(rhs, op);
                if (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL) {
                    llvm::Value* c = builder_.CreateCall(
                        rt_dyn_eq_, {num_tag(a), num_bits(a), num_tag(b), num_bits(b)},
                        "dyneq");
                    llvm::Value* eq =
                        builder_.CreateICmpNE(c, builder_.getInt32(0), "cmptmp");
                    last_value_ = {t == TokenType::OP_EQUAL
                                       ? eq
                                       : builder_.CreateNot(eq, "cmptmp"),
                                   CGType::Bool};
                    return;
                }
                const int op_code = t == TokenType::OP_LESS    ? 2
                                  : t == TokenType::OP_LESS_EQ ? 3
                                  : t == TokenType::OP_GREATER ? 4 : 5;
                llvm::Value* c = builder_.CreateCall(
                    rt_dyn_rel_, {builder_.getInt64(static_cast<uint64_t>(op_code)),
                                  num_tag(a), num_bits(a), num_tag(b), num_bits(b)},
                    "dynrel");
                last_value_ = {builder_.CreateICmpNE(c, builder_.getInt32(0), "cmptmp"),
                               CGType::Bool};
                return;
            }
            if (has_num) {
                // number 比较（t62）：collie_rt_num_cmp（双整数精确、混合 double
                // 视图、NaN IEEE 语义），返 0/1 后与 0 做 icmp ne 得 i1
                require_numeric(lhs);
                require_numeric(rhs);
                const int op_code = t == TokenType::OP_EQUAL      ? 0
                                  : t == TokenType::OP_NOT_EQUAL  ? 1
                                  : t == TokenType::OP_LESS       ? 2
                                  : t == TokenType::OP_LESS_EQ    ? 3
                                  : t == TokenType::OP_GREATER    ? 4 : 5;
                llvm::Value* a = to_num(lhs);
                llvm::Value* b = to_num(rhs);
                llvm::Value* c = builder_.CreateCall(
                    rt_num_cmp_,
                    {builder_.getInt64(static_cast<uint64_t>(op_code)),
                     num_tag(a), num_bits(a), num_tag(b), num_bits(b)},
                    "numcmp");
                last_value_ = {builder_.CreateICmpNE(c, builder_.getInt32(0), "cmptmp"),
                               CGType::Bool};
                return;
            }
            require_numeric(lhs);
            require_numeric(rhs);
            llvm::Value* v = nullptr;
            if (lhs.type == CGType::Double || rhs.type == CGType::Double) {
                llvm::Value* l = to_double(lhs);
                llvm::Value* r = to_double(rhs);
                v = t == TokenType::OP_EQUAL      ? builder_.CreateFCmpOEQ(l, r, "cmptmp")
                  : t == TokenType::OP_NOT_EQUAL  ? builder_.CreateFCmpUNE(l, r, "cmptmp")
                  : t == TokenType::OP_LESS       ? builder_.CreateFCmpOLT(l, r, "cmptmp")
                  : t == TokenType::OP_LESS_EQ    ? builder_.CreateFCmpOLE(l, r, "cmptmp")
                  : t == TokenType::OP_GREATER    ? builder_.CreateFCmpOGT(l, r, "cmptmp")
                                                  : builder_.CreateFCmpOGE(l, r, "cmptmp");
            } else {
                v = t == TokenType::OP_EQUAL      ? builder_.CreateICmpEQ(lhs.value, rhs.value, "cmptmp")
                  : t == TokenType::OP_NOT_EQUAL  ? builder_.CreateICmpNE(lhs.value, rhs.value, "cmptmp")
                  : t == TokenType::OP_LESS       ? builder_.CreateICmpSLT(lhs.value, rhs.value, "cmptmp")
                  : t == TokenType::OP_LESS_EQ    ? builder_.CreateICmpSLE(lhs.value, rhs.value, "cmptmp")
                  : t == TokenType::OP_GREATER    ? builder_.CreateICmpSGT(lhs.value, rhs.value, "cmptmp")
                                                  : builder_.CreateICmpSGE(lhs.value, rhs.value, "cmptmp");
            }
            last_value_ = {v, CGType::Bool};
            return;
        }
        case TokenType::OP_BIT_AND:
        case TokenType::OP_BIT_OR:
        case TokenType::OP_BIT_XOR: {
            // 位运算（t69）：仅整数域（对齐解释器 eval_bitwise 的 int64 路径）；
            // Num 整数态 tag 静态不可判、Double 拒编不错编
            if (lhs.type != CGType::Int || rhs.type != CGType::Int) {
                unsupported("bitwise '" + std::string(op.lexeme()) +
                                "' on non-integer operand",
                            op.line(), op.column());
            }
            llvm::Value* v =
                op.type() == TokenType::OP_BIT_AND
                    ? builder_.CreateAnd(lhs.value, rhs.value, "bandtmp")
                : op.type() == TokenType::OP_BIT_OR
                    ? builder_.CreateOr(lhs.value, rhs.value, "bortmp")
                    : builder_.CreateXor(lhs.value, rhs.value, "bxortmp");
            last_value_ = {v, CGType::Int};
            return;
        }
        case TokenType::OP_BIT_LSHIFT:
        case TokenType::OP_BIT_RSHIFT: {
            // 移位（t69）：移位量限 0-63，越界运行时陷阱（对齐解释器报错，
            // 回避 shl/ashr 移位量越界的 poison）；左移位模式移动 =
            // 解释器无符号域回绕，右移 ashr = 解释器算术移位（符号扩展）
            if (lhs.type != CGType::Int || rhs.type != CGType::Int) {
                unsupported("shift '" + std::string(op.lexeme()) +
                                "' on non-integer operand",
                            op.line(), op.column());
            }
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            auto* trap_bb = llvm::BasicBlock::Create(context_, "shift.trap", fn);
            auto* cont_bb = llvm::BasicBlock::Create(context_, "shift.cont", fn);
            // 无符号比较 (u64)count > 63 一次覆盖负数与超上限
            llvm::Value* bad = builder_.CreateICmpUGT(
                rhs.value, builder_.getInt64(63), "shift.bad");
            builder_.CreateCondBr(bad, trap_bb, cont_bb);
            builder_.SetInsertPoint(trap_bb);
            builder_.CreateCall(rt_trap_shift_count_);
            builder_.CreateUnreachable();
            builder_.SetInsertPoint(cont_bb);
            llvm::Value* v = op.type() == TokenType::OP_BIT_LSHIFT
                                 ? builder_.CreateShl(lhs.value, rhs.value, "shltmp")
                                 : builder_.CreateAShr(lhs.value, rhs.value, "ashrtmp");
            last_value_ = {v, CGType::Int};
            return;
        }
        default:
            unsupported("binary operator '" + std::string(op.lexeme()) + "'",
                        op.line(), op.column());
    }
}

void CodeGenerator::visitUnary(const UnaryExpr& expr) {
    const Token& op = expr.op();
    if (op.type() == TokenType::OP_NOT) {
        // 逻辑非：bool 直接取反；tribool Kleene 非 2-t（true↔false，unset
        // 不变，对齐解释器 Kleene 非，t65）
        CGValue v = emit(expr.operand());
        if (v.type == CGType::Tri) {
            last_value_ = {builder_.CreateSub(builder_.getInt8(2), v.value, "nottmp"),
                           CGType::Tri};
            return;
        }
        if (v.type != CGType::Bool) {
            unsupported("'!' on non-bool operand", op.line(), op.column());
        }
        last_value_ = {builder_.CreateNot(v.value, "nottmp"), CGType::Bool};
        return;
    }
    if (op.type() == TokenType::OP_BIT_NOT) {
        // 按位取反（t69）：i64 域 xor 全一（~x = -x-1，与解释器 BigInt 精确
        // 取反在 i64 范围内一致）；仅整数操作数，其余拒编
        CGValue v = emit(expr.operand());
        if (v.type != CGType::Int) {
            unsupported("'~' on non-integer operand", op.line(), op.column());
        }
        last_value_ = {builder_.CreateNot(v.value, "bnottmp"), CGType::Int};
        return;
    }
    if (op.type() != TokenType::OP_MINUS) {
        unsupported("unary operator '" + std::string(op.lexeme()) + "'",
                    op.line(), op.column());
    }
    CGValue v = emit(expr.operand());
    if (v.type == CGType::Int) {
        // 一元负号也走溢出检查（CG1 t58）：-INT64_MIN 超 i64 范围，陷阱报错
        last_value_ = {checked_int_arith(llvm::Intrinsic::ssub_with_overflow,
                                         builder_.getInt64(0), v.value, "negtmp"),
                       CGType::Int};
    } else if (v.type == CGType::Double) {
        last_value_ = {builder_.CreateFNeg(v.value, "negtmp"), CGType::Double};
    } else if (v.type == CGType::Num) {
        // number 一元负号（t62）：op 5 下沉运行时（-INT64_MIN 走 CG1 陷阱）
        llvm::Value* zero = make_num(builder_.getInt64(0), builder_.getInt64(0));
        last_value_ = {call_num_arith(5, to_num(v), zero), CGType::Num};
    } else {
        unsupported("unary '-' on non-numeric operand", op.line(), op.column());
    }
}

void CodeGenerator::visitCall(const CallExpr& expr) {
    // 与解释器 visitCall 相同的内建分发方式：callee 为名为 print 的标识符
    const auto* callee = dynamic_cast<const IdentifierExpr*>(expr.callee());
    if (callee && callee->name().lexeme() == "print") {
        gen_print(expr);
        return;
    }
    // toString 内建（S7 t54）：任意标量转串（与解释器 Value::to_string 对齐）；
    // 字符串插值 @"{expr}" 由 parser 脱糖为 toString(expr) 拼接链，此处即插值落地点。
    // 内建分发先于用户函数查表，与解释器 visitCall 的优先顺序一致
    if (callee && callee->name().lexeme() == "toString") {
        const auto& arguments = expr.arguments();
        if (arguments.size() != 1) {
            // 语义层已校验元数，此处防御
            unsupported("toString expects exactly 1 argument",
                        expr.paren().line(), expr.paren().column());
        }
        CGValue v = emit(arguments[0].get());
        last_value_ = {to_str(v, expr.paren()), CGType::Str};
        return;
    }
    // len 内建（t59）：array 元素个数 / string UTF-8 码点数（对齐解释器 len，
    // 长度恒为整数）；其余参数类型拒编不错编
    if (callee && callee->name().lexeme() == "len") {
        const auto& arguments = expr.arguments();
        if (arguments.size() != 1) {
            // 语义层已校验元数，此处防御
            unsupported("len expects exactly 1 argument",
                        expr.paren().line(), expr.paren().column());
        }
        CGValue v = emit(arguments[0].get());
        if (v.type == CGType::Arr) {
            last_value_ = {builder_.CreateCall(rt_arr_len_, {v.value}, "lentmp"),
                           CGType::Int};
            return;
        }
        if (v.type == CGType::Str) {
            last_value_ = {builder_.CreateCall(rt_str_len_, {v.value}, "lentmp"),
                           CGType::Int};
            return;
        }
        if (v.type == CGType::Dyn) {
            // 动态承载值长度面（t152）：解释器 call_builtin_len 只认 array/string，
            // 其余运行期报错，故按 tag 运行期分派（3=串码点、4/7=数组元素数，
            // 其余落 "len() expects a string or array, got <kind>" 陷阱）
            last_value_ = {builder_.CreateCall(rt_dyn_len_,
                                               {num_tag(v.value), num_bits(v.value),
                                                builder_.getInt64(1)},
                                               "dynlen"),
                           CGType::Int};
            return;
        }
        unsupported("len() of this value type",
                    expr.paren().line(), expr.paren().column());
    }
    // toNumber 内建（t63）：结果恒为 number（tagged 双表示）；string 解析
    // 下沉 collie_rt，bool/integer/decimal/number 纯 IR 内联转 Num；动态承载值
    // 下沉 collie_rt_dyn_to_number 按运行期 tag 分派（t155）；
    // array/tuple/实例参数拒编（解释器此处为运行期报错）
    if (callee && callee->name().lexeme() == "toNumber") {
        const auto& arguments = expr.arguments();
        if (arguments.size() != 1) {
            // 语义层已校验元数，此处防御
            unsupported("toNumber expects exactly 1 argument",
                        expr.paren().line(), expr.paren().column());
        }
        CGValue v = emit(arguments[0].get());
        last_value_ = {to_number_num(v, expr.paren().line(), expr.paren().column()),
                       CGType::Num};
        return;
    }
    // 用户自定义函数（S5 t52）：查第一遍建好的原型表；作用域链上的嵌套
    // 函数绑定优先（t91，遮蔽顶层同名，对齐解释器 env 由内向外解析）
    if (callee) {
        const std::string fname(callee->name().lexeme());
        const CGFunction* info = nullptr;
        CGVar* bound = lookup_var(fname);
        if (bound != nullptr && !bound->fn_key.empty()) {
            auto fit = functions_.find(bound->fn_key);
            if (fit != functions_.end()) {
                info = &fit->second;
            }
        }
        if (info == nullptr) {
            auto it = functions_.find(fname);
            if (it != functions_.end()) {
                info = &it->second;
            }
        }
        if (info != nullptr) {
            const auto& arguments = expr.arguments();
            if (arguments.size() != info->param_types.size()) {
                // 元数不匹配（同名函数按最后定义胜出，t131）
                unsupported("call arity mismatch",
                            expr.paren().line(), expr.paren().column());
            }
            std::vector<llvm::Value*> args;
            for (size_t i = 0; i < arguments.size(); ++i) {
                CGValue a = emit(arguments[i].get());
                // 实参按形参类型对齐：仅 integer→decimal 提升；Obj 严格同类（t61）
                args.push_back(coerce_call_arg(a, info->param_types[i],
                                               info->param_cls[i],
                                               expr.paren().line(),
                                               expr.paren().column()));
            }
            // 闭包捕获实参（t141）：签名尾随等长 ptr 形参，逐捕获传宿主单元地址。
            // 据 cell_id 一致性取当前作用域可见的同名变量槽——一致即同一单元
            // （宿主 alloca / 内层转发所得 ptr / 自递归自身捕获绑定），不一致即
            // 该名已被内层新声明遮蔽，此刻无从取得登记的宿主槽指针，保守拒编不错编。
            for (const CGFunction::Capture& cap : info->captures) {
                CGVar* cv = lookup_var(cap.name);
                if (cv == nullptr || cv->cell_id != cap.cell_id ||
                    cv->slot == nullptr) {
                    unsupported("closure variable '" + cap.name +
                                    "' not capturable at this call",
                                expr.paren().line(), expr.paren().column());
                }
                args.push_back(cv->slot);
            }
            llvm::CallInst* call = builder_.CreateCall(info->fn, args);
            // Arr 返回值 elem 记 Num 哨兵（t70：跨签名边界元素类型动态化）
            last_value_ = (info->ret_type == CGType::Void)
                              ? CGValue{nullptr, CGType::Void}
                              : CGValue{call, info->ret_type,
                                        info->ret_type == CGType::Arr
                                            ? CGType::Num
                                            : CGType::Int,
                                        info->ret_cls};
            return;
        }
    }
    unsupported("function call", expr.paren().line(), expr.paren().column());
}

void CodeGenerator::gen_print(const CallExpr& expr) {
    // print(a, b, ...)：与解释器 call_builtin_print 对齐——空格分隔 + 末尾换行；
    // 逐参按 CGType 调对应 collie_rt 接口（垫片接管格式化，输出对齐解释器）。
    // 两阶段（t77 修 CG8）：先求值全部实参（副作用输出按源序发生在本行
    // 之前，对齐解释器先求值再打印），再统一输出；打印阶段的
    // to_str/tuple_to_str/arr_to_str 无输出副作用，安全
    const auto& arguments = expr.arguments();
    std::vector<CGValue> values;
    values.reserve(arguments.size());
    for (const auto& argument : arguments) {
        values.push_back(emit(argument.get()));
    }
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) builder_.CreateCall(rt_print_sep_, {}); // 参数间单个空格
        const CGValue& v = values[i];
        switch (v.type) {
            case CGType::Str:
                builder_.CreateCall(rt_print_str_, {v.value});
                break;
            case CGType::Int:
                builder_.CreateCall(rt_print_i64_, {v.value});
                break;
            case CGType::Double:
                builder_.CreateCall(rt_print_f64_, {v.value});
                break;
            case CGType::Num:
                // number 打印（t62）：垫片按 tag 分派 %lld / 四步小数格式
                builder_.CreateCall(rt_print_num_, {num_tag(v.value), num_bits(v.value)});
                break;
            case CGType::Dyn:
                // 动态标量打印（t138）：tag 扩 kind 2/3/6，垫片按 tag 分派
                // true/false、串内容、true/unset/false（对齐 Value::to_string）
                builder_.CreateCall(rt_print_num_, {num_tag(v.value), num_bits(v.value)});
                break;
            case CGType::Bool: {
                // i1 零扩展为 i32（C 接口参数为 int）
                llvm::Value* as_i32 = builder_.CreateZExt(v.value, builder_.getInt32Ty());
                builder_.CreateCall(rt_print_bool_, {as_i32});
                break;
            }
            case CGType::Tri:
                // tribool 打印（t65）：双 select 三常量串（零新增垫片接口）
                builder_.CreateCall(rt_print_str_, {to_str(v, expr.paren())});
                break;
            case CGType::Arr:
                // 数组打印（t59）：垫片转 [1, 2, 3] 格式串后原样输出
                builder_.CreateCall(
                    rt_print_str_,
                    {builder_.CreateCall(rt_arr_to_str_, {v.value}, "arrstr")});
                break;
            case CGType::Obj:
                // 实例打印固定 "<object>"（对齐解释器 Value::to_string Instance 分支）
                builder_.CreateCall(rt_print_str_,
                                    {builder_.CreateGlobalString("<object>")});
                break;
            case CGType::Fn:
                // 函数值打印固定 "<function>"（t143，对齐解释器 Value::to_string
                // Function 分支）
                builder_.CreateCall(rt_print_str_,
                                    {builder_.CreateGlobalString("<function>")});
                break;
            case CGType::Tup:
                // tuple 打印（t68）：静态展开拼接后整串输出（格式对齐
                // Value::to_string Tuple 分支）
                builder_.CreateCall(rt_print_str_, {tuple_to_str(v, expr.paren())});
                break;
            case CGType::TupDyn:
                // 动态 tuple 打印（t140）：垫片转 (1, 2) 格式整串输出
                builder_.CreateCall(
                    rt_print_str_,
                    {builder_.CreateCall(rt_tuple_to_str_, {v.value}, "tupdynstr")});
                break;
            case CGType::Void:
                // none 打印（t81）：常量串 "none"（对齐解释器 Value::to_string
                // None 分支；求值已于收集阶段发生，副作用保序）
                builder_.CreateCall(rt_print_str_,
                                    {builder_.CreateGlobalString("none")});
                break;
            default:
                unsupported("print of this value type", expr.paren().line(),
                            expr.paren().column());
        }
    }
    builder_.CreateCall(rt_print_newline_, {}); // 一行结束换行
}

void CodeGenerator::gen_logical(const BinaryExpr& expr) {
    // 短路降级（与解释器对齐）：&& 左侧为 false / || 左侧为 true 时右侧不求值；
    // bool/tribool 混域统一 i8 三态编码（False=0 < Unset=1 < True=2，t65）：
    // Kleene AND=umin / OR=umax 一步覆盖真值表，unset 不短路（与解释器一致，
    // 仅确定值短路）；任一侧 tribool 结果为 tribool，纯 bool 侧收窄回 i1
    const Token& op = expr.op();
    const bool is_and = op.type() == TokenType::OP_AND;

    CGValue lhs = emit(expr.left());
    if (lhs.type != CGType::Bool && lhs.type != CGType::Tri) {
        unsupported("non-bool operand of '" + std::string(op.lexeme()) + "'",
                    op.line(), op.column());
    }
    llvm::Value* l_tri = to_tri(lhs);
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* rhs_bb = llvm::BasicBlock::Create(context_, is_and ? "and.rhs" : "or.rhs", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, is_and ? "and.end" : "or.end", fn);
    llvm::BasicBlock* lhs_end = builder_.GetInsertBlock();
    // 短路条件：AND 左为 false（0）/ OR 左为 true（2）时右侧不求值
    llvm::Value* sc = builder_.CreateICmpEQ(
        l_tri, builder_.getInt8(is_and ? 0 : 2), is_and ? "and.sc" : "or.sc");
    builder_.CreateCondBr(sc, merge_bb, rhs_bb);

    // 条件发射区（t110）：短路右侧运行期可能不执行，区内 uninit 清除隔离
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编
    builder_.SetInsertPoint(rhs_bb);
    CGValue rhs = emit(expr.right());
    if (rhs.type != CGType::Bool && rhs.type != CGType::Tri) {
        unsupported("non-bool operand of '" + std::string(op.lexeme()) + "'",
                    op.line(), op.column());
    }
    // Kleene 合并：AND=min / OR=max（对齐解释器的 min-max 合并）
    llvm::Value* combined = builder_.CreateBinaryIntrinsic(
        is_and ? llvm::Intrinsic::umin : llvm::Intrinsic::umax, l_tri, to_tri(rhs));
    llvm::BasicBlock* rhs_end = builder_.GetInsertBlock(); // 右侧可能自带嵌套分支
    builder_.CreateBr(merge_bb);
    uninit_restore(uninit_snap);

    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(builder_.getInt8Ty(), 2,
                                            is_and ? "andtmp" : "ortmp");
    phi->addIncoming(l_tri, lhs_end); // 短路边：左值即结果
    phi->addIncoming(combined, rhs_end);
    if (lhs.type == CGType::Tri || rhs.type == CGType::Tri) {
        last_value_ = {phi, CGType::Tri};
    } else {
        // 纯 bool 域：值域 {0,2}，收窄回 i1（与既往 i1 短路降级输出等价）
        last_value_ = {builder_.CreateICmpEQ(phi, builder_.getInt8(2), "booltmp"),
                       CGType::Bool};
    }
}

// ---------------- 语句 ----------------

void CodeGenerator::visitExpression(const ExpressionStmt& stmt) {
    emit(stmt.expression()); // 值弃用（print 等以副作用为主）
}

// ---------------- 范围外节点：显式报错，绝不静默错编 ----------------

void CodeGenerator::visitIdentifier(const IdentifierExpr& expr) {
    const std::string name(expr.name().lexeme());
    CGVar* var = lookup_var(name);
    if (!var) {
        // 顶层函数名作值（t143）：functions_ 第一遍已建齐，裸名直查命中即
        // 出 Fn 值（改编键含 '.' 不会误命中嵌套/方法）；解释器 env 中函数
        // 名即函数值（value.h Kind::Function），print/toString/插值消费
        // "<function>"（Value::to_string Function 分支）
        auto fit = functions_.find(name);
        if (fit != functions_.end()) {
            last_value_ = {fit->second.fn, CGType::Fn};
            return;
        }
        unsupported("identifier '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    if (!var->fn_key.empty()) {
        // 嵌套函数绑定作值（t91 登记，t143 解锁消费）：出 Fn 值；间接调用
        // 经变量名语义层已拦（callee 需 FUNCTION 符号），非差分面
        auto fit = functions_.find(var->fn_key);
        if (fit == functions_.end()) {
            unsupported("function '" + name + "' used as a value",
                        expr.name().line(), expr.name().column());
        }
        last_value_ = {fit->second.fn, CGType::Fn};
        return;
    }
    if (var->uninit) {
        // 无初始化声明未经支配性定赋（t92/t110）：解释器此值为 none（流不
        // 敏感放行但运行期可能未经赋值），槽值静态不可知，拒编不错编
        unsupported("use of uninitialized variable '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    if (var->type == CGType::Tup) {
        // tuple 变量（t68）：无单一 slot，逐解构槽 load 重组展开值
        last_value_ = load_tuple_var(var->tup, name);
        return;
    }
    last_value_ = {builder_.CreateLoad(llvm_type_of(var->type), var->slot, name),
                   var->type, var->elem, var->cls};
}

void CodeGenerator::visitAssign(const AssignExpr& expr) {
    const std::string name(expr.name().lexeme());
    CGVar* var = lookup_var(name);
    if (!var) {
        unsupported("assignment to undeclared '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    if (!var->fn_key.empty()) {
        // 嵌套函数绑定（t91）：函数绑定不可被赋值覆盖，拒编不错编
        unsupported("assignment to function '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    CGValue v = emit(expr.value());
    if (var->type == CGType::Arr) {
        // 数组赋值即指针拷贝（引用语义对齐解释器，t59）；元素类型不同的数组
        // 拒编不错编（后续索引读写按变量记录的元素类型解码，错型会错值）
        if (v.type == CGType::Dyn) {
            // 动态承载值重赋进 array 变量（t165）：tag ∈ {4,7} 守卫后取数组 ptr，
            // 运行期 kind 随新对象自带——槽 elem 非 Num 时按 t106 异型互赋同规则
            // 降级为动态域哨兵（循环回边/全局跨函数快照两面守卫沿用），已是
            // Num（含无初始化声明 t101）则透传
            llvm::Value* ptr = dyn_to_arr_slot(v.value);
            if (var->elem != CGType::Num) {
                if (loops_.size() != var->loop_depth) {
                    unsupported("assigning array with different element type to '" +
                                    name + "' inside a loop",
                                expr.name().line(), expr.name().column());
                }
                if (llvm::isa<llvm::GlobalVariable>(var->slot) &&
                    (in_function_ || fn_gen_count_ != var->fn_gen_at)) {
                    unsupported("assigning array with different element type to "
                                "global '" +
                                    name + "' across functions",
                                expr.name().line(), expr.name().column());
                }
                var->elem = CGType::Num;
                var->cls.clear();
            }
            builder_.CreateStore(ptr, var->slot);
            var->uninit = false;
            last_value_ = {ptr, CGType::Arr, var->elem, var->cls};
            return;
        }
        if (v.type != CGType::Arr) {
            unsupported("assigning non-array value to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        if (var->elem == CGType::Num) {
            // 动态 elem 槽（t70/t88）：任意元素数组透传（kind 随数组对象自带，
            // print/len/== rt 侧全 kind 覆盖；索引读按运行期 kind 产出 Dyn，t151）
        } else if (v.elem != var->elem) {
            // 异型互赋（t106）：槽 elem 降级 Num 动态域哨兵（t94 先例）——
            // 整数组替换 kind 随新对象自带，后续读写走动态路径；两面拒编
            // 不错编：循环回边（赋值前已生成的静态读代码第二轮按旧 elem
            // 解码错值）与全局槽跨函数快照（函数体链底按值拷贝静态 elem，
            // 降级不回溯/不外传）
            if (loops_.size() != var->loop_depth) {
                unsupported("assigning array with different element type to '" +
                                name + "' inside a loop",
                            expr.name().line(), expr.name().column());
            }
            if (llvm::isa<llvm::GlobalVariable>(var->slot) &&
                (in_function_ || fn_gen_count_ != var->fn_gen_at)) {
                // 函数内写全局槽（链底副本降级不外传顶层）或声明后已有
                // 函数体快照旧 elem：拒编；声明在后的全局槽不受影响
                unsupported("assigning array with different element type to "
                            "global '" + name + "' across functions",
                            expr.name().line(), expr.name().column());
            }
            var->elem = v.elem == CGType::Dyn ? CGType::Dyn : CGType::Num;
            var->cls.clear();
        } else if (var->elem == CGType::Obj && v.cls != var->cls &&
                   !is_subclass_of(v.cls, var->cls) &&
                   !is_subclass_of(var->cls, v.cls)) {
            // 实例数组同继承树互赋放行（t115，镜像标量 Obj upcast/downcast）：
            // 同树内元素类沿链前缀布局兼容——Animal 级字段偏移前缀一致、
            // arr[i].field 按 var->cls 偏移解码；子类特有字段深度访问运行期
            // 陷阱退出不错值，方法调用按对象头类 id 动态分派。var->cls 保持
            // 不变（不收敛 NCA，与标量 Obj L1145 一致）。兄弟类（互不为子类）
            // 特有字段偏移错位可能错值，保守拒编不错编（t100 残余面）
            unsupported("assigning array with incompatible element class to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        builder_.CreateStore(v.value, var->slot);
        // 无初始化 array 变量赋值后清 uninit 放行后续读（t101/t110 定赋
        // 区域跟踪：条件发射区已快照隔离，此处无条件清）
        var->uninit = false;
        last_value_ = {v.value, CGType::Arr, var->elem, var->cls};
        return;
    }
    if (var->type == CGType::Obj) {
        // 实例赋值即指针拷贝（引用语义对齐解释器 shared_ptr，t60）；
        // 同一继承树内 upcast/downcast 均放行（t86/t103，解释器不校验类，
        // 成员访问按对象头类 id 动态分派/陷阱不错值）；跨树拒编不错编。
        // 本分支只服务类名声明变量（静态 cls 恒非空——object 声明变量无论
        // 有无初值均建 CGType::Dyn 槽走下方 Dyn 分支：无初值 t173、有初值
        // t174，原 t121「cls 空占位吸收 RHS 类名」、t143「首赋函数值重绑 Fn
        // 槽」与 t120「Obj 槽记静态初始类名」三支随表示切换退役）
        if (v.type == CGType::Dyn && !var->cls.empty()) {
            // 动态承载值重赋类名变量（t158）：同声明初始化规则——tag 5 且类 id
            // 属静态 cls 或其后代类才还原实例 ptr（守卫不中陷阱退出），cls 保持
            // 声明类不变
            llvm::Value* ptr = dyn_unbox_to_obj(v.value, var->cls,
                                                expr.name().line(),
                                                expr.name().column());
            builder_.CreateStore(ptr, var->slot);
            var->uninit = false;
            last_value_ = {ptr, CGType::Obj, CGType::Int, var->cls};
            return;
        }
        if (v.type != CGType::Obj ||
            (v.cls != var->cls && !is_subclass_of(v.cls, var->cls) &&
             !is_subclass_of(var->cls, v.cls))) {
            unsupported("assigning incompatible value to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        builder_.CreateStore(v.value, var->slot);
        // 无初始化类类型变量赋值后清 uninit 放行后续读（t101/t110 定赋
        // 区域跟踪：条件发射区已快照隔离，此处无条件清）
        var->uninit = false;
        last_value_ = {v.value, CGType::Obj, CGType::Int, var->cls};
        return;
    }
    if (var->type == CGType::Dyn) {
        // object 动态承载槽重赋（t146）：标量/Dyn/Obj（tag 5）统一经 to_dyn
        // 组装整槽替换，解释器 coerce default 直存任意值同规则；数组位放行
        // （t160）：tag 4 + 内层数组 ptr 位模式；tuple 位放行（t170）：tag 8 +
        // 物化后的堆对象 ptr；函数位放行（t171）：tag 9；空位放行（t175）：
        // tag 10 + bits 恒 0（`none` 调用副作用已在 emit 时发生）
        if (!is_dyn_carrier(v.type)) {
            unsupported("assigning incompatible value to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        llvm::Value* boxed =
            to_dyn(v, expr.name().line(), expr.name().column());
        builder_.CreateStore(boxed, var->slot);
        var->uninit = false;
        last_value_ = {boxed, CGType::Dyn};
        return;
    }
    if (var->type == CGType::Tup) {
        // tuple 变量重赋值（t68）：同形状逐槽写；换形状重建槽组（t117）
        //（无初值 Tuple 槽已改 TupDyn 动态承载，t181，原 tup<0 首赋建组支退役）
        if (v.type != CGType::Tup) {
            unsupported("assigning non-tuple value to tuple variable '" +
                            name + "'",
                        expr.name().line(), expr.name().column());
        }
        if (!tuple_shape_matches(var->tup, v.tup)) {
            // 换形状重赋（t117，解释器整值替换动态换形状）：重建解构槽组
            //（create_tuple_var 递归建全部子槽，嵌套换形状一并解决），旧槽组
            // 成死存储不影响运行期输出。两守卫拒编不错编：① 条件发射区内
            //（分支/循环体/三目臂等，cond_depth_ > 0）——区外读按生成时形状
            // 解码，区内换形状后形状运行期才定；② 全局槽组跨函数——函数体
            // 链底快照旧槽组（t76），函数内换形状/声明后已有函数体（t106
            // fn_gen_at 同规则）均不回溯，重建新全局符号后旧函数体按旧槽组
            // 解码错值。循环回边无需单独守卫：循环体即条件发射区，且重建
            // 是新槽组、旧读旧槽互不污染（与 Arr 共享单 ptr 槽不同）
            if (cond_depth_ > 0) {
                unsupported("assigning tuple of different shape to '" + name +
                                "' inside a conditional region",
                            expr.name().line(), expr.name().column());
            }
            if (has_global_slot(var->tup) &&
                (in_function_ || fn_gen_count_ != var->fn_gen_at)) {
                unsupported("assigning tuple of different shape to global '" +
                                name + "' across functions",
                            expr.name().line(), expr.name().column());
            }
            var->tup = create_tuple_var(
                tuple_values_[v.tup],
                name + "#" + std::to_string(tuple_rebuild_seq_++));
            var->uninit = false;
            last_value_ = {nullptr, CGType::Tup, CGType::Int, "", v.tup};
            return;
        }
        int idx = store_tuple_var(var->tup, v, expr.name());
        var->uninit = false;
        last_value_ = {nullptr, CGType::Tup, CGType::Int, "", idx};
        return;
    }
    // byte/word 变量（t69）的 Dyn 拆槽消息用声明类型名（t147），范围校验随后接续
    llvm::Value* stored = coerce_for_slot(v, var->type, expr.name(), "", true,
                                          bit_type_name(var->bit_max));
    if (var->bit_max > 0) {
        // byte/word 变量赋值点范围陷阱（t69，对齐解释器 coerce_to_declared）
        stored = check_bit_range(stored, var->bit_max,
                                 var->bit_max == 255 ? "byte" : "word");
    }
    builder_.CreateStore(stored, var->slot);
    // 定赋区域跟踪（t92/t110）：发射顺序即支配顺序，赋值后同区域后续读
    // 运行期必已过此存储，无条件清；条件发射区（分支/循环体/三目臂等）
    // 由 uninit_snapshot/restore 隔离，区内清除不外泄
    var->uninit = false;
    // 赋值表达式的值 = 存入后的值（与解释器一致）
    last_value_ = {stored, var->type};
}

void CodeGenerator::visitTuple(const TupleExpr& expr) {
    // 元组字面量（t68）：纯静态展开——元素求值后连同名字表登记注册表，
    // 无运行时对象（虚值 value=nullptr）；元素类型/个数/名字编译期全可知
    //（语义层对 tuple 元素零追踪，codegen 自建，对齐解释器 visitTuple 求值顺序）
    CGTuple t;
    t.elems.reserve(expr.elements().size());
    for (const auto& element : expr.elements()) {
        CGValue v = emit(element.get());
        if (v.type == CGType::Void) {
            // `none` 函数返回值作 tuple 元素（t175）：解释器直存 Kind::None 元素
            // 并可打印/索引/判等，此处就地盒化为 tag 10 的 Dyn 元素（副作用已在
            // emit 时发生），后续消费全落运行期 tag 分派臂
            v = {to_dyn(v, expr.paren().line(), expr.paren().column()),
                 CGType::Dyn};
        }
        t.elems.push_back(v);
    }
    t.names = expr.names();
    last_value_ = {nullptr, CGType::Tup, CGType::Int, "", register_tuple(std::move(t))};
}
void CodeGenerator::visitTernary(const TernaryExpr& expr) { gen_ternary(expr); }
void CodeGenerator::visitMultiMatch(const MultiMatchExpr& expr) { gen_multi_match(expr); }
void CodeGenerator::visitArrayLiteral(const ArrayLiteralExpr& expr) {
    // 数组字面量（t59）：语义层不追踪元素类型（一刀切 KW_ARRAY），codegen 自行
    // 做同质推断——Int/Double 混合整体提升 Double（提升后输出与解释器一致：整值
    // double 按整数打印），其余混合拒编不错编；空字面量元素类型记
    // Int（print 得 []，索引必越界报错，行为与解释器一致）。
    // 嵌套数组（t85/t89）：全 Arr 元素放行为 kind 4（槽存内层数组 ptr 位模式），
    // 内层元素任意（bool/str/更深嵌套均可，t89 放宽）——print/==/整槽替换
    // rt 侧全 kind 递归覆盖；内层经动态域索引读出按运行期 kind 产出 Dyn（t151）
    const Token& bracket = expr.bracket();
    std::vector<CGValue> elements;
    elements.reserve(expr.elements().size());
    CGType elem = CGType::Int;
    std::string elem_cls; // 仅 elem==Obj 有意义：元素类名（t100 实例数组，复用 CGValue.cls）
    for (const auto& element : expr.elements()) {
        CGValue v = emit(element.get());
        if (v.type == CGType::Void) {
            // `none` 函数返回值作数组元素（t175）：解释器数组逐元素保型可直存
            // Kind::None（`[nop(), 1]` 打印 `[none, 1]`），此处就地盒化为 tag 10
            // 的 Dyn 元素并入 kind 7 双步长槽；副作用已在 emit 该调用时发生
            v = {to_dyn(v, bracket.line(), bracket.column()), CGType::Dyn};
        }
        if (elements.empty()) {
            // 元素全为 tuple / 函数（t170/t171）：静态 tuple 无单值槽表示、函数
            // 无数组 kind，直接收敛 kind 7 双步长槽（tag 8 承载堆对象位、tag 9
            // 承载函数 ptr 位），读出即 Dyn
            elem = (v.type == CGType::Tup || v.type == CGType::TupDyn ||
                    v.type == CGType::Fn)
                       ? CGType::Dyn
                       : v.type;
            elem_cls = v.cls;
        } else if (v.type != elem) {
            if (is_dyn_carrier(elem) && is_dyn_carrier(v.type)) {
                // 异质基本类型混排（t139）+ 类实例混入（t144）+ 数值系互混与
                // bool×tribool 互混（t159：Int/Double/Num 不再整体拍平为 Double
                // 视图、bool/tribool 不再整体加宽为 Tri）+ 嵌套数组混入（t160：
                // `[1, [2, 3], "x"]` 的数组位 tag 4）：收敛 elem=Dyn（运行时
                // kind 7 双步长 {tag,bits} 槽），解锁 print/len/索引读/== 显示与
                // 比较面；元素按运行期 tag 保型对齐解释器逐元素保型（integer i =
                // [3, 4.5][0]、bool x = [unset, true][1] 不再因静态最宽 kind 视图
                // 失配，标量位上的方法调用也不再答出解释器运行期报错的结果），
                // 整值 double 省 .0 的 print 输出仍一致；实例元素 tag 5、数组元素
                // tag 4 均存 ptr 位模式（镜像 t100 kind 5 / t85 kind 4 单数组
                // elem_to_bits 表示），读出为 Dyn、print/toString/插值走 rt 按
                // tag 分派（数组位递归 arr_to_str、实例位 <object>），== 经 dyn_eq
                // 数组位递归深比较、实例位恒不等（对齐解释器 values_equal）；
                // 后续元素经 is_dyn_scalar(Dyn)/is_dyn_carrier(Dyn) 持续吸收，
                // 承载集（含空位 tag 10，t175）之外的元素维持拒编不错编——
                // is_dyn_scalar 本体不收 Obj/Arr，
                // tuple 物化处（t140）的「含数组元素」面不受本面影响
                elem = CGType::Dyn;
            } else {
                unsupported("heterogeneous array literal",
                            bracket.line(), bracket.column());
            }
        } else if (elem == CGType::Obj && v.cls != elem_cls) {
            // 实例数组混合类字面量（t100 同类约束 → t119 NCA 收敛）：类不同
            // 时求最近公共祖先——有则元素类收敛到 NCA（各元素类沿链前缀
            // 布局兼容：字段按 NCA 偏移解码、方法按对象头 id 动态分派，与
            // t115/t118 同规则）；无公共祖先（跨树无公共字段前缀）收敛到
            // cls 空的动态 object 元素（t141，对齐解释器数组动态存任意实例）：
            // print/len/== 经 rt kind 5 全承载、索引读出落空 cls → 方法调用走
            // t133 全体定义者分派、字段访问维持拒编（无公共前缀不可静态解码）
            const std::string nca = nearest_common_ancestor(elem_cls, v.cls);
            elem_cls = nca; // 空串=动态 object 元素
        }
        elements.push_back(v);
    }
    if (elem == CGType::Num) {
        // 全 Num 字面量（t90 落 double 视图 → t159 改走 kind 7）：Num 的运行期
        // tag 静态不可判，double 槽会把整数位定宽成小数（integer i = [x, y][0]
        // 两端分歧）；按元素保型透传 {tag,bits} 对齐解释器逐元素保型
        elem = CGType::Dyn;
    }
    llvm::Value* arr = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(elements.size())),
         builder_.getInt64(static_cast<uint64_t>(arr_kind_of(elem)))},
        "arrnew");
    for (size_t i = 0; i < elements.size(); ++i) {
        CGValue v = elements[i];
        if (elem == CGType::Dyn) {
            // 异质动态标量写（t139）：静态标量元素打常量 tag + elem_to_bits；
            // Num/Dyn 取运行期 num_tag/num_bits（Dyn 元素透传，支持二次物化）；
            // Obj 元素走 else 分支常量 tag 5 + 实例 ptr 位模式（t144）
            llvm::Value* tag = nullptr;
            llvm::Value* bits = nullptr;
            if (v.type == CGType::Num || v.type == CGType::Dyn) {
                tag = num_tag(v.value);
                bits = num_bits(v.value);
            } else {
                // 单点走 to_dyn：标量按 arr_kind_of、实例位 tag 5、数组位 tag 4、
                // tuple 位 tag 8（t170 静态 tuple 先物化），与承载集判定同源
                llvm::Value* vd = to_dyn(v, bracket.line(), bracket.column());
                tag = num_tag(vd);
                bits = num_bits(vd);
            }
            builder_.CreateCall(
                rt_arr_set_dyn_,
                {arr, builder_.getInt64(static_cast<uint64_t>(i)), tag, bits});
            continue;
        }
        // 同质字面量（elem 与元素类型一致）：直接写单值槽（t159 后任何跨 kind
        // 混排都收敛 kind 7，此处不再有整体加宽分支）
        builder_.CreateCall(rt_arr_set_,
                            {arr, builder_.getInt64(i), elem_to_bits(v)});
    }
    last_value_ = {arr, CGType::Arr, elem, elem_cls}; // Arr 复用 cls 记元素类名（t100）
}
void CodeGenerator::visitIndex(const IndexExpr& expr) {
    // string 索引（S8 t56）/数组索引（t59）：负索引与越界报错在 collie_rt
    // 运行期（对齐解释器 normalize_index）；tuple 索引待对应类型 codegen 支持，
    // 非 Int 索引拒编不错编
    CGValue object = emit(expr.object());
    if (object.type == CGType::TupDyn) {
        // 动态 tuple 索引（t140）：形状运行期才定，非常量/常量索引同路——
        // rt_tuple_get_dyn 一次取回 {tag,bits} 拼 CGType::Dyn（负索引归一化/
        // 越界陷阱 "Index N out of range (size M)" 均在 rt 内，与解释器一致）；
        // 结果仅 print/toString/插值可消费（Dyn 既有面），算术/进槽经 default 拒编
        CGValue index = emit(expr.index());
        index.value = index_to_int(index, expr.bracket().line(), expr.bracket().column());
        index.type = CGType::Int;
        llvm::AllocaInst* otag =
            create_entry_alloca(builder_.getInt64Ty(), "tupdyn.otag");
        llvm::AllocaInst* obits =
            create_entry_alloca(builder_.getInt64Ty(), "tupdyn.obits");
        builder_.CreateCall(rt_tuple_get_dyn_,
                            {object.value, index.value, otag, obits});
        last_value_ = {make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag),
                                builder_.CreateLoad(builder_.getInt64Ty(), obits)),
                       CGType::Dyn};
        return;
    }
    if (object.type == CGType::Tup) {
        // tuple 常量索引（t68）：编译期解析（含负索引归一化；越界解释器
        // 为运行期报错，codegen 静态可判，拒编不错编）
        // 按值拷贝不留引用：非常量路径下方 emit(index) 可能触发 register_tuple
        // 扩容 tuple_values_，持引用会悬垂（与 gen_tuple_eq 同一防护）
        const CGTuple t = tuple_values_[object.tup];
        const long long n = static_cast<long long>(t.elems.size());
        long long idx = 0;
        if (const_int_of(expr.index(), idx)) {
            if (idx < 0) idx += n; // 负索引归一化（对齐解释器 normalize_index）
            if (idx < 0 || idx >= n) {
                unsupported("tuple index out of range",
                            expr.bracket().line(), expr.bracket().column());
            }
            last_value_ = t.elems[static_cast<size_t>(idx)];
            return;
        }
        // 非常量索引（t83）：限同质 tuple（所有元素同 CGType 且 ∈
        // {Int/Double/Bool/Str}）——物化为运行时数组后 rt_arr_get(动态 idx) 取值，
        // 复用负索引归一化 + 越界陷阱（消息 "Index N out of range (size M)"
        // 与解释器 normalize_index 一致）；结果类型即元素类型，静态可定。
        // 数值系异质（t107）：元素全 ∈ {Int/Double/Num}（含全 Num 同质）——
        // 逐元素 to_num 后物化 tags+bits 双 int 数组，同一索引两次 rt_arr_get
        // 拼 Num 动态值（首次即越界陷阱，零新增 rt 接口）；结果型 Num，
        // 下游算术/比较/打印走既有 Num 路径。含 Bool/Str/Tri 的异质与同质
        // tribool（t138）：物化为 CGType::Dyn 动态标量（tag 复用 kind 编码），
        // 仅 print/toString/插值可消费，其余消费点默认拒编。含嵌套
        // (Tup/Arr/Obj) 的异质、空 tuple 保持拒编——结果类型静态不可定且
        // 无统一表示，拒编不错编
        if (t.elems.empty()) {
            unsupported("non-constant index on empty tuple",
                        expr.bracket().line(), expr.bracket().column());
        }
        const auto is_numeric = [](CGType ty) {
            return ty == CGType::Int || ty == CGType::Double || ty == CGType::Num;
        };
        const auto is_scalar = [](CGType ty) {
            // 承载位判定（t172 起与 is_dyn_carrier 同集）：标量 + Fn（tag 9，
            // t171）+ Arr/Obj（tag 4/5）+ Tup/TupDyn（tag 8，t170）均可入
            // tags+bits 物化路径，异质 tuple 的非常量索引/取键读出即 Dyn
            return ty == CGType::Int || ty == CGType::Double || ty == CGType::Num ||
                   ty == CGType::Bool || ty == CGType::Str || ty == CGType::Tri ||
                   ty == CGType::Dyn || ty == CGType::Fn || ty == CGType::Arr ||
                   ty == CGType::Obj || ty == CGType::Tup || ty == CGType::TupDyn;
        };
        bool homogeneous = true;
        bool all_numeric = true;
        bool all_scalar = true;
        const CGType elem = t.elems.front().type;
        for (const auto& e : t.elems) {
            if (e.type != elem) homogeneous = false;
            if (!is_numeric(e.type)) all_numeric = false;
            if (!is_scalar(e.type)) all_scalar = false;
        }
        // Dyn 承载判定（t138）：非单数组路径、非数值系，但元素全为可承载位
        // ——tags+bits 物化为 CGType::Dyn 后走 S92–S118 全部运行期 tag 分派消费；
        // Void 等无 {tag,bits} 编码者仍拒编不错编。同质跨树实例位（t172）静态
        // 给不出统一 cls，同样落本路径（读出 tag 5 后按对象头类 id 分派）
        const bool obj_static_merge = elem != CGType::Obj || !homogeneous ||
                                      !tuple_obj_merged_cls(t).empty();
        const bool single_array =
            obj_static_merge && homogeneous &&
            (elem == CGType::Int || elem == CGType::Double ||
             elem == CGType::Bool || elem == CGType::Str ||
             elem == CGType::Arr || elem == CGType::Obj);
        const bool use_dyn = !single_array && !all_numeric && all_scalar;
        if (!single_array && !all_numeric && !use_dyn) {
            if (homogeneous) {
                // 同质不可承载元素（Void 等）：单数组路径已排除同质 Arr/Obj（t114）
                unsupported("non-constant tuple index on this element type",
                            expr.bracket().line(), expr.bracket().column());
            }
            unsupported("non-constant index on heterogeneous tuple",
                        expr.bracket().line(), expr.bracket().column());
        }
        CGValue index = emit(expr.index());
        index.value =
            index_to_int(index, expr.bracket().line(), expr.bracket().column());
        index.type = CGType::Int;
        if (use_dyn) {
            // Dyn 路径（t138）：tags+bits 双数组同一索引两次取回拼动态标量
            // （负索引归一化/越界陷阱在首次 get，零新增 rt 接口）
            auto [tags, bits_arr] = materialize_dyn_scalars(
                t, expr.bracket().line(), expr.bracket().column());
            llvm::Value* tag =
                builder_.CreateCall(rt_arr_get_, {tags, index.value}, "dyntag");
            llvm::Value* bits =
                builder_.CreateCall(rt_arr_get_, {bits_arr, index.value}, "dynbits");
            last_value_ = {make_num(tag, bits), CGType::Dyn};
            return;
        }
        if (!homogeneous || elem == CGType::Num) {
            // 数值系路径（t107）：tags+bits 双数组（均 kind 0 int），同一
            // 索引两次取回拼 Num（负索引归一化/越界陷阱在首次 get）
            llvm::Value* tags = builder_.CreateCall(
                rt_arr_new_,
                {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(0)},
                "tuptags");
            llvm::Value* bits_arr = builder_.CreateCall(
                rt_arr_new_,
                {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(0)},
                "tupbits");
            for (long long i = 0; i < n; ++i) {
                llvm::Value* num = to_num(t.elems[static_cast<size_t>(i)]);
                builder_.CreateCall(
                    rt_arr_set_,
                    {tags, builder_.getInt64(static_cast<uint64_t>(i)), num_tag(num)});
                builder_.CreateCall(
                    rt_arr_set_,
                    {bits_arr, builder_.getInt64(static_cast<uint64_t>(i)), num_bits(num)});
            }
            llvm::Value* tag =
                builder_.CreateCall(rt_arr_get_, {tags, index.value}, "tuptag");
            llvm::Value* bits =
                builder_.CreateCall(rt_arr_get_, {bits_arr, index.value}, "tupbit");
            last_value_ = {make_num(tag, bits), CGType::Num};
            return;
        }
        llvm::Value* arr = builder_.CreateCall(
            rt_arr_new_,
            {builder_.getInt64(static_cast<uint64_t>(n)),
             builder_.getInt64(static_cast<uint64_t>(arr_kind_of(elem)))},
            "tuparr");
        for (long long i = 0; i < n; ++i) {
            builder_.CreateCall(
                rt_arr_set_,
                {arr, builder_.getInt64(static_cast<uint64_t>(i)),
                 elem_to_bits(t.elems[static_cast<size_t>(i)])});
        }
        llvm::Value* bits =
            builder_.CreateCall(rt_arr_get_, {arr, index.value}, "tupget");
        last_value_ = tuple_dynamic_result(bits, elem, t, expr.bracket().line(),
                                           expr.bracket().column());
        return;
    }
    if (object.type != CGType::Str && object.type != CGType::Arr &&
        object.type != CGType::Dyn) {
        unsupported("indexing this value type",
                    expr.bracket().line(), expr.bracket().column());
    }
    CGValue index = emit(expr.index());
    index.value =
        index_to_int(index, expr.bracket().line(), expr.bracket().column());
    index.type = CGType::Int;
    if (object.type == CGType::Dyn) {
        // 动态承载值作索引接收者（t153）：嵌套容器索引 `a[i][j]` 的内层接收者、
        // object 动态槽与动态 tuple 索引读出的值——tag 运行期才定，按解释器
        // visitIndex 接受表下沉 rt 分派（3=串按 UTF-8 码点索引返单码点子串、
        // 4/7=数组按元素 kind 读、其余位落 "Only arrays, strings and tuples can
        // be indexed, got <kind>" 陷阱）；负索引归一化与越界陷阱复用 rt 既有路径，
        // 结果统一产出 CGType::Dyn（串位打 tag 3），消费面全走 t147–t152 既有 Dyn 臂
        llvm::AllocaInst* otag =
            create_entry_alloca(builder_.getInt64Ty(), "dynidx.otag");
        llvm::AllocaInst* obits =
            create_entry_alloca(builder_.getInt64Ty(), "dynidx.obits");
        builder_.CreateCall(rt_dyn_index_,
                            {num_tag(object.value), num_bits(object.value),
                             index.value, otag, obits});
        last_value_ = {make_num(
                           builder_.CreateLoad(builder_.getInt64Ty(), otag, "dynidxtag"),
                           builder_.CreateLoad(builder_.getInt64Ty(), obits, "dynidxbits")),
                       CGType::Dyn};
        return;
    }
    if (object.type == CGType::Arr) {
        if (object.elem == CGType::Dyn) {
            // 异质动态标量数组读（t139，kind 7）：rt_arr_get_dyn 一次取回
            // {tag,bits} 拼 CGType::Dyn——负索引归一化/越界陷阱均在 rt 内；
            // 消费面同 t138 tuple Dyn：print/toString/插值/再入异质数组写放行，
            // 算术/比较/进类型化槽/进签名经既有 default 面拒编不错编
            llvm::AllocaInst* otag =
                create_entry_alloca(builder_.getInt64Ty(), "dyn.otag");
            llvm::AllocaInst* obits =
                create_entry_alloca(builder_.getInt64Ty(), "dyn.obits");
            builder_.CreateCall(rt_arr_get_dyn_,
                                {object.value, index.value, otag, obits});
            last_value_ = {make_num(
                               builder_.CreateLoad(builder_.getInt64Ty(), otag, "dyntag"),
                               builder_.CreateLoad(builder_.getInt64Ty(), obits, "dynbits")),
                           CGType::Dyn};
            return;
        }
        // 8 字节槽位模式按元素类型解码（字面量同质推断/变量槽记录，t59）
        llvm::Value* bits = builder_.CreateCall(
            rt_arr_get_, {object.value, index.value}, "arrget");
        if (object.elem == CGType::Arr) {
            // 嵌套数组内层读（t85/t89）：槽存内层数组 ptr 位模式，还原后
            // elem 记 Num 动态域哨兵——内层 kind 可为任意（t89 放宽），
            // 内层索引读同样按运行期 kind 产出 Dyn（t151 消除 CG9），写侧
            // tag/kind 失配仍落 CG7 陷阱；
            // print/len/== 全 kind 天然工作
            llvm::Value* inner = builder_.CreateIntToPtr(
                bits, llvm::PointerType::getUnqual(context_), "inner");
            last_value_ = {inner, CGType::Arr, CGType::Num};
            return;
        }
        if (object.elem == CGType::Num) {
            // 动态域读（t70/t88 引入，t151 消除缺口 CG9）：运行时 kind 决定承载
            // 形态——kind 0/1 与 number tag 编码重合，bits 原样即数值位；kind 2/3/
            // 5/6（bool/str/实例/tribool）槽位模式与 Dyn 的 bits 编码同源；kind 7 为
            // 双步长 {tag,bits} 槽，须经 rt_arr_get_dyn 取回真实元素。四路统一产出
            // CGType::Dyn（{tag,bits} 与 Num 同构，数值位读出行为不变），消费面全走
            // Dyn 既有臂：print/toString/插值/拼接（rt 按 tag 分派，t151 补 tag 4/7
            // → 数组转串）、==/!=（t149 dyn_eq，数组走 arr_eq 深比较）、算术（t148
            // tag<2 守卫）、比较（t149）、作下标（t150）、进标量类型化槽/形参/返回
            // （t147 拆槽）——原 kind ≥ 2 陷阱退出面（解释器动态类型可行）经此解锁
            llvm::Value* kind = builder_.CreateCall(
                rt_arr_kind_, {object.value}, "arrkind");
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            auto* dyn_bb = llvm::BasicBlock::Create(context_, "dynkind.seven", fn);
            auto* plain_bb = llvm::BasicBlock::Create(context_, "dynkind.plain", fn);
            auto* cont_bb = llvm::BasicBlock::Create(context_, "dynkind.cont", fn);
            llvm::Value* is7 = builder_.CreateICmpEQ(
                kind, builder_.getInt64(7), "dynkind.is7");
            builder_.CreateCondBr(is7, dyn_bb, plain_bb);
            builder_.SetInsertPoint(dyn_bb);
            llvm::AllocaInst* otag =
                create_entry_alloca(builder_.getInt64Ty(), "d7.otag");
            llvm::AllocaInst* obits =
                create_entry_alloca(builder_.getInt64Ty(), "d7.obits");
            builder_.CreateCall(
                rt_arr_get_dyn_, {object.value, index.value, otag, obits});
            llvm::Value* d7tag =
                builder_.CreateLoad(builder_.getInt64Ty(), otag, "d7.tag");
            llvm::Value* d7bits =
                builder_.CreateLoad(builder_.getInt64Ty(), obits, "d7.bits");
            builder_.CreateBr(cont_bb);
            builder_.SetInsertPoint(plain_bb);
            builder_.CreateBr(cont_bb);
            builder_.SetInsertPoint(cont_bb);
            llvm::PHINode* dyn_tag =
                builder_.CreatePHI(builder_.getInt64Ty(), 2, "dynkind.tag");
            dyn_tag->addIncoming(d7tag, dyn_bb);
            dyn_tag->addIncoming(kind, plain_bb);
            llvm::PHINode* dyn_bits =
                builder_.CreatePHI(builder_.getInt64Ty(), 2, "dynkind.bits");
            dyn_bits->addIncoming(d7bits, dyn_bb);
            dyn_bits->addIncoming(bits, plain_bb);
            last_value_ = {make_num(dyn_tag, dyn_bits), CGType::Dyn};
            return;
        }
        last_value_ = {bits_to_elem(bits, object.elem), object.elem,
                       CGType::Int, object.cls}; // Obj 元素传播元素类名（t100，支持 arr[i].field/method()）
        return;
    }
    last_value_ = {builder_.CreateCall(rt_str_index_, {object.value, index.value}, "idxtmp"),
                   CGType::Str};
}
void CodeGenerator::visitIndexAssign(const IndexAssignExpr& expr) {
    // 数组索引赋值（t59）：求值顺序对齐解释器（object → index → value），
    // 写入共享底层存储（引用语义）；值仅 Int→Double 隐式提升，其余元素类型
    // 不匹配拒编不错编（解释器数组元素动态异质，codegen 同质表示无法承载）；
    // string/tuple 的索引赋值语义层已拦
    CGValue object = emit(expr.object());
    if (object.type != CGType::Arr && object.type != CGType::Dyn) {
        unsupported("index assignment on this value type",
                    expr.bracket().line(), expr.bracket().column());
    }
    CGValue index = emit(expr.index());
    index.value =
        index_to_int(index, expr.bracket().line(), expr.bracket().column());
    index.type = CGType::Int;
    CGValue v = emit(expr.value());
    if (object.type == CGType::Dyn) {
        // 动态承载值索引写（t153）：`a[i][j] = v` 的内层接收者为 Dyn（动态域
        // 嵌套数组元素、kind 7 元素、object 动态槽等），tag 运行期才定——按解释器
        // visitIndexAssign 接受表下沉 rt 分派（4/7=数组写，复用 arr_set_num 的
        // kind 对齐、int→dec 提升与 CG7 失配陷阱；3=串不可变陷阱；其余=
        // "Only arrays can be index-assigned, got <kind>" 陷阱）；写入值统一经
        // to_dyn 打 tag（Arr 值打 tag 4 存内层数组 ptr，读回即 Dyn）。值类型
        // 守卫与 kind 7 写侧同源（is_dyn_carrier）：静态可判的非承载值维持拒编不错编
        if (!is_dyn_carrier(v.type)) {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
        llvm::Value* vd = to_dyn(v);
        builder_.CreateCall(rt_dyn_set_,
                            {num_tag(object.value), num_bits(object.value),
                             index.value, num_tag(vd), num_bits(vd)});
        last_value_ = v;
        return;
    }
    if (object.elem == CGType::Dyn) {
        // 异质动态标量数组整槽写（t139，kind 7）：任意基本标量写入自带 tag，
        // 解释器异质数组本就容任意元素；Dyn 值透传分量，静态标量打常量 tag。
        // 实例元素 tag 5、数组元素 tag 4、tuple 元素 tag 8（t170 先物化）均存 ptr
        // 位模式，读出为 Dyn、print/toString/插值走 rt 按 tag 分派
        if (!is_dyn_carrier(v.type)) {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
        llvm::Value* tag = nullptr;
        llvm::Value* bits = nullptr;
        if (v.type == CGType::Num || v.type == CGType::Dyn) {
            tag = num_tag(v.value);
            bits = num_bits(v.value);
        } else {
            // 单点 to_dyn：标量按 arr_kind_of、实例位 tag 5、数组位 tag 4、
            // tuple 位 tag 8（t170，静态 tuple 先物化）
            llvm::Value* vd = to_dyn(v, expr.bracket().line(),
                                     expr.bracket().column());
            tag = num_tag(vd);
            bits = num_bits(vd);
        }
        builder_.CreateCall(rt_arr_set_dyn_,
                            {object.value, index.value, tag, bits});
        last_value_ = v;
        return;
    }
    if (object.elem == CGType::Arr) {
        // 嵌套数组整槽替换（t85/t89）：外层槽写入新内层数组 ptr 位模式；
        // 值须为数组（任意元素，t89 放宽——kind 随内层对象自带），
        // 非数组值写 kind 4 槽拒编不错编
        if (v.type == CGType::Dyn) {
            // 动态承载数组位写 kind 4 槽（t166）：tag 运行期才定——交 t165 的
            // dyn_to_arr_slot 守卫 tag ∈ {4, 7} 后还原内层数组 ptr 再写槽；
            // 非数组位（解释器异质数组可直存任意值）落同函数 array 落点陷阱，
            // 属残余面而非错值
            llvm::Value* inner = dyn_to_arr_slot(v.value);
            builder_.CreateCall(
                rt_arr_set_,
                {object.value, index.value,
                 builder_.CreatePtrToInt(inner, builder_.getInt64Ty(),
                                         "arrelembits")});
            last_value_ = v;
            return;
        }
        if (v.type != CGType::Arr) {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
        builder_.CreateCall(rt_arr_set_,
                            {object.value, index.value, elem_to_bits(v)});
        last_value_ = v;
        return;
    }
    if (object.elem == CGType::Num) {
        // 动态域写（t70/t88）：数值系值转 Num 表示下沉 rt 按运行时 kind 对齐
        //（tag==kind 直存 / int→double 提升 / mismatch 陷阱 CG7）；bool/str
        // 值打对应 kind tag 直写（t88，rt 侧 tag==kind 直存、mismatch 同陷阱）
        if (v.type == CGType::Bool || v.type == CGType::Str ||
            v.type == CGType::Tri) {
            // tribool 值动态写（t136）：tag 6 与 kind 6 直存对齐；写非
            // tribool 数组落 tag/kind 失配陷阱（CG7 面，同 bool/str）
            builder_.CreateCall(
                rt_arr_set_num_,
                {object.value, index.value,
                 builder_.getInt64(static_cast<uint64_t>(arr_kind_of(v.type))),
                 elem_to_bits(v)});
            last_value_ = v;
            return;
        }
        if (v.type == CGType::Dyn) {
            // Dyn 值写动态域（t151）：读出即 {tag,bits}，与 Num 同构，分量直传
            // rt_arr_set_num 按数组实际 kind 对齐（tag==kind 直存，失配落 CG7
            // 陷阱）——动态域读→算术→写回的就地修改链路无需静态类型知识
            builder_.CreateCall(rt_arr_set_num_,
                                {object.value, index.value,
                                 num_tag(v.value), num_bits(v.value)});
            last_value_ = v;
            return;
        }
        if (v.type == CGType::Obj || v.type == CGType::Arr ||
            v.type == CGType::Tup || v.type == CGType::TupDyn ||
            v.type == CGType::Fn || v.type == CGType::Void) {
            // 静态实例位/数组位/tuple 位/函数位/空位写动态域（t167，t170 扩 tuple 位、
            // t171 扩函数位、t175 扩空位）：接收者
            // elem 为 Num 哨兵时数组的运行期 kind 静态不可定（类字段/形参/无初始化
            // 声明等来源），故交 to_dyn 打 tag 5/4/8/9/10 后与 t151 同路——tag==kind 直存
            // ptr 位模式（内层 kind 随新数组对象自带，引用语义不变）、kind 7 落双步长
            // {tag,bits} 槽、失配落 CG7 陷阱；读回即 Dyn，消费全落 S92–S121 既有
            // tag 分派臂
            llvm::Value* vd = to_dyn(v, expr.bracket().line(),
                                     expr.bracket().column());
            builder_.CreateCall(rt_arr_set_num_,
                                {object.value, index.value, num_tag(vd),
                                 num_bits(vd)});
            last_value_ = v;
            return;
        }
        if (v.type != CGType::Int && v.type != CGType::Double &&
            v.type != CGType::Num) {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
        llvm::Value* num = to_num(v);
        builder_.CreateCall(rt_arr_set_num_,
                            {object.value, index.value, num_tag(num), num_bits(num)});
        last_value_ = v;
        return;
    }
    if (v.type != object.elem) {
        if (object.elem == CGType::Double && v.type == CGType::Int) {
            v = {to_double(v), CGType::Double};
        } else if (object.elem == CGType::Tri && v.type == CGType::Bool) {
            // bool 写 tribool 槽（t136）：to_tri 加宽后直存（0/2 与解释器
            // tri_of(bool) 编码一致，读出再比较不受影响）
            v = {to_tri(v), CGType::Tri};
        } else if (v.type == CGType::Num &&
                   (object.elem == CGType::Int ||
                    object.elem == CGType::Double)) {
            // Num 值写静态数值槽（t90）：tag 运行期定，下沉 rt_arr_set_num
            // 按槽 kind 对齐（tag==kind 直存 / 0→1 提升 / 1→0 陷阱 CG7）
            builder_.CreateCall(
                rt_arr_set_num_,
                {object.value, index.value, num_tag(v.value), num_bits(v.value)});
            last_value_ = v;
            return;
        } else if (object.elem == CGType::Obj && v.type == CGType::Dyn &&
                   !object.cls.empty()) {
            // 动态承载值写实例数组元素槽（t158）：同声明槽规则——运行期守卫
            // tag 5 且对象头类 id 属声明元素类或后代类后按实例 ptr 位模式写
            // kind 5 槽（读出仍按 object.cls 前缀偏移解码、方法按 id 分派，
            // t100/t118 同树规则）；守卫不中陷阱退出
            v = {dyn_unbox_to_obj(v.value, object.cls, expr.bracket().line(),
                                  expr.bracket().column()),
                 CGType::Obj, CGType::Int, object.cls};
        } else {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
    }
    if (object.elem == CGType::Obj && v.type == CGType::Obj &&
        !object.cls.empty() &&
        v.cls != object.cls && !is_subclass_of(v.cls, object.cls) &&
        !is_subclass_of(object.cls, v.cls)) {
        // 实例数组整槽写（t100/t118）：同类或同继承树互赋放行（镜像整体互赋
        // t115 L1138——upcast/downcast 任一方向）。同树内元素类沿链前缀布局
        // 兼容：d[0].field 按 object.cls（声明元素类）偏移解码，父类级字段
        // 偏移前缀一致；d[0].method() 按对象头类 id 动态分派。兄弟类/无关类
        //（互不为子类）特有字段偏移错位可能错值，拒编不错编。
        // object.cls 空（动态 object 数组 t141）：槽 kind 5 承载任意实例 ptr，
        // 读回落空 cls → 方法走 t133 分派、字段拒编，无从错值，整槽写放行
        unsupported("array element class mismatch in index assignment",
                    expr.bracket().line(), expr.bracket().column());
    }
    builder_.CreateCall(rt_arr_set_, {object.value, index.value, elem_to_bits(v)});
    last_value_ = v; // 赋值表达式的值为右侧值（与解释器一致，支持链式赋值）
}
// subString 下标参数降级（t105 引入，t154 提取为成员供静态串接收者与动态
// 承载值接收者两路共用）：Int 直传（rt 侧 clamp，end==-1 取 length）；
// Double/Num 按解释器语义预处理——end 在 floor 前判 NaN/精确 -1.0 取 length
//（floor(-0.9) 为 -1 不特判、clamp 到 0 得空串，顺序敏感）；NaN start 归 0；
// 其余 floor 后 clamp 转 i64 交 rt 垫片按 length 收口
llvm::Value* CodeGenerator::sub_string_index(const CGValue& v, bool is_end,
                                             size_t line, size_t column) {
    if (v.type == CGType::Int) {
        return v.value;  // rt 侧 clamp；end==-1 取 length（既有语义）
    }
    if (v.type != CGType::Double && v.type != CGType::Num) {
        unsupported(std::string("non-numeric subString() ") +
                        (is_end ? "end" : "start"),
                    line, column);
    }
    llvm::Value* d = to_double(v);
    llvm::Value* special = builder_.CreateFCmpUNO(d, d, "ssnan");
    if (is_end) {
        // 精确 -1.0 特判在 floor 之前（解释器 end_raw == -1.0 判定）
        llvm::Value* is_neg1 = builder_.CreateFCmpOEQ(
            d, llvm::ConstantFP::get(builder_.getDoubleTy(), -1.0), "ssneg1");
        special = builder_.CreateOr(special, is_neg1, "ssspec");
    }
    // 特例值先中和为 0 再 floor/clamp/转换（NaN 过 fptosi 是 poison）；
    // 上界 clamp 4e18 防超范围 fptosi poison（±Infinity/超大值随后
    // 由 rt 垫片按 length 收口，与解释器 double 域 clamp 殊途同归）
    llvm::Value* zero = llvm::ConstantFP::get(builder_.getDoubleTy(), 0.0);
    llvm::Value* safe = builder_.CreateSelect(special, zero, d, "sssafe");
    llvm::Value* fl =
        builder_.CreateUnaryIntrinsic(llvm::Intrinsic::floor, safe);
    fl = builder_.CreateBinaryIntrinsic(llvm::Intrinsic::maxnum, fl, zero);
    fl = builder_.CreateBinaryIntrinsic(
        llvm::Intrinsic::minnum, fl,
        llvm::ConstantFP::get(builder_.getDoubleTy(), 4.0e18));
    llvm::Value* idx = builder_.CreateFPToSI(fl, builder_.getInt64Ty(), "ssidx");
    if (!is_end) {
        return idx;  // NaN start 中和为 0 即解释器语义
    }
    // NaN/精确 -1.0 end → 哨兵 -1（rt 取 length）
    return builder_.CreateSelect(
        special, llvm::ConstantInt::getSigned(builder_.getInt64Ty(), -1), idx,
        "ssend");
}
void CodeGenerator::visitMethodCall(const MethodCallExpr& expr) {
    // string 方法（S10 t57）：trim 系列/subString 降级到 collie_rt；toString()
    // 方法形式对任意标量接收者复用 to_str（与内建 toString(x) 同一降级）；
    // toNumber() 方法形式复用内建 toNumber(x) 降级（t63）；number 专属
    // 方法（abs/integerPart 等）与 tribool/tuple 方法维持拒编不错编
    const std::string name(expr.name().lexeme());
    size_t line = expr.name().line();
    size_t column = expr.name().column();
    CGValue object = emit(expr.object());

    if (object.type == CGType::Dyn &&
        (name == "trim" || name == "trimLeft" || name == "trimRight" ||
         name == "subString") &&
        std::none_of(classes_.begin(), classes_.end(),
                     [&](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.dispatch.count(name) != 0;
                     })) {
        // 动态承载值的串方法（t154）：tag 运行期才定——下沉 rt 按 tag 分派
        // （3=串复用既有 trim/subString 垫片，其余位落解释器同消息陷阱
        // "Method 'x()' is only supported on strings, got <kind>"）。静态守卫
        // 「全程序无类定义同名方法」：有则接收者可能是实例，整面回落下方
        // t145 dyn_obj_guard 的实例位方法动态分派，已交付面零回归
        if (name == "subString") {
            const auto& arguments = expr.arguments();
            if (arguments.empty() || arguments.size() > 2) {
                unsupported("subString() with this arity", line, column);
            }
            CGValue start = emit(arguments[0].get());
            llvm::Value* start_v = sub_string_index(start, false, line, column);
            llvm::Value* end =
                llvm::ConstantInt::getSigned(builder_.getInt64Ty(), -1);
            if (arguments.size() == 2) {
                CGValue end_value = emit(arguments[1].get());
                end = sub_string_index(end_value, true, line, column);
            }
            last_value_ = {builder_.CreateCall(
                               rt_dyn_str_substring_,
                               {num_tag(object.value), num_bits(object.value),
                                start_v, end, builder_.CreateGlobalString(name)},
                               "dynsubstr"),
                           CGType::Str};
            return;
        }
        if (!expr.arguments().empty()) {
            // 解释器同为运行期报错；codegen 静态可判，拒编不错编（同静态串路径）
            unsupported(name + "() with arguments", line, column);
        }
        int mode = name == "trim" ? 0 : name == "trimLeft" ? 1 : 2;
        last_value_ = {builder_.CreateCall(
                           rt_dyn_str_trim_,
                           {num_tag(object.value), num_bits(object.value),
                            builder_.getInt64(mode),
                            builder_.CreateGlobalString(name)},
                           "dyntrim"),
                       CGType::Str};
        return;
    }

    if (object.type == CGType::Dyn && name == "get" &&
        std::none_of(classes_.begin(), classes_.end(),
                     [&](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.dispatch.count(name) != 0;
                     })) {
        // 动态承载值的 get(key)（t170）：tag 8 动态 tuple 位复用 rt_tuple_key_dyn
        // 的非空名扫描取 {tag,bits} 拼 Dyn，其余位落 rt 方法接收者陷阱（解释器
        // get 段在实例分支之后、元组分支内先判 is_tuple，消息即同一条）；静态
        // 守卫「全程序无类定义同名方法」同 t154~t157，命中则整面回落下方 t145
        // 实例位方法动态分派
        if (expr.arguments().size() != 1) {
            unsupported("get() with this arity", line, column);
        }
        CGValue key_val = emit(expr.arguments()[0].get());
        if (key_val.type != CGType::Str) {
            unsupported("non-string tuple get() key", line, column);
        }
        llvm::AllocaInst* otag =
            create_entry_alloca(builder_.getInt64Ty(), "dyntupget.otag");
        llvm::AllocaInst* obits =
            create_entry_alloca(builder_.getInt64Ty(), "dyntupget.obits");
        builder_.CreateCall(
            rt_dyn_tuple_get_, {num_tag(object.value), num_bits(object.value),
                                key_val.value, otag, obits});
        last_value_ = {make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag),
                                builder_.CreateLoad(builder_.getInt64Ty(), obits)),
                       CGType::Dyn};
        return;
    }

    if (object.type == CGType::Dyn && name == "toString" &&
        expr.arguments().empty() &&
        std::none_of(classes_.begin(), classes_.end(),
                     [&](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.dispatch.count(name) != 0;
                     })) {
        // 动态承载值的 toString()（t159）：解释器把它列为通用内建方法，任意标量
        // 位均可转串（实例位转 "<object>"），故复用动态标量转串垫片
        // collie_rt_num_to_str 按运行期 tag 分派（与插值/print 同一格式面，零新增
        // rt 接口）；静态守卫「无类定义同名方法」同 t154~t157，命中守卫的极程序
        // 整面回落 t145 实例位分派
        last_value_ = {to_str(object, expr.name()), CGType::Str};
        return;
    }

    if (object.type == CGType::Dyn && name == "toNumber" &&
        expr.arguments().empty() &&
        std::none_of(classes_.begin(), classes_.end(),
                     [&](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.dispatch.count(name) != 0;
                     })) {
        // 动态承载值的 toNumber()（t155）：与内建形式同一 rt 分派，仅 mode 取
        // 1（解释器对实例接收者的方法调用走方法查找失败分支，消息不同）；
        // 结果静态类型 Num，消费面（算术/比较/进类型化槽/print）全走既有臂；
        // 静态守卫「无类定义同名方法」同 t154，命中则整面回落 t145 实例位分派
        last_value_ = {dyn_to_number(object, true), CGType::Num};
        return;
    }

    if (object.type == CGType::Dyn && is_number_method_name(name) &&
        expr.arguments().empty() &&
        std::none_of(classes_.begin(), classes_.end(),
                     [&](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.dispatch.count(name) != 0;
                     })) {
        // 动态承载值的 number 专属方法（t156）：解释器进该段先判 is_number()，
        // 故 rt 守卫只放行 tag 0/1（其余位按解释器两类消息陷阱退出，不静默错
        // 值）；Dyn 的 {tag,bits} 与 Num 表示完全重合，放行后直接按 Num 接收者
        // 复用 t67 双路降级（整数态/小数态各自成路，结果 Num 或 Bool），消费面
        // 全走既有臂。静态守卫「无类定义同名方法」同 t154/t155，命中则整面
        // 回落 t145 实例位分派
        builder_.CreateCall(rt_dyn_num_guard_,
                            {num_tag(object.value),
                             builder_.CreateGlobalString(name)});
        gen_number_method({object.value, CGType::Num}, name, line, column);
        return;
    }

    if (object.type == CGType::Dyn && is_tribool_method_name(name) &&
        expr.arguments().empty() &&
        std::none_of(classes_.begin(), classes_.end(),
                     [&](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.dispatch.count(name) != 0;
                     })) {
        // 动态承载值的 tribool 三态判定方法（t157）：解释器进该段先判
        // is_tribool()，故 rt 守卫只放行 tag 6（其余位按解释器两类消息陷阱退出，
        // 不静默错值；bool 位也不放行——解释器对 `true.isTrue()` 运行期报错）；
        // tag 6 的 bits 就是静态 tribool 的 i8 三态编码零扩展（t136 装箱即
        // zext i8），trunc 回 i8 后直接复用 t65 的常量 icmp 降级，结果 Bool
        builder_.CreateCall(rt_dyn_tri_guard_,
                            {num_tag(object.value),
                             builder_.CreateGlobalString(name)});
        gen_tribool_method(
            builder_.CreateTrunc(num_bits(object.value), builder_.getInt8Ty(),
                                 "dyntri"),
            name);
        return;
    }

    // t159：Dyn 接收者走 toString 拆分时携带的 {tag,bits} 与哨兵类 id，
    // 供下方空 cls 分派的 default 臂转串使用（其余路径恒为 nullptr）
    llvm::Value* dyn_tag = nullptr;
    llvm::Value* dyn_bits = nullptr;
    llvm::Value* dyn_cls_override = nullptr;
    if (object.type == CGType::Dyn) {
        const bool split_tostring =
            name == "toString" && expr.arguments().empty() &&
            std::any_of(classes_.begin(), classes_.end(),
                        [this](const std::pair<const std::string, CGClass>& entry) {
                            const auto it = entry.second.dispatch.find("toString");
                            if (it == entry.second.dispatch.end()) {
                                return false;
                            }
                            // 仅当定义者的 toString 是无参 string 返回时才可拆分
                            //（异签名统一 PHI 不可行，交下方既有分派/拒编路径）
                            const CGMethod& m = entry.second.instances.at(it->second);
                            return m.ret_type == CGType::Str && m.param_types.empty();
                        });
        if (split_tostring) {
            // 动态承载值的 toString()（t159）：解释器把它列为通用内建方法（任意
            // 标量位转自身表示串），而实例位在同名类方法存在时须走 t133 用户方法
            // 分派——按运行期 tag 拆分：tag==5 取对象头类 id 入分派 switch，其余
            // 位以哨兵 id 落 default 臂转串（collie_rt_num_to_str 同 print/插值
            // 的 tag 分派表，实例位兜底 "<object>" 与解释器一致）；无同名类方法时
            // 上方 t159 臂已整值走转串，不会到这里
            dyn_tag = num_tag(object.value);
            dyn_bits = num_bits(object.value);
            llvm::Value* ptr = builder_.CreateIntToPtr(
                dyn_bits, llvm_type_of(CGType::Obj), "dynobjptr");
            dyn_cls_override = dyn_obj_clsid_or_sentinel(dyn_tag, ptr);
            object = {ptr, CGType::Obj, CGType::Int, std::string()};
        } else {
            // kind 7 元素读出（Dyn）的方法调用（t145）：tag==5 实例位守卫还原
            // 实例 ptr 后并入下方空 cls 方法动态分派（t133，全体定义者 switch
            // 按对象头类 id 调用）；非实例位落 collie_rt undefined method 陷阱
            // （解释器对 `1.abs()` 等标量自有方法可运行——number 专属方法已在上方
            // t156 臂按运行期 tag 处理，tribool 三态判定已在 t157 臂处理，其余标量
            // 位方法面维持拒错编从陷阱）；串方法 trim/subString 与 toNumber 已在
            // t154/t155 臂处理，仅在存在同名类方法时才落到这里
            object = {dyn_obj_guard(object, name, true), CGType::Obj,
                      CGType::Int, std::string()};
        }
    }

    if (object.type == CGType::Obj && object.cls.empty()) {
        // object 动态值方法调用（t133，S81 残余面解锁）：静态 cls 未知，
        // 全体定义了该方法的类均为 switch case（不限同树——object 可承载
        // 任意实例），按对象头类 id 运行期分派；动态类未定义该方法落
        // default 陷阱臂（解释器同为运行期报错，消息对齐面非 stdout）；
        // toString 兜底同 t102 规则；全体均未定义时拒编不错编。
        // 字段访问不在此面（空 cls 无静态布局，维持拒编）
        std::vector<const CGClass*> defs;
        for (const auto& entry : classes_) {
            if (entry.second.dispatch.count(name) != 0) {
                defs.push_back(&entry.second);
            }
        }
        if (defs.empty()) {
            if (name == "toString" && expr.arguments().empty()) {
                last_value_ = {to_str(object, expr.name()), CGType::Str};
                return;
            }
            unsupported("undefined method '" + name +
                            "' on object dynamic value",
                        line, column);
        }
        const CGMethod& dm = defs[0]->instances.at(defs[0]->dispatch.at(name));
        const auto& arguments = expr.arguments();
        if (arguments.size() != dm.param_types.size()) {
            // 解释器运行期报错；codegen 静态可判，拒编不错编（同命中路径）
            unsupported("method arity mismatch for '" + name + "'", line, column);
        }
        std::sort(defs.begin(), defs.end(),
                  [](const CGClass* a, const CGClass* b) {
                      return a->id < b->id;
                  });
        if (!signatures_consistent(defs, name, dm)) {
            // 各定义者签名不一致（t183，S130）：object 面跨树同名异签无法静态
            // 排除，改逐臂按各自签名动态拆槽、返回值合流为 Dyn
            std::vector<CGValue> evaluated;
            evaluated.reserve(arguments.size());
            for (const auto& arg : arguments) {
                evaluated.push_back(emit(arg.get()));
            }
            llvm::Value* clsid =
                dyn_cls_override
                    ? dyn_cls_override
                    : builder_.CreateLoad(builder_.getInt64Ty(), object.value,
                                          "clsid");
            emit_mismatched_dispatch(defs, name, object.value, evaluated, clsid,
                                     dyn_tag, dyn_bits, line, column);
            return;
        }
        std::vector<llvm::Value*> args{object.value};
        for (size_t i = 0; i < arguments.size(); ++i) {
            CGValue a = emit(arguments[i].get());
            args.push_back(coerce_call_arg(a, dm.param_types[i], dm.param_cls[i],
                                           line, column));
        }
        // Dyn 接收者的 toString 拆分（t159）：类 id 由 tag==5 分支的守卫载入
        // （哨兵路径不解引用非指针位模式）；其余方法调用沿用无条件载入
        llvm::Value* clsid = dyn_cls_override
                                 ? dyn_cls_override
                                 : builder_.CreateLoad(builder_.getInt64Ty(),
                                                       object.value, "clsid");
        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
        auto* trap_bb = llvm::BasicBlock::Create(context_, "dispatch.trap", fn);
        auto* merge_bb = llvm::BasicBlock::Create(context_, "dispatch.end", fn);
        llvm::SwitchInst* sw = builder_.CreateSwitch(
            clsid, trap_bb, static_cast<unsigned>(defs.size()));
        std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
        for (const CGClass* sub : defs) {
            auto* bb = llvm::BasicBlock::Create(
                context_,
                "dispatch." + std::string(sub->stmt->name().lexeme()), fn);
            sw->addCase(builder_.getInt64(sub->id), bb);
            builder_.SetInsertPoint(bb);
            const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
            llvm::Value* call = builder_.CreateCall(m.fn, args);
            incoming.emplace_back(builder_.GetInsertBlock(), call);
            builder_.CreateBr(merge_bb);
        }
        builder_.SetInsertPoint(trap_bb);
        if (name == "toString" && dm.ret_type == CGType::Str &&
            arguments.empty()) {
            // 动态类无用户 toString → 内建兜底（对齐解释器）：哨兵 id 落此臂的
            // 是 Dyn 非实例位（t159），按运行期 tag 转自身表示串（num_to_str 的
            // tag 5 仍兜底 "<object>"）；静态 cls 接收者维持常量 "<object>"
            llvm::Value* fallback =
                dyn_tag
                    ? builder_.CreateCall(rt_num_to_str_, {dyn_tag, dyn_bits},
                                          "dynstr")
                    : static_cast<llvm::Value*>(
                          builder_.CreateGlobalString("<object>"));
            incoming.emplace_back(trap_bb, fallback);
            builder_.CreateBr(merge_bb);
        } else {
            builder_.CreateCall(rt_trap_undefined_method_,
                                {builder_.CreateGlobalString(name)});
            builder_.CreateUnreachable();
        }
        builder_.SetInsertPoint(merge_bb);
        llvm::Value* result = nullptr;
        if (dm.ret_type != CGType::Void) {
            llvm::PHINode* phi = builder_.CreatePHI(
                llvm_type_of(dm.ret_type),
                static_cast<unsigned>(incoming.size()), "dispatchtmp");
            for (const auto& in : incoming) {
                phi->addIncoming(in.second, in.first);
            }
            result = phi;
        }
        last_value_ = (dm.ret_type == CGType::Void)
                          ? CGValue{nullptr, CGType::Void}
                          : CGValue{result, dm.ret_type,
                                    dm.ret_type == CGType::Arr ? CGType::Num
                                                               : CGType::Int,
                                    dm.ret_cls};
        return;
    }
    if (object.type == CGType::Obj) {
        // 类实例方法（t60/t61/t86）：查分派表后，静态 cls 无后代类 → 直调
        // 本类单态化实例（现状零开销）；有后代类（upcast 后动态类可能为任一
        // 后代）→ 读对象头类 id switch 到动态类的单态化实例（模板方法 this
        // 分派天然正确）；未命中时 toString 内建兜底返 "<object>"（对齐解释
        // 器分派顺序：find_method 优先）
        const CGClass& cls = classes_.at(object.cls);
        auto dit = cls.dispatch.find(name);
        if (dit != cls.dispatch.end()) {
            const CGMethod& info = cls.instances.at(dit->second);
            const auto& arguments = expr.arguments();
            if (arguments.size() != info.param_types.size()) {
                // 解释器运行期报错；codegen 静态可判，拒编不错编
                unsupported("method arity mismatch for '" + name + "'", line, column);
            }
            // 同树定义者收集（t86/t103）：downcast 放行后动态类可为整棵
            // 继承树上任意类（旁支可经共同祖先中转写入槽），case 只挂
            // 树内定义了该方法的类；按类 id 排序保证 case 生成顺序确定
            std::vector<const CGClass*> defs{&cls};
            bool lone_tree = true;
            for (const auto& entry : classes_) {
                if (entry.first == object.cls ||
                    nearest_common_ancestor(entry.first, object.cls).empty()) {
                    continue;
                }
                lone_tree = false;
                if (entry.second.dispatch.count(name) != 0) {
                    defs.push_back(&entry.second);
                }
            }
            std::sort(defs.begin(), defs.end(),
                      [](const CGClass* a, const CGClass* b) {
                          return a->id < b->id;
                      });
            if (!lone_tree && !signatures_consistent(defs, name, info)) {
                // 覆写者签名与静态类不一致（t183，S130）：统一 PHI 无法承载
                // 不同签名，改逐臂按各自签名动态拆槽、返回值合流为 Dyn
                std::vector<CGValue> evaluated;
                evaluated.reserve(arguments.size());
                for (const auto& arg : arguments) {
                    evaluated.push_back(emit(arg.get()));
                }
                llvm::Value* clsid = builder_.CreateLoad(
                    builder_.getInt64Ty(), object.value, "clsid");
                emit_mismatched_dispatch(defs, name, object.value, evaluated,
                                         clsid, nullptr, nullptr, line, column);
                return;
            }
            std::vector<llvm::Value*> args{object.value};
            for (size_t i = 0; i < arguments.size(); ++i) {
                CGValue a = emit(arguments[i].get());
                args.push_back(coerce_call_arg(a, info.param_types[i],
                                               info.param_cls[i], line, column));
            }
            llvm::Value* result = nullptr;
            if (lone_tree) {
                // 单类树：动态类必为静态类，直调零开销（现状保持）
                result = builder_.CreateCall(info.fn, args);
            } else {
                // 读头部类 id（struct 元素 0，偏移 0 即对象指针本身）后
                // switch：case = 各定义者，default = 陷阱（t103：downcast 放
                // 行后动态类可为无此方法的祖先/旁支，t86 期 default 直走静态
                // 类臂的前提不再成立——祖先字段块更小会错值；纯 upcast 程序
                // 动态类必有该方法，default 不可达行为不变）
                llvm::Value* clsid = builder_.CreateLoad(
                    builder_.getInt64Ty(), object.value, "clsid");
                llvm::Function* fn = builder_.GetInsertBlock()->getParent();
                auto* trap_bb =
                    llvm::BasicBlock::Create(context_, "dispatch.trap", fn);
                auto* merge_bb =
                    llvm::BasicBlock::Create(context_, "dispatch.end", fn);
                llvm::SwitchInst* sw = builder_.CreateSwitch(
                    clsid, trap_bb, static_cast<unsigned>(defs.size()));
                std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
                for (const CGClass* sub : defs) {
                    auto* bb = llvm::BasicBlock::Create(
                        context_,
                        "dispatch." + std::string(sub->stmt->name().lexeme()), fn);
                    sw->addCase(builder_.getInt64(sub->id), bb);
                    builder_.SetInsertPoint(bb);
                    const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
                    llvm::Value* call = builder_.CreateCall(m.fn, args);
                    incoming.emplace_back(builder_.GetInsertBlock(), call);
                    builder_.CreateBr(merge_bb);
                }
                builder_.SetInsertPoint(trap_bb);
                if (name == "toString" && info.ret_type == CGType::Str &&
                    arguments.empty()) {
                    // 动态类无用户 toString → 内建兜底 "<object>"（对齐解释
                    // 器分派顺序：find_method 优先，未命中才内建兜底）
                    incoming.emplace_back(
                        trap_bb, builder_.CreateGlobalString("<object>"));
                    builder_.CreateBr(merge_bb);
                } else {
                    // 消息对齐解释器 "Undefined method 'X' on object"
                    builder_.CreateCall(rt_trap_undefined_method_,
                                        {builder_.CreateGlobalString(name)});
                    builder_.CreateUnreachable();
                }
                builder_.SetInsertPoint(merge_bb);
                if (info.ret_type != CGType::Void) {
                    llvm::PHINode* phi = builder_.CreatePHI(
                        llvm_type_of(info.ret_type),
                        static_cast<unsigned>(incoming.size()), "dispatchtmp");
                    for (const auto& in : incoming) {
                        phi->addIncoming(in.second, in.first);
                    }
                    result = phi;
                }
            }
            // Arr 返回值 elem 记 Num 哨兵（t70，同 visitCall）
            last_value_ = (info.ret_type == CGType::Void)
                              ? CGValue{nullptr, CGType::Void}
                              : CGValue{result, info.ret_type,
                                        info.ret_type == CGType::Arr ? CGType::Num
                                                                     : CGType::Int,
                                        info.ret_cls};
            return;
        }
        // 静态类未命中但树内其他类定义了该方法（t102/t103，S39 残余面解
        // 锁）：upcast/downcast 放行后动态类可为整棵继承树上任意定义者；
        // 读对象头类 id switch 到定义者的单态化实例，default（动态类实例
        // 无此方法）走运行期陷阱不错值——消息对齐解释器
        // "Undefined method 'X' on object"；toString 仅在树内无用户定义时
        // 才直接内建兜底（对齐解释器分派顺序：find_method 优先）
        std::vector<const CGClass*> defs;
        for (const auto& entry : classes_) {
            if (entry.first != object.cls &&
                !nearest_common_ancestor(entry.first, object.cls).empty() &&
                entry.second.dispatch.count(name) != 0) {
                defs.push_back(&entry.second);
            }
        }
        if (defs.empty()) {
            if (name == "toString" && expr.arguments().empty()) {
                last_value_ = {to_str(object, expr.name()), CGType::Str};
                return;
            }
            unsupported("undefined method '" + name + "' on class '" + object.cls +
                            "'",
                        line, column);
        }
        const CGMethod& dm = defs[0]->instances.at(defs[0]->dispatch.at(name));
        const auto& arguments = expr.arguments();
        if (arguments.size() != dm.param_types.size()) {
            // 解释器运行期报错；codegen 静态可判，拒编不错编（同命中路径）
            unsupported("method arity mismatch for '" + name + "'", line, column);
        }
        std::sort(defs.begin(), defs.end(),
                  [](const CGClass* a, const CGClass* b) {
                      return a->id < b->id;
                  });
        if (!signatures_consistent(defs, name, dm)) {
            // 树内各定义者签名不一致（t183，S130）：静态类本身未定义该方法，
            // 命中哪个继承者由运行期类 id 决定，改逐臂动态拆槽、合流为 Dyn
            std::vector<CGValue> evaluated;
            evaluated.reserve(arguments.size());
            for (const auto& arg : arguments) {
                evaluated.push_back(emit(arg.get()));
            }
            llvm::Value* clsid = builder_.CreateLoad(
                builder_.getInt64Ty(), object.value, "clsid");
            emit_mismatched_dispatch(defs, name, object.value, evaluated, clsid,
                                     nullptr, nullptr, line, column);
            return;
        }
        std::vector<llvm::Value*> args{object.value};
        for (size_t i = 0; i < arguments.size(); ++i) {
            CGValue a = emit(arguments[i].get());
            args.push_back(coerce_call_arg(a, dm.param_types[i], dm.param_cls[i],
                                           line, column));
        }
        // switch：case 各定义者（按 id 排序，同 t86），default = 陷阱
        llvm::Value* clsid = builder_.CreateLoad(
            builder_.getInt64Ty(), object.value, "clsid");
        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
        auto* trap_bb = llvm::BasicBlock::Create(context_, "dispatch.trap", fn);
        auto* merge_bb = llvm::BasicBlock::Create(context_, "dispatch.end", fn);
        llvm::SwitchInst* sw = builder_.CreateSwitch(
            clsid, trap_bb, static_cast<unsigned>(defs.size()));
        std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
        for (const CGClass* sub : defs) {
            auto* bb = llvm::BasicBlock::Create(
                context_,
                "dispatch." + std::string(sub->stmt->name().lexeme()), fn);
            sw->addCase(builder_.getInt64(sub->id), bb);
            builder_.SetInsertPoint(bb);
            const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
            llvm::Value* call = builder_.CreateCall(m.fn, args);
            incoming.emplace_back(builder_.GetInsertBlock(), call);
            builder_.CreateBr(merge_bb);
        }
        builder_.SetInsertPoint(trap_bb);
        if (name == "toString" && dm.ret_type == CGType::Str &&
            arguments.empty()) {
            // 动态类无用户 toString → 内建兜底 "<object>"（对齐解释器）
            incoming.emplace_back(trap_bb,
                                  builder_.CreateGlobalString("<object>"));
            builder_.CreateBr(merge_bb);
        } else {
            builder_.CreateCall(rt_trap_undefined_method_,
                                {builder_.CreateGlobalString(name)});
            builder_.CreateUnreachable();
        }
        builder_.SetInsertPoint(merge_bb);
        llvm::Value* result = nullptr;
        if (dm.ret_type != CGType::Void) {
            llvm::PHINode* phi = builder_.CreatePHI(
                llvm_type_of(dm.ret_type),
                static_cast<unsigned>(incoming.size()), "dispatchtmp");
            for (const auto& in : incoming) {
                phi->addIncoming(in.second, in.first);
            }
            result = phi;
        }
        // Arr 返回值 elem 记 Num 哨兵（t70，同 visitCall/命中路径）
        last_value_ = (dm.ret_type == CGType::Void)
                          ? CGValue{nullptr, CGType::Void}
                          : CGValue{result, dm.ret_type,
                                    dm.ret_type == CGType::Arr ? CGType::Num
                                                               : CGType::Int,
                                    dm.ret_cls};
        return;
    }

    if (name == "toString" && expr.arguments().empty()) {
        last_value_ = {to_str(object, expr.name()), CGType::Str};
        return;
    }

    if (object.type == CGType::TupDyn && name == "get") {
        // 动态 tuple.get(key)（t140）：形状运行期才定，常量/动态键同路——
        // 键求值为 Str 后 rt_tuple_key_dyn 按非空名 strcmp 扫描取 {tag,bits}
        // 拼 Dyn（未命中陷阱 "Undefined tuple field '<key>'" 在 rt 内，对齐解释器）；
        // 结果仅 print/toString/插值可消费
        if (expr.arguments().size() != 1) {
            unsupported("get() with this arity", line, column);
        }
        CGValue key_val = emit(expr.arguments()[0].get());
        if (key_val.type != CGType::Str) {
            unsupported("non-string tuple get() key", line, column);
        }
        llvm::AllocaInst* otag =
            create_entry_alloca(builder_.getInt64Ty(), "tupdyn.otag");
        llvm::AllocaInst* obits =
            create_entry_alloca(builder_.getInt64Ty(), "tupdyn.obits");
        builder_.CreateCall(
            rt_tuple_key_dyn_, {object.value, key_val.value, otag, obits});
        last_value_ = {make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag),
                                builder_.CreateLoad(builder_.getInt64Ty(), obits)),
                       CGType::Dyn};
        return;
    }
    if (object.type == CGType::Tup && name == "get") {
        // tuple.get(key)：编译期扫名字表（对齐解释器 get 的非空名匹配）。
        // 按值拷贝不留引用：动态键路径下方 emit(key) 可能触发 register_tuple
        // 扩容 tuple_values_，持引用会悬垂（与 visitIndex/gen_tuple_eq 同一防护）
        if (expr.arguments().size() != 1) {
            unsupported("get() with this arity", line, column);
        }
        const CGTuple t = tuple_values_[object.tup];
        // 常量字符串键（t68）：编译期解析（未命中静态可判，拒编不错编）
        const auto* key = dynamic_cast<const LiteralExpr*>(expr.arguments()[0].get());
        if (key && key->token().type() == TokenType::LITERAL_STRING) {
            const std::string key_str(key->token().lexeme());
            for (size_t i = 0; i < t.names.size(); ++i) {
                if (!t.names[i].empty() && t.names[i] == key_str) {
                    last_value_ = t.elems[i];
                    return;
                }
            }
            unsupported("undefined tuple field '" + key_str + "'", line, column);
        }
        // 动态键（t84）：限同质命名 tuple（元素同 CGType 且 ∈ {Int/Double/Bool/Str}、
        // ≥1 非空名，结果类型静态可定）——物化 names(kind 3)+values 数组后
        // rt_tuple_get 按非空名 strcmp 扫描取 i64 bits、bits_to_elem 还原；未命中打
        // "Undefined tuple field '<key>'" + exit(1)（核心消息与解释器 RuntimeError
        // 一致，位置前缀缺失同 t83 越界陷阱既定分歧）。
        // 数值系异质（t108，同 t107 索引机制）：元素全 ∈ {Int/Double/Num}
        //（含全 Num 同质）——逐元素 to_num 后物化 tags+bits 双 int 数组，
        // names 复用一份、同一键两次 rt_tuple_get 拼 Num 动态值（未命中陷阱
        // 在首次，零新增 rt 接口）。含 Bool/Str/Tri 的异质/同质 tribool
        //（t138）走 Dyn 承载。嵌套元素异质/无统一表示元素/空 tuple/
        // 无命名字段/非 Str 键保持拒编——结果类型静态不可定或
        // 无统一表示，拒编不错编
        const long long n = static_cast<long long>(t.elems.size());
        if (n == 0) {
            unsupported("non-constant get() on empty tuple", line, column);
        }
        const auto is_numeric = [](CGType ty) {
            return ty == CGType::Int || ty == CGType::Double || ty == CGType::Num;
        };
        const auto is_scalar = [](CGType ty) {
            // 承载位判定（t172 起与 is_dyn_carrier 同集，与 visitIndex 的
            // 同名判定同源）：标量 + Fn/Arr/Obj/Tup/TupDyn 均可入 tags+bits
            // 物化路径，动态取键的读出即对应 Dyn 位
            return ty == CGType::Int || ty == CGType::Double || ty == CGType::Num ||
                   ty == CGType::Bool || ty == CGType::Str || ty == CGType::Tri ||
                   ty == CGType::Dyn || ty == CGType::Fn || ty == CGType::Arr ||
                   ty == CGType::Obj || ty == CGType::Tup || ty == CGType::TupDyn;
        };
        bool homogeneous = true;
        bool all_numeric = true;
        bool all_scalar = true;
        const CGType elem = t.elems.front().type;
        for (const auto& e : t.elems) {
            if (e.type != elem) homogeneous = false;
            if (!is_numeric(e.type)) all_numeric = false;
            if (!is_scalar(e.type)) all_scalar = false;
        }
        // Dyn 承载判定（t138，同 visitIndex）：元素全为可承载位 → tags+bits 物化
        // CGType::Dyn，读出走 S92–S118 全部运行期 tag 分派消费；同质 Arr/Obj
        // 走单数组路径（t114），同质跨树实例位（t172）与 Void 等无 {tag,bits}
        // 编码者分别落本路径/维持拒编不错编
        const bool obj_static_merge = elem != CGType::Obj || !homogeneous ||
                                      !tuple_obj_merged_cls(t).empty();
        const bool single_array =
            obj_static_merge && homogeneous &&
            (elem == CGType::Int || elem == CGType::Double ||
             elem == CGType::Bool || elem == CGType::Str ||
             elem == CGType::Arr || elem == CGType::Obj);
        const bool use_dyn = !single_array && !all_numeric && all_scalar;
        if (!single_array && !all_numeric && !use_dyn) {
            if (homogeneous) {
                unsupported("non-constant tuple get() on this element type", line, column);
            }
            unsupported("non-constant get() on heterogeneous tuple", line, column);
        }
        bool has_named = false;
        for (const auto& nm : t.names) {
            if (!nm.empty()) { has_named = true; break; }
        }
        if (!has_named) {
            unsupported("non-constant get() on tuple with no named fields", line, column);
        }
        CGValue key_val = emit(expr.arguments()[0].get());
        if (key_val.type != CGType::Str) {
            unsupported("non-string tuple get() key", line, column);
        }
        // names 数组（kind 3 string）：无名元素存空串 ""（rt 侧非空名匹配天然跳过）
        llvm::Value* names = builder_.CreateCall(
            rt_arr_new_,
            {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(3)},
            "tupnames");
        for (long long i = 0; i < n; ++i) {
            llvm::Value* nm_ptr =
                builder_.CreateGlobalString(t.names[static_cast<size_t>(i)]);
            builder_.CreateCall(
                rt_arr_set_,
                {names, builder_.getInt64(static_cast<uint64_t>(i)),
                 builder_.CreatePtrToInt(nm_ptr, builder_.getInt64Ty(), "nmbits")});
        }
        // values 数组：Dyn 路径（t138）tags+bits 同一键两次 rt_tuple_get 拼动态
        // 标量（未命中陷阱在首次）；数值系路径（t108）tags+bits 双数组（均 kind 0）
        // 同一键两次 rt_tuple_get 拼 Num；静态路径（t84）单数组逐元素 elem_to_bits
        if (use_dyn) {
            auto [tags, bits_arr] = materialize_dyn_scalars(t, line, column);
            llvm::Value* tag = builder_.CreateCall(
                rt_tuple_get_, {names, tags, key_val.value}, "dyntag");
            llvm::Value* bits = builder_.CreateCall(
                rt_tuple_get_, {names, bits_arr, key_val.value}, "dynbits");
            last_value_ = {make_num(tag, bits), CGType::Dyn};
            return;
        }
        if (!homogeneous || elem == CGType::Num) {
            llvm::Value* tags = builder_.CreateCall(
                rt_arr_new_,
                {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(0)},
                "tuptags");
            llvm::Value* bits_arr = builder_.CreateCall(
                rt_arr_new_,
                {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(0)},
                "tupbits");
            for (long long i = 0; i < n; ++i) {
                llvm::Value* num = to_num(t.elems[static_cast<size_t>(i)]);
                builder_.CreateCall(
                    rt_arr_set_,
                    {tags, builder_.getInt64(static_cast<uint64_t>(i)), num_tag(num)});
                builder_.CreateCall(
                    rt_arr_set_,
                    {bits_arr, builder_.getInt64(static_cast<uint64_t>(i)), num_bits(num)});
            }
            llvm::Value* tag = builder_.CreateCall(
                rt_tuple_get_, {names, tags, key_val.value}, "tuptag");
            llvm::Value* bits = builder_.CreateCall(
                rt_tuple_get_, {names, bits_arr, key_val.value}, "tupbit");
            last_value_ = {make_num(tag, bits), CGType::Num};
            return;
        }
        llvm::Value* vals = builder_.CreateCall(
            rt_arr_new_,
            {builder_.getInt64(static_cast<uint64_t>(n)),
             builder_.getInt64(static_cast<uint64_t>(arr_kind_of(elem)))},
            "tupvals");
        for (long long i = 0; i < n; ++i) {
            builder_.CreateCall(
                rt_arr_set_,
                {vals, builder_.getInt64(static_cast<uint64_t>(i)),
                 elem_to_bits(t.elems[static_cast<size_t>(i)])});
        }
        llvm::Value* bits =
            builder_.CreateCall(rt_tuple_get_, {names, vals, key_val.value}, "tupget");
        last_value_ = tuple_dynamic_result(bits, elem, t, line, column);
        return;
    }

    if (name == "toNumber" && expr.arguments().empty()) {
        // toNumber() 方法形式（t63）：与内建 toNumber(x) 同一降级（语义层
        // 已限接收者为 string/bool/数值）
        last_value_ = {to_number_num(object, line, column), CGType::Num};
        return;
    }

    if (object.type == CGType::Tri && expr.arguments().empty() &&
        is_tribool_method_name(name)) {
        // tribool 三态判定方法（t65）：与对应三态常量 icmp 出 bool（对齐解释器）
        gen_tribool_method(object.value, name);
        return;
    }

    if (object.type == CGType::Str) {
        if (name == "trim" || name == "trimLeft" || name == "trimRight") {
            // 0 参（语义层已校验，此处防御）；mode 编码见 collie_rt_str_trim
            if (!expr.arguments().empty()) {
                unsupported(name + "() with arguments", line, column);
            }
            int mode = name == "trim" ? 0 : name == "trimLeft" ? 1 : 2;
            last_value_ = {builder_.CreateCall(
                               rt_str_trim_, {object.value, builder_.getInt32(mode)}, "trimtmp"),
                           CGType::Str};
            return;
        }
        if (name == "subString") {
            // subString(start[, end])：参数收 Int/Double/Num（t105 解锁非整数，
            // 对齐解释器：end 在 floor 前判 NaN/精确 -1.0 取 length——floor(-0.9)
            // 为 -1 不特判、clamp 到 0 得空串，顺序敏感；NaN start 归 0；其余
            // floor 后 clamp 转 i64 交 rt 垫片按 length 收口）；缺省 end 传 -1，
            // 运行时取 length（对齐解释器 end 缺省/-1 语义）
            const auto& arguments = expr.arguments();
            if (arguments.empty() || arguments.size() > 2) {
                unsupported("subString() with this arity", line, column);
            }
            CGValue start = emit(arguments[0].get());
            llvm::Value* start_v = sub_string_index(start, false, line, column);
            llvm::Value* end = llvm::ConstantInt::getSigned(builder_.getInt64Ty(), -1);
            if (arguments.size() == 2) {
                CGValue end_value = emit(arguments[1].get());
                end = sub_string_index(end_value, true, line, column);
            }
            last_value_ = {builder_.CreateCall(
                               rt_str_substring_, {object.value, start_v, end}, "substrtmp"),
                           CGType::Str};
            return;
        }
    }
    if (object.type == CGType::Int || object.type == CGType::Double ||
        object.type == CGType::Num) {
        // number 专属方法（t67）：十方法均 0 参（语义层已校验，此处防御）
        if (is_number_method_name(name)) {
            if (!expr.arguments().empty()) {
                unsupported(name + "() with arguments", line, column);
            }
            gen_number_method(object, name, line, column);
            return;
        }
    }
    unsupported("method call '" + name + "'", line, column);
}
void CodeGenerator::visitProperty(const PropertyExpr& expr) {
    // string 的 length 属性（S8 t56）：UTF-8 码点数，返 integer（对齐解释器）；
    // array/tuple 的 length 与类实例字段待对应类型 codegen 支持
    CGValue object = emit(expr.object());
    if (object.type == CGType::Str && expr.name().lexeme() == "length") {
        last_value_ = {builder_.CreateCall(rt_str_len_, {object.value}, "lentmp"),
                       CGType::Int};
        return;
    }
    if (object.type == CGType::Arr && expr.name().lexeme() == "length") {
        // 数组 length 属性（t59）：元素个数，返 integer（对齐解释器）
        last_value_ = {builder_.CreateCall(rt_arr_len_, {object.value}, "arrlen"),
                       CGType::Int};
        return;
    }
    if (object.type == CGType::Dyn && expr.name().lexeme() == "length" &&
        std::none_of(classes_.begin(), classes_.end(),
                     [](const std::pair<const std::string, CGClass>& entry) {
                         return entry.second.field_index.count("length") != 0;
                     })) {
        // 动态承载值的 .length（t152）：tag 运行期才定，按解释器 visitProperty
        // 的 length 接受表分派——串位取 UTF-8 码点数、数组位（kind 4/7）取元素
        // 个数、其余位落 "Property 'length' is only supported on strings and
        // arrays, got <kind>" 陷阱；实例位 tag 5 由上方守卫静态保证无类声明
        // length 字段，解释器同为 "Undefined property 'length' on object"，
        // 故此处一并按 tag 5 陷阱即对齐（有类声明该字段时整面回落 t145 的
        // 实例位字段分派，见范围外）；来源复用全部既有 Dyn 通道
        last_value_ = {builder_.CreateCall(rt_dyn_len_,
                                           {num_tag(object.value),
                                            num_bits(object.value),
                                            builder_.getInt64(0)},
                                           "dynlen"),
                       CGType::Int};
        return;
    }
    if (object.type == CGType::Dyn) {
        // 动态承载值的字段读：按运行期 tag 两路分派后合流为 Dyn（t145 实例位
        // + t170 tuple 位）——tag 8 走 t163 的 rt_tuple_key_dyn 常量键扫描取
        // {tag,bits}（解释器 visitProperty 的元组分支：length 优先、命名字段
        // 线性扫名、未命中 "Undefined tuple field '<name>'"）；tag 5 经
        // dyn_obj_guard 还原实例 ptr 后并入既有字段动态分派（t142）再 to_dyn
        // 打框；其余位由 dyn_obj_guard 落既有 undefined property 陷阱（解释器
        // 对 `1 .name` 同为运行期报错，拒错编从陷阱）。合流值消费面全落
        // S92–S112 既有 tag 分派臂；.length 已在上方按运行期 tag 解锁
        // （t152，tuple 位由 rt dyn_len 的 case 8 补上）
        const std::string name(expr.name().lexeme());
        const size_t line = expr.name().line();
        const size_t column = expr.name().column();
        llvm::Function* dyn_fn = builder_.GetInsertBlock()->getParent();
        auto* tup_bb = llvm::BasicBlock::Create(context_, "prop.tup", dyn_fn);
        auto* objv_bb = llvm::BasicBlock::Create(context_, "prop.objval", dyn_fn);
        auto* dyn_end_bb =
            llvm::BasicBlock::Create(context_, "prop.dynend", dyn_fn);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(num_tag(object.value), builder_.getInt64(8),
                                  "istuptag"),
            tup_bb, objv_bb);
        builder_.SetInsertPoint(tup_bb);
        llvm::AllocaInst* otag =
            create_entry_alloca(builder_.getInt64Ty(), "dynprop.otag");
        llvm::AllocaInst* obits =
            create_entry_alloca(builder_.getInt64Ty(), "dynprop.obits");
        builder_.CreateCall(
            rt_tuple_key_dyn_,
            {builder_.CreateIntToPtr(num_bits(object.value),
                                     llvm_type_of(CGType::TupDyn), "dynpptr"),
             builder_.CreateGlobalString(name), otag, obits});
        llvm::Value* tup_boxed = make_num(
            builder_.CreateLoad(builder_.getInt64Ty(), otag),
            builder_.CreateLoad(builder_.getInt64Ty(), obits));
        llvm::BasicBlock* tup_end = builder_.GetInsertBlock();
        builder_.CreateBr(dyn_end_bb);
        builder_.SetInsertPoint(objv_bb);
        CGValue field = read_obj_field(
            {dyn_obj_guard(object, name, false), CGType::Obj, CGType::Int,
             std::string()},
            name, line, column, true);
        // 无定义者时实例位整臂为运行期陷阱（块已 terminate，不再发指令/连边），
        // 仅剩 tuple 臂流入合流点
        llvm::Value* obj_boxed = field.value ? to_dyn(field, line, column) : nullptr;
        llvm::BasicBlock* objv_end = builder_.GetInsertBlock();
        if (obj_boxed) {
            builder_.CreateBr(dyn_end_bb);
        }
        builder_.SetInsertPoint(dyn_end_bb);
        llvm::PHINode* dyn_phi =
            builder_.CreatePHI(llvm_type_of(CGType::Dyn), 2, "dynprop");
        dyn_phi->addIncoming(tup_boxed, tup_end);
        if (obj_boxed) {
            dyn_phi->addIncoming(obj_boxed, objv_end);
        }
        last_value_ = {dyn_phi, CGType::Dyn};
        return;
    }
    if (object.type == CGType::Obj) {
        last_value_ = read_obj_field(object, std::string(expr.name().lexeme()),
                                     expr.name().line(), expr.name().column(),
                                     false);
        return;
    }
    if (object.type == CGType::TupDyn) {
        // 动态 tuple 属性（t140）：length 经 rt_tuple_len 运行期取元素数；
        // 命名字段访问（t163，S110）与 get(key) 同源——形状运行期才定，键名
        // 作常量串交 rt_tuple_key_dyn 按非空名 strcmp 扫描取 {tag,bits} 拼
        // Dyn，其后消费全部落 S92–S109 既有运行期 tag 分派臂；未命中落 rt
        // 陷阱 "Undefined tuple field '<name>'"，与解释器 visitProperty 的
        // 元组分支同消息（position 元组名字为空串，标识符永不匹配，两端一致）
        const std::string name(expr.name().lexeme());
        if (name == "length") {
            last_value_ = {builder_.CreateCall(rt_tuple_len_, {object.value},
                                               "tupdyn.len"),
                           CGType::Int};
            return;
        }
        llvm::AllocaInst* otag =
            create_entry_alloca(builder_.getInt64Ty(), "tupprop.otag");
        llvm::AllocaInst* obits =
            create_entry_alloca(builder_.getInt64Ty(), "tupprop.obits");
        builder_.CreateCall(rt_tuple_key_dyn_,
                            {object.value, builder_.CreateGlobalString(name),
                             otag, obits});
        last_value_ = {make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag),
                                builder_.CreateLoad(builder_.getInt64Ty(), obits)),
                       CGType::Dyn};
        return;
    }
    if (object.type == CGType::Tup) {
        // visitProperty 分支顺序）；命名字段线性扫名字表编译期解析（未
        // 找到解释器为运行期报错，codegen 静态可判，拒编不错编）
        const std::string name(expr.name().lexeme());
        const CGTuple& t = tuple_values_[object.tup];
        if (name == "length") {
            last_value_ = {builder_.getInt64(t.elems.size()), CGType::Int};
            return;
        }
        for (size_t i = 0; i < t.names.size(); ++i) {
            if (t.names[i] == name) {
                last_value_ = t.elems[i];
                return;
            }
        }
        unsupported("undefined tuple field '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    unsupported("property access", expr.name().line(), expr.name().column());
}
void CodeGenerator::visitPropertyAssign(const PropertyAssignExpr& expr) {
    // 字段赋值（t60）：求值顺序 object → value（对齐解释器 visitPropertyAssign）；
    // 按字段声明类型对齐（仅 integer→decimal 提升，与四处 coerce 等价）
    CGValue object = emit(expr.object());
    if (object.type == CGType::Dyn) {
        // kind 7 元素读出（Dyn）的字段写（t145）：与读路径对称——tag==5
        // 守卫还原实例 ptr 并入下方空 cls 动态分派（t142）；非实例位落
        // undefined property 陷阱（解释器对标量接收者同为运行期报错）
        object = {dyn_obj_guard(object, std::string(expr.name().lexeme()),
                                false),
                  CGType::Obj, CGType::Int, std::string()};
    }
    if (object.type != CGType::Obj) {
        unsupported("property assignment on this value type",
                    expr.name().line(), expr.name().column());
    }
    const std::string name(expr.name().lexeme());
    // 同树定义者收集（t60/t102/t103，与读路径一致）：downcast 放行后动态
    // 类可为整棵继承树上任意类；单类树维持直 GEP 存零开销，多类树各
    // arm 按定义者布局存储，default（动态类实例无此字段）走运行期陷阱
    // 不错值——消息对齐解释器 "Undefined property 'X' on object"
    //
    // 空 cls = 动态 object 字段写（t142）：与读路径对称——全体定义了该字段的
    // 类均入 switch case（不限同树，同 t133 方法动态分派），按对象头类 id
    // 运行期分派存储；lone_tree 恒 false（真实类可能不在 defs 中，须留 default
    // 兜底）。原空 cls 直落 classes_.at("") 抛 out_of_range 使编译进程崩溃，
    // 现纳入动态面。各定义者字段声明类型全等时 switch 前单一 coerce，不一致
    // 时逐臂动态拆槽后合流为 Dyn（t176 读侧 / t178 写侧）
    std::vector<const CGClass*> defs;
    bool lone_tree = true;
    if (object.cls.empty()) {
        for (const auto& entry : classes_) {
            if (entry.second.field_index.count(name) != 0) {
                defs.push_back(&entry.second);
            }
        }
        lone_tree = false;
    } else {
        for (const auto& entry : classes_) {
            if (entry.first != object.cls) {
                if (nearest_common_ancestor(entry.first, object.cls).empty()) {
                    continue;
                }
                lone_tree = false;
            }
            if (entry.second.field_index.count(name) != 0) {
                defs.push_back(&entry.second);
            }
        }
    }
    if (defs.empty()) {
        unsupported("undefined property '" + name + "' on " +
                        (object.cls.empty()
                             ? std::string("dynamic object")
                             : "class '" + object.cls + "'"),
                    expr.name().line(), expr.name().column());
    }
    if (lone_tree) {
        // 单类树：动态类必为静态类，直 GEP + store 零开销（现状保持）
        const CGClass& cls = classes_.at(object.cls);
        auto it = cls.field_index.find(name);
        CGValue v = emit(expr.value());
        const CGField& field = cls.fields[it->second];
        // 校验按本类生效声明、落槽按物理承载（S131：遮蔽冲突槽为 {tag,bits}）
        CGValue stored = field_slot_store(field, *field.decl, v, expr.name());
        builder_.CreateStore(
            stored.value,
            builder_.CreateStructGEP(cls.type, object.value, it->second + 1, name));
        // 赋值表达式的值 = 所赋的值（与解释器一致）；Arr 字段带 Num 哨兵（t71），
        // Obj 字段带类名（t72），冲突槽带 Dyn
        last_value_ = stored;
        return;
    }
    // 多类树：值在 switch 前求值（求值顺序 object → value 不变），各 arm 按
    // 定义者布局存储。各臂**声明承载**（含遮蔽冲突标志）全等时沿用 switch 前
    // 单一转换 + 单型 PHI（t86 静态精度不丢）；不一致时逐臂按各自声明拆槽、
    // 打框后合流为 Dyn（t178 读侧同源，S131 起冲突臂落 {tag,bits} 槽）
    const CGField& ref = defs[0]->fields.at(defs[0]->field_index.at(name));
    std::sort(defs.begin(), defs.end(),
              [](const CGClass* a, const CGClass* b) {
                  return a->id < b->id;
              });
    bool uniform = true;
    for (const CGClass* sub : defs) {
        const CGField& f = sub->fields.at(sub->field_index.at(name));
        // 按声明承载比对（S131）：遮蔽冲突槽的物理类型已整树统一为 Dyn，只看
        // 物理承载会让 byte×word、string×integer 这类分歧看着一致而漏判
        if (!field_carrier_equal(ref, f)) {
            uniform = false;
        }
    }
    CGValue v = emit(expr.value());
    if (!uniform) {
        // 不一致字段的写侧（t178）：解释器 visitPropertyAssign 按**运行期动态类**
        // 的字段声明 coerce_to_declared 后直存，且赋值表达式的值就是那份按动态类
        // 转换后的值。各臂静态类型无法统一，若逐臂按静态类型 coerce，则「某臂的
        // 转换静态不可行」（串位写 array 字段等）会拖垮整个赋值点，而解释器只在
        // 动态类命中该臂时才报错——故先把所赋的值打框为 {tag,bits}，再逐臂交既有
        // 动态拆槽通道（coerce_for_slot 的 allow_dyn_unbox 路径：标量/串/数值互转
        // 即解释器 coerce_to_declared 接受表，Arr 走 tag ∈ {4,7} 守卫、Obj 走
        // tag 5 + 类 id 守卫、Dyn 直存），守卫未命中落该臂内运行期同消息陷阱。
        // store 后按各臂类型 to_dyn 打框，在 {i64,i64} 第一类 struct 上 PHI 合流
        // 为 Dyn（与 t176 读侧同源），消费面全落 S92–S122 既有 tag 分派臂
        CGValue boxed{to_dyn(v, expr.name().line(), expr.name().column()),
                      CGType::Dyn, CGType::Int, std::string()};
        llvm::Value* clsid = builder_.CreateLoad(
            builder_.getInt64Ty(), object.value, "clsid");
        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
        auto* trap_bb = llvm::BasicBlock::Create(context_, "propasg.trap", fn);
        auto* merge_bb = llvm::BasicBlock::Create(context_, "propasg.end", fn);
        llvm::SwitchInst* sw = builder_.CreateSwitch(
            clsid, trap_bb, static_cast<unsigned>(defs.size()));
        std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
        for (const CGClass* sub : defs) {
            auto* bb = llvm::BasicBlock::Create(
                context_,
                "propasg." + std::string(sub->stmt->name().lexeme()), fn);
            sw->addCase(builder_.getInt64(sub->id), bb);
            builder_.SetInsertPoint(bb);
            const CGField& f = sub->fields.at(sub->field_index.at(name));
            // 各臂按自身生效声明拆槽校验（S131：遮蔽冲突槽校验后还要打框，
            // 落槽形态由 field_slot_store 一并按 f.dyn_slot 决定）
            CGValue stored = field_slot_store(f, *f.decl, boxed, expr.name());
            const unsigned idx = sub->field_index.at(name) + 1; // 跳过类 id 头部
            builder_.CreateStore(
                stored.value,
                builder_.CreateStructGEP(sub->type, object.value, idx, name));
            // 臂的表达式值 = 按该臂声明类型转换后的值（对齐解释器 result_）
            incoming.emplace_back(
                builder_.GetInsertBlock(),
                to_dyn(stored, expr.name().line(), expr.name().column()));
            builder_.CreateBr(merge_bb);
        }
        builder_.SetInsertPoint(trap_bb);
        builder_.CreateCall(rt_trap_undefined_property_,
                            {builder_.CreateGlobalString(name)});
        builder_.CreateUnreachable();
        builder_.SetInsertPoint(merge_bb);
        llvm::PHINode* dyn_phi = builder_.CreatePHI(
            llvm_type_of(CGType::Dyn), static_cast<unsigned>(incoming.size()),
            "propasgdyn");
        for (const auto& in : incoming) {
            dyn_phi->addIncoming(in.second, in.first);
        }
        last_value_ = {dyn_phi, CGType::Dyn, CGType::Int, std::string()};
        return;
    }
    // 承载全等：switch 前单一转换 + 单型 PHI（t86 静态精度不丢；S131 起转换
    // 用 ref 的声明承载，落槽形态随之——冲突槽各臂物理同为 {tag,bits}）
    CGValue stored = field_slot_store(ref, *ref.decl, v, expr.name());
    llvm::Value* clsid = builder_.CreateLoad(
        builder_.getInt64Ty(), object.value, "clsid");
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* trap_bb = llvm::BasicBlock::Create(context_, "propasg.trap", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, "propasg.end", fn);
    llvm::SwitchInst* sw = builder_.CreateSwitch(
        clsid, trap_bb, static_cast<unsigned>(defs.size()));
    std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
    for (const CGClass* sub : defs) {
        auto* bb = llvm::BasicBlock::Create(
            context_,
            "propasg." + std::string(sub->stmt->name().lexeme()), fn);
        sw->addCase(builder_.getInt64(sub->id), bb);
        builder_.SetInsertPoint(bb);
        const unsigned idx = sub->field_index.at(name) + 1; // 跳过类 id 头部
        builder_.CreateStore(
            stored.value, builder_.CreateStructGEP(sub->type, object.value, idx, name));
        incoming.emplace_back(builder_.GetInsertBlock(), stored.value);
        builder_.CreateBr(merge_bb);
    }
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_undefined_property_,
                        {builder_.CreateGlobalString(name)});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(
        llvm_type_of(stored.type), static_cast<unsigned>(incoming.size()),
        "proptmp");
    for (const auto& in : incoming) {
        phi->addIncoming(in.second, in.first);
    }
    // 赋值表达式的值 = 所赋的值（与解释器一致）；Arr 字段带 Num 哨兵（t71），
    // Obj 字段带类名（t72），冲突槽带 Dyn
    last_value_ = {phi, stored.type, stored.elem, stored.cls};
}
void CodeGenerator::visitNew(const NewExpr& expr) {
    // 类实例化（t60）：collie_rt_obj_new 分配字段块（按字段类型累计上界：
    // Num {i64,i64} 记 16、其余字段 ≤ 8 字节记 8，对齐 ≤ 8，任意目标布局下
    // 恒足够，t74），再按声明顺序求值字段初始值写入 → 求值构造器实参 →
    // 调构造器（三段顺序对齐解释器 visitNew）
    const std::string name(expr.class_name().lexeme());
    size_t line = expr.class_name().line();
    size_t column = expr.class_name().column();
    auto it = classes_.find(name);
    if (it == classes_.end()) {
        unsupported("'new' of unknown class '" + name + "'", line, column);
    }
    const CGClass& cls = it->second;
    // 执行序守卫（t127/t128）：裸块/函数体内声明的类在第二遍走到声明语句前
    // 尚未"注册"（对齐解释器执行到才登记）——new 早于声明语句则拒编不错编
    // （否则 codegen 能跑、解释器 Undefined class rc=1，两端不一致）。
    // 作用域守卫（t130）：声明所属作用域（owner_scope）必在当前生成栈
    // scope_stack_ 中（含嵌套）该类方可见——函数/方法体类仅该体内可见，
    // if/while/for/do-while/switch 各 case 体类仅该块内可见；作用域外 new
    // 保守拒编（块是否执行/函数是否被调用属运行期事实，静态无从判定）
    if (!cls.ready) {
        unsupported("class '" + name + "' used before its declaration", line, column);
    }
    if (cls.owner_scope != nullptr && !scope_contains(cls.owner_scope)) {
        const std::string where =
            dynamic_cast<const FunctionStmt*>(cls.owner_scope)
                ? std::string("function '") +
                      std::string(
                          static_cast<const FunctionStmt*>(cls.owner_scope)->name().lexeme()) +
                      "'"
                : std::string("a block");
        unsupported("class '" + name + "' declared inside " + where +
                        " is not visible here",
                    line, column);
    }
    // 无初值字段守卫（t109/t125/t126）：解释器字段先绑 none、构造器随后覆写——
    // 仅当实例化类自身构造器体在 any 观察点前以 this.f = expr（RHS 不含
    // this/base，实例未逃逸故无他径可观察）覆盖全部 uninit 字段（含继承）时
    // none 态不可观察，放行并零值占位；否则拒编不错编（none 无静态表示）。
    // 走查逻辑见 ctor_definite_assign_stmt：直线定赋、this-free 表达式跳过、
    // 裸块递归、双臂 if 各臂副本求交；单臂 if / 循环 / switch / return / 声明 /
    // 观察 this 的非定赋语句一律保守停止落拒编。
    std::vector<std::string> pending;
    for (const CGField& field : cls.fields) {
        if (field.uninit) pending.push_back(field.name);
    }
    if (!pending.empty()) {
        auto cit = cls.dispatch.find(name);
        if (cit != cls.dispatch.end()) {
            const FunctionStmt* ctor_stmt = cls.instances.at(cit->second).stmt;
            ctor_definite_assign_stmts(ctor_stmt->body()->statements(), pending);
        }
        if (!pending.empty()) {
            unsupported("field '" + pending.front() +
                            "' without initializer not definitely assigned at "
                            "start of constructor of class '" + name + "'",
                        line, column);
        }
    }
    uint64_t size = 8; // i64 类 id 头部（t86）
    for (const CGField& field : cls.fields) {
        // Num/Dyn 均为 {i64,i64} 双字槽（t62/t162），其余字段 ≤ 8 字节
        size += (field.type == CGType::Num || field.type == CGType::Dyn) ? 16 : 8;
    }
    llvm::Value* obj =
        builder_.CreateCall(rt_obj_new_, {builder_.getInt64(size)}, "objnew");
    // 写类 id 头部（struct 元素 0，t86）：upcast 后方法调用点按 id 动态分派
    builder_.CreateStore(builder_.getInt64(cls.id),
                         builder_.CreateStructGEP(cls.type, obj, 0, "clsid"));
    for (unsigned i = 0; i < cls.fields.size(); ++i) {
        const CGField& field = cls.fields[i];
        // 遮蔽链（t182）：被本类同名声明覆写的父链初值仍按 base-first 顺序
        // 求值并写入同一槽（解释器同名键覆写，初值副作用可见），最后由本类
        // 声明覆写定值
        for (const VarDeclStmt* shadow_decl : field.shadowed) {
            CGValue v = emit(shadow_decl->initializer());
            // 逐层按**该层自身声明**转换（解释器 visitNew 对每层字段各调一次
            // coerce_to_declared，副作用可见、末次写入胜），冲突槽再打框落 {tag,bits}
            CGValue stored = field_slot_store(field, *shadow_decl, v,
                                              shadow_decl->name());
            builder_.CreateStore(stored.value,
                                 builder_.CreateStructGEP(cls.type, obj, i + 1,
                                                          field.name));
        }
        if (field.uninit) {
            // 无初值字段（t109）：零值占位——上方前缀定赋守卫保证赋值前
            // 不可观察，占位值任意且安全（解释器 none 同样不可观察）
            builder_.CreateStore(
                llvm::Constant::getNullValue(llvm_type_of(field.type)),
                builder_.CreateStructGEP(cls.type, obj, i + 1, field.name));
            continue;
        }
        CGValue v = emit(field.decl->initializer());
        // 字段初始值按本类生效声明类型对齐（解释器 coerce_to_declared 等价静态检查）
        CGValue stored = field_slot_store(field, *field.decl, v, field.decl->name());
        builder_.CreateStore(stored.value,
                             builder_.CreateStructGEP(cls.type, obj, i + 1, field.name));
    }
    // 构造器实参在字段初始化之后求值（对齐解释器顺序）
    std::vector<CGValue> args;
    for (const auto& argument : expr.arguments()) {
        args.push_back(emit(argument.get()));
    }
    auto dit = cls.dispatch.find(name); // 构造器 = 与类名同名成员（不继承，仅本类命中）
    if (dit == cls.dispatch.end()) {
        if (!args.empty()) {
            // 解释器运行期报错；codegen 静态可判，拒编不错编
            unsupported("constructor arguments for class '" + name +
                            "' without constructor",
                        line, column);
        }
    } else {
        const CGMethod& ctor = cls.instances.at(dit->second);
        if (args.size() != ctor.param_types.size()) {
            unsupported("constructor arity mismatch for '" + name + "'", line, column);
        }
        std::vector<llvm::Value*> call_args{obj};
        for (size_t i = 0; i < args.size(); ++i) {
            // 实参按形参类型对齐：仅 integer→decimal 提升；Obj 严格同类（t61）
            call_args.push_back(coerce_call_arg(args[i], ctor.param_types[i],
                                                ctor.param_cls[i], line, column));
        }
        builder_.CreateCall(ctor.fn, call_args);
    }
    last_value_ = {obj, CGType::Obj, CGType::Int, name};
}
void CodeGenerator::visitThis(const ThisExpr& expr) {
    // this 仅在方法体生成期可用（语义层对 object 动态放行，此处防御）
    if (!current_this_) {
        unsupported("'this' outside class method",
                    expr.keyword().line(), expr.keyword().column());
    }
    last_value_ = {current_this_, CGType::Obj, CGType::Int, current_class_name_};
}
void CodeGenerator::visitBaseCall(const BaseCallExpr& expr) {
    // 构造器委托 base(...)（t61）：按定义类的父类解析（对齐解释器
    // visitBaseCall），调父类构造器在当前分派类下的单态化实例（体内
    // this.m() 仍按实例实际类分派）；父类无构造器时 0 实参为空操作
    size_t line = expr.keyword().line();
    size_t column = expr.keyword().column();
    if (!current_this_) {
        unsupported("'base' outside class method", line, column);
    }
    const CGClass& dcls = classes_.at(current_defining_class_);
    if (dcls.super.empty()) {
        unsupported("'base' in class '" + current_defining_class_ +
                        "' without superclass",
                    line, column);
    }
    const CGClass& cls = classes_.at(current_class_name_);
    const std::string ctor_key = dcls.super + "." + dcls.super; // 父类构造器与父类名同名
    auto it = cls.instances.find(ctor_key);
    if (it == cls.instances.end()) {
        if (!expr.arguments().empty()) {
            // 解释器运行期报错；codegen 静态可判，拒编不错编
            unsupported("constructor arguments for class '" + dcls.super +
                            "' without constructor",
                        line, column);
        }
        last_value_ = {nullptr, CGType::Void}; // 空操作（解释器返 none）
        return;
    }
    const CGMethod& ctor = it->second;
    if (expr.arguments().size() != ctor.param_types.size()) {
        unsupported("constructor arity mismatch for '" + dcls.super + "'",
                    line, column);
    }
    std::vector<llvm::Value*> args{current_this_};
    for (size_t i = 0; i < expr.arguments().size(); ++i) {
        CGValue a = emit(expr.arguments()[i].get());
        args.push_back(coerce_call_arg(a, ctor.param_types[i],
                                       ctor.param_cls[i], line, column));
    }
    builder_.CreateCall(ctor.fn, args);
    last_value_ = {nullptr, CGType::Void}; // 解释器固定返 none
}
void CodeGenerator::visitBaseMethodCall(const BaseMethodCallExpr& expr) {
    // base.method(...)（t61）：从定义类的父类起静态查首个定义者（绕过
    // 子类覆写，C# 语义，对齐解释器 visitBaseMethodCall），调该定义者在
    // 当前分派类下的单态化实例（体内 this.m() 仍按实例实际类分派）
    size_t line = expr.keyword().line();
    size_t column = expr.keyword().column();
    if (!current_this_) {
        unsupported("'base' outside class method", line, column);
    }
    const CGClass& dcls = classes_.at(current_defining_class_);
    if (dcls.super.empty()) {
        unsupported("'base' in class '" + current_defining_class_ +
                        "' without superclass",
                    line, column);
    }
    const std::string name(expr.method().lexeme());
    const std::string definer = find_defining_class(dcls.super, name);
    if (definer.empty()) {
        unsupported("undefined method '" + name + "' in superclass chain of '" +
                        current_defining_class_ + "'",
                    line, column);
    }
    const CGClass& cls = classes_.at(current_class_name_);
    const CGMethod& info = cls.instances.at(definer + "." + name);
    if (expr.arguments().size() != info.param_types.size()) {
        unsupported("method arity mismatch for '" + name + "'", line, column);
    }
    std::vector<llvm::Value*> args{current_this_};
    for (size_t i = 0; i < expr.arguments().size(); ++i) {
        CGValue a = emit(expr.arguments()[i].get());
        args.push_back(coerce_call_arg(a, info.param_types[i],
                                       info.param_cls[i], line, column));
    }
    llvm::CallInst* call = builder_.CreateCall(info.fn, args);
    // Arr 返回值 elem 记 Num 哨兵（t70，同 visitCall）
    last_value_ = (info.ret_type == CGType::Void)
                      ? CGValue{nullptr, CGType::Void}
                      : CGValue{call, info.ret_type,
                                info.ret_type == CGType::Arr ? CGType::Num
                                                             : CGType::Int,
                                info.ret_cls};
}

void CodeGenerator::visitVarDecl(const VarDeclStmt& stmt) {
    // 无初始化时解释器绑 none（动态哨兵值）；静态类型放行（t92 四类型，
    // t96 扩展 number/tribool/byte/word/char/character）：槽照常创建（顶层
    // 零初始化全局槽/函数内 alloca 不预存），CGVar 记 uninit——uninit 期间
    // 读拒编（语义层流不敏感放行的未定赋读，解释器运行期仍是 none，
    // 零初始化槽会错值，拒编不错编），支配性定赋后清（t110 定赋区域
    // 跟踪：if/else 全路径定赋取交集）；Tuple 建动态 tuple 槽（t181）；其余
    // （object/未注册类名）维持拒编
    if (!stmt.initializer()) {
        const TokenType tt = stmt.type().type();
        if (tt == TokenType::KW_BYTE || tt == TokenType::KW_WORD) {
            // byte/word（t96）：i64 承载 + bit_max，赋值走 visitAssign 通用
            // 路径——既有赋值点 check_bit_range 陷阱自动生效，不丢范围校验
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(builder_.getInt64Ty(), name);
            var.type = CGType::Int;
            var.bit_max = tt == TokenType::KW_BYTE ? 255 : 65535;
            var.uninit = true;
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        if (tt == TokenType::KW_INTEGER || tt == TokenType::KW_DECIMAL ||
            tt == TokenType::KW_BOOL || tt == TokenType::KW_STRING ||
            tt == TokenType::KW_NUMBER || tt == TokenType::KW_TRIBOOL ||
            tt == TokenType::KW_CHAR || tt == TokenType::KW_CHARACTER) {
            // Num（struct{i64,i64}）/Tri（i8）/Str（ptr）零初始化全局槽均
            // 合法常量，uninit 期读拒编保证零值不可达（t96）
            CGType type = declared_cgtype(stmt.type());
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(llvm_type_of(type), name);
            var.type = type;
            var.uninit = true;
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        if (tt == TokenType::KW_ARRAY) {
            // 无初始化 array 变量（t101）：elem 无从静态推断，记 Num 动态域
            // 哨兵（t70/t88）——后续赋值走 visitAssign Arr 动态域透传（任意元素
            // 数组指针直存）、读出 kind 随对象自带并按 kind 产出 Dyn（t151）；
            // uninit 期读拒编（同块赋值后清，t92）
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(llvm_type_of(CGType::Arr), name);
            var.type = CGType::Arr;
            var.elem = CGType::Num;
            var.uninit = true;
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        if (tt == TokenType::IDENTIFIER &&
            classes_.count(std::string(stmt.type().lexeme()))) {
            // 无初始化类类型变量（t101）：Obj 槽记声明类名，后续赋值走
            // visitAssign Obj 分支（同类或子类 upcast 放行，t86）；uninit 期读
            // 拒编（同块赋值后清，t92）
            const std::string cls_name(stmt.type().lexeme());
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(llvm_type_of(CGType::Obj), name);
            var.type = CGType::Obj;
            var.cls = cls_name;
            var.uninit = true;
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        if (tt == TokenType::KW_TUPLE) {
            // 无初始化 Tuple 变量（t112 引入，t181 换表示）：槽直接建
            // CGType::TupDyn 的动态 tuple 堆对象 ptr（与 t140 有初值动态 tuple
            // 变量、t180 声明落点同表示），首赋/重赋一律走 visitAssign 尾部的
            // coerce_for_slot（allow_dyn_unbox）——静态 Tup 右值经
            // materialize_tuple_dynamic 物化、Dyn 承载位经 dyn_to_tuple_slot 的
            // tag == 8 守卫、TupDyn 透传，形状自此运行期自带（整槽替换即换形状，
            // 无须 t117 的槽组重建守卫）；原「tup 恒 -1 哨兵 + 首赋按 RHS 静态
            // 形状建解构槽组」支随之退役：槽组按静态形状建立后，动态承载位无从
            // 逐槽写入（解释器 coerce_to_declared 只看运行期 is_tuple()）
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(llvm_type_of(CGType::TupDyn), name);
            var.type = CGType::TupDyn;
            var.uninit = true;
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        if (tt == TokenType::KW_OBJECT) {
            // 无初始化 object 变量（t121 引入，t173 换表示）：槽直接建
            // CGType::Dyn 的 {tag,bits} 动态承载（与 t146 有初值路径同表示），
            // 首赋与重赋一律走 visitAssign 的 Dyn 分支（is_dyn_carrier 判定 +
            // to_dyn 打框），读出即 Dyn 落 S92–S119 全部运行期 tag 分派消费面
            // ——原 t121「Obj ptr 槽 + 首赋吸收 RHS 类名」与 t143「首赋函数值
            // 重绑 Fn 槽」两支随之退役：ptr 单字槽不可中段改 {i64,i64}，故首赋
            // 吸收后异型重赋只能保守拒编（解释器 coerce_to_declared default
            // 直存任意值）；uninit 期读仍拒编（同块赋值后清，t92/t110 定赋
            // 区域跟踪）
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(llvm_type_of(CGType::Dyn), name);
            var.type = CGType::Dyn;
            var.uninit = true;
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        unsupported("variable declaration without initializer",
                    stmt.name().line(), stmt.name().column());
    }
    // object 动态类型变量（t120 引入，t174 换表示）：解释器 coerce_to_declared
    // default 不校验任意值直存，故一律建 CGType::Dyn 的 {tag,bits} 动态承载槽
    // （无初值声明同表示，t173）——初值经 to_dyn 打 tag（实例位 tag 5 存实例
    // ptr 位模式），读出恒 Dyn 落 S92–S121 全部运行期 tag 分派消费面（方法按
    // 对象头类 id 分派 t133、字段读写按类 id 分派 t142），重赋走 visitAssign
    // Dyn 分支（is_dyn_carrier 准入 + 整槽替换）；原 t120「Obj ptr 槽 + 静态
    // 初始类名」近似退役：ptr 单字槽不可中段改 {i64,i64}
    if (stmt.type().type() == TokenType::KW_OBJECT) {
        CGValue init = emit(stmt.initializer());
        if (!is_dyn_carrier(init.type)) {
            // 承载集（标量/Dyn/数组/tuple/函数/实例/空位）之外的初值拒编不错编
            unsupported("initializing 'object' variable with non-object value",
                        stmt.name().line(), stmt.name().column());
        }
        const std::string name(stmt.name().lexeme());
        llvm::Value* slot = create_var_slot(llvm_type_of(CGType::Dyn), name);
        builder_.CreateStore(to_dyn(init, stmt.name().line(),
                                    stmt.name().column()),
                             slot);
        CGVar var{slot, CGType::Dyn};
        var.cell_id = next_cell_id_++; // 可捕获单元（t141 机制自动覆盖）
        scopes_.back()[name] = var;
        return;
    }
    // 类类型变量（t60）：声明类型为已注册类名的 IDENTIFIER token（语义层
    // 内部改写 KW_OBJECT，AST 原样保留标识符）；槽存实例 ptr，指针拷贝即引用语义
    if (stmt.type().type() == TokenType::IDENTIFIER) {
        const std::string cls_name(stmt.type().lexeme());
        if (classes_.count(cls_name) == 0) {
            unsupported("variable type '" + cls_name + "'",
                        stmt.type().line(), stmt.type().column());
        }
        CGValue init = emit(stmt.initializer());
        if (init.type == CGType::Dyn) {
            // 动态承载值初始化类名变量（t158）：运行期守卫 tag 5 + 类 id 属
            // 声明类或后代类后还原实例 ptr，槽静态 cls 记声明类（与静态初始化
            // 同规则：字段按前缀偏移解码、方法按对象头 id 动态分派）；守卫不中
            // 陷阱退出
            llvm::Value* ptr = dyn_unbox_to_obj(
                init.value, cls_name, stmt.name().line(), stmt.name().column());
            const std::string name(stmt.name().lexeme());
            llvm::Value* slot = create_var_slot(llvm_type_of(CGType::Obj), name);
            builder_.CreateStore(ptr, slot);
            CGVar var{slot, CGType::Obj, CGType::Int, cls_name};
            var.cell_id = next_cell_id_++; // 可捕获单元（t141）
            scopes_.back()[name] = var;
            return;
        }
        // 同一继承树内 upcast/downcast 均可初始化（t86/t103，静态 cls 记
        // 声明类，解释器不校验类）；跨树拒编不错编
        if (init.type != CGType::Obj ||
            (init.cls != cls_name && !is_subclass_of(init.cls, cls_name) &&
             !is_subclass_of(cls_name, init.cls))) {
            unsupported("initializing '" + cls_name + "' variable with incompatible value",
                        stmt.name().line(), stmt.name().column());
        }
        const std::string name(stmt.name().lexeme());
        llvm::Value* slot = create_var_slot(llvm_type_of(CGType::Obj), name);
        builder_.CreateStore(init.value, slot);
        CGVar var{slot, CGType::Obj, CGType::Int, cls_name};
        var.cell_id = next_cell_id_++; // 可捕获单元（t141）
        scopes_.back()[name] = var;
        return;
    }
    // byte/word 变量（t69）：i64 承载零类型扩散，仅声明/赋值点插范围陷阱
    //（对齐解释器 coerce_to_declared 只在赋值点校验、表达式域无截断）；
    // 初始值须 Int 或 Dyn 整数位（t147 拆槽：非整数态/非数值 kind 落类型陷阱，
    // 消息用 byte/word 落点名，随后既有范围陷阱接续；Double 拒编不错编）
    if (stmt.type().type() == TokenType::KW_BYTE ||
        stmt.type().type() == TokenType::KW_WORD) {
        const bool is_byte = stmt.type().type() == TokenType::KW_BYTE;
        const long long max_val = is_byte ? 255 : 65535;
        CGValue bit_init = emit(stmt.initializer());
        if (bit_init.type != CGType::Int && bit_init.type != CGType::Dyn) {
            unsupported(std::string("initializing '") + (is_byte ? "byte" : "word") +
                            "' variable with non-integer value",
                        stmt.name().line(), stmt.name().column());
        }
        llvm::Value* bit_value = bit_init.type == CGType::Int
            ? bit_init.value
            : dyn_unbox_to(bit_init.value, CGType::Int, stmt.name().line(),
                           stmt.name().column(), is_byte ? "byte" : "word");
        llvm::Value* checked = check_bit_range(bit_value, max_val,
                                               is_byte ? "byte" : "word");
        const std::string bit_name(stmt.name().lexeme());
        llvm::Value* slot = create_var_slot(builder_.getInt64Ty(), bit_name);
        builder_.CreateStore(checked, slot);
        CGVar bit_var{slot, CGType::Int, CGType::Int, "", -1, max_val};
        bit_var.cell_id = next_cell_id_++; // 可捕获单元（t141）
        scopes_.back()[bit_name] = bit_var;
        return;
    }
    CGType type = declared_cgtype(stmt.type());
    CGValue init = emit(stmt.initializer());
    CGType elem = CGType::Int;
    std::string elem_cls; // 仅 type==Arr && elem==Obj 有意义：元素类名（t100）
    llvm::Value* stored = nullptr;
    if (type == CGType::Tup &&
        (init.type == CGType::TupDyn || init.type == CGType::Dyn)) {
        // 动态 tuple 变量（t140）：初始值为三元不同形状合流/函数返回的动态对象
        // ——单 ptr 槽（引用语义，指针拷贝），形状运行期自带于堆对象；改判类型后
        // 落下方通用建槽（llvm_type_of(TupDyn)=ptr），消费面见各 TupDyn 臂。
        // 初始值为动态承载位（t180）：解释器 coerce_to_declared 的 KW_TUPLE 分支
        // 只看运行期 is_tuple()，object/Dyn 槽里的 tuple 位直存 Tuple 变量——tag 8
        // 守卫后取堆对象 ptr，其余位落运行期同消息陷阱（形状静态不可知，故本
        // 变量后续一律走动态消费面，不做静态解构）
        type = CGType::TupDyn;
        stored = init.type == CGType::Dyn ? dyn_to_tuple_slot(init.value)
                                          : init.value;
    } else if (type == CGType::Tup) {
        // Tuple 变量（t68）：解构为逐元素独立槽（无单一 slot，形状入
        // tuple_vars_）；声明无元素类型标注，形状取自初始值
        if (init.type != CGType::Tup) {
            unsupported("initializing 'Tuple' variable with non-tuple value",
                        stmt.name().line(), stmt.name().column());
        }
        const std::string name(stmt.name().lexeme());
        int idx = create_tuple_var(tuple_values_[init.tup], name);
        CGVar var{nullptr, CGType::Tup, CGType::Int, "", idx};
        var.fn_gen_at = fn_gen_count_; // 换形状全局守卫基准（t117）
        scopes_.back()[name] = var;
        return;
    }
    if (type == CGType::Arr) {
        // array 槽存不透明 ptr（指针拷贝即引用语义）；声明无元素类型标注，
        // 元素类型取自初始值的同质推断结果（t59）
        if (init.type == CGType::Dyn) {
            // 动态承载值进 array 声明槽（t165）：tag ∈ {4,7} 守卫后取数组 ptr，
            // 运行期 kind 随对象自带 → elem 记 Num 动态域哨兵（t70 同规则，
            // 读写/打印/判等全部走既有动态域路径）；其余位落运行期同消息陷阱
            stored = dyn_to_arr_slot(init.value);
            elem = CGType::Num;
        } else if (init.type != CGType::Arr) {
            unsupported("initializing 'array' variable with non-array value",
                        stmt.name().line(), stmt.name().column());
        } else {
            stored = init.value;
            elem = init.elem;
            elem_cls = init.cls; // Obj 元素数组：拷贝元素类名（t100）
        }
    } else {
        stored = coerce_for_slot(init, type, stmt.name());
    }
    const std::string name(stmt.name().lexeme());
    llvm::Value* slot = create_var_slot(llvm_type_of(type), name);
    builder_.CreateStore(stored, slot);
    CGVar var{slot, type, elem, elem_cls}; // 同名直接遮蔽（重复声明由语义层拦截）
    var.loop_depth = loops_.size();        // 异型互赋降级限同层（t106）
    var.fn_gen_at = fn_gen_count_;         // 全局槽快照守卫基准（t106）
    var.cell_id = next_cell_id_++;         // 可捕获单元（t141）
    scopes_.back()[name] = var;
}

void CodeGenerator::visitBlock(const BlockStmt& stmt) {
    scopes_.emplace_back();
    for (const auto& inner : stmt.statements()) {
        inner->accept(*this);
    }
    scopes_.pop_back();
}

void CodeGenerator::visitIf(const IfStmt& stmt) {
    // 条件必须为 bool（t43c 语义层已强制，此处防御）
    CGValue cond = emit(stmt.condition());
    if (cond.type != CGType::Bool) {
        unsupported("non-bool 'if' condition",
                    stmt.if_token().line(), stmt.if_token().column());
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* then_bb = llvm::BasicBlock::Create(context_, "if.then", fn);
    auto* else_bb = stmt.else_branch()
                        ? llvm::BasicBlock::Create(context_, "if.else", fn)
                        : nullptr;
    auto* merge_bb = llvm::BasicBlock::Create(context_, "if.end", fn);
    builder_.CreateCondBr(cond.value, then_bb, else_bb ? else_bb : merge_bb);

    // 定赋区域跟踪（t110）：分支为条件发射区，快照隔离区内 uninit 清除；
    // 有 else 时运行期必走其一，汇合点对两分支清除集取交集（终止支
    // 不达汇合点，不约束交集）——if/else 全路径定赋后读放行，对齐解释器
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编
    // 句柄解析：快照变量居外层作用域，分支只 push/pop 更深层，恒可寻得
    auto snap_var = [&](const std::pair<size_t, std::string>& h) -> CGVar* {
        auto found = scopes_[h.first].find(h.second);
        return found == scopes_[h.first].end() ? nullptr : &found->second;
    };
    builder_.SetInsertPoint(then_bb);
    {
        ScopePushGuard then_guard(scope_stack_, stmt.then_branch()); // t130：then 块作用域
        stmt.then_branch()->accept(*this);
    }
    // 终止支检测：return/break/continue 后插入点被切到无前驱死块
    //（visitReturn 等的 *.dead 块），getTerminator 恒空——补判无前驱；
    // 非死支末块必有前驱（then/else 入口块有 CondBr 前驱）
    auto branch_terminated = [&]() {
        llvm::BasicBlock* bb = builder_.GetInsertBlock();
        return bb->getTerminator() != nullptr || bb->hasNPredecessors(0);
    };
    const bool then_terminated = branch_terminated();
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(merge_bb);
    }
    std::vector<bool> then_cleared(uninit_snap.size(), false);
    for (size_t i = 0; i < uninit_snap.size(); ++i) {
        CGVar* v = snap_var(uninit_snap[i]);
        then_cleared[i] = v != nullptr && !v->uninit;
    }
    uninit_restore(uninit_snap);
    if (else_bb) {
        builder_.SetInsertPoint(else_bb);
        {
            ScopePushGuard else_guard(scope_stack_, stmt.else_branch()); // t130：else 块作用域
            stmt.else_branch()->accept(*this);
        }
        const bool else_terminated = branch_terminated();
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(merge_bb);
        }
        std::vector<bool> else_cleared(uninit_snap.size(), false);
        for (size_t i = 0; i < uninit_snap.size(); ++i) {
            CGVar* v = snap_var(uninit_snap[i]);
            else_cleared[i] = v != nullptr && !v->uninit;
        }
        uninit_restore(uninit_snap);
        for (size_t i = 0; i < uninit_snap.size(); ++i) {
            const bool by_then = then_terminated || then_cleared[i];
            const bool by_else = else_terminated || else_cleared[i];
            if (by_then && by_else) {
                if (CGVar* v = snap_var(uninit_snap[i])) {
                    v->uninit = false;
                }
            }
        }
    }
    builder_.SetInsertPoint(merge_bb);
}

void CodeGenerator::visitWhile(const WhileStmt& stmt) {
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* cond_bb = llvm::BasicBlock::Create(context_, "while.cond", fn);
    auto* body_bb = llvm::BasicBlock::Create(context_, "while.body", fn);
    auto* end_bb = llvm::BasicBlock::Create(context_, "while.end", fn);
    builder_.CreateBr(cond_bb);

    // 条件发射区（t110）：循环条件/体回边多次、体可能零次执行，
    // 区内 uninit 清除隔离不外泄
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编
    builder_.SetInsertPoint(cond_bb);
    CGValue cond = emit(stmt.condition());
    if (cond.type != CGType::Bool) {
        unsupported("non-bool 'while' condition",
                    stmt.while_token().line(), stmt.while_token().column());
    }
    builder_.CreateCondBr(cond.value, body_bb, end_bb);

    builder_.SetInsertPoint(body_bb);
    loops_.push_back({cond_bb, end_bb}); // break/continue 目标（S4 t51）
    {
        ScopePushGuard body_guard(scope_stack_, stmt.body()); // t130：while 体作用域
        stmt.body()->accept(*this);
    }
    loops_.pop_back();
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(cond_bb);
    }
    uninit_restore(uninit_snap);
    builder_.SetInsertPoint(end_bb);
}

void CodeGenerator::visitFor(const ForStmt& stmt) {
    // for 的初始化变量作用域限循环内（与解释器 ScopeGuard 对齐）
    scopes_.emplace_back();
    if (stmt.initializer()) {
        stmt.initializer()->accept(*this);
    }
    // 条件发射区（t110）：初始化语句无条件执行定赋外泄合法，
    // 条件/体/增量区内 uninit 清除隔离
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* cond_bb = llvm::BasicBlock::Create(context_, "for.cond", fn);
    auto* body_bb = llvm::BasicBlock::Create(context_, "for.body", fn);
    auto* inc_bb = stmt.increment()
                       ? llvm::BasicBlock::Create(context_, "for.inc", fn)
                       : nullptr;
    auto* end_bb = llvm::BasicBlock::Create(context_, "for.end", fn);
    builder_.CreateBr(cond_bb);

    builder_.SetInsertPoint(cond_bb);
    if (stmt.condition()) {
        CGValue cond = emit(stmt.condition());
        if (cond.type != CGType::Bool) {
            unsupported("non-bool 'for' condition",
                        stmt.for_token().line(), stmt.for_token().column());
        }
        builder_.CreateCondBr(cond.value, body_bb, end_bb);
    } else {
        builder_.CreateBr(body_bb); // 无条件 = 恒真（与解释器一致）
    }

    builder_.SetInsertPoint(body_bb);
    // continue 跳增量块（无增量则条件块），与解释器“continue 后仍执行 increment”对齐
    loops_.push_back({inc_bb ? inc_bb : cond_bb, end_bb});
    {
        ScopePushGuard body_guard(scope_stack_, stmt.body()); // t130：for 体作用域
        stmt.body()->accept(*this);
    }
    loops_.pop_back();
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(inc_bb ? inc_bb : cond_bb);
    }
    if (inc_bb) {
        builder_.SetInsertPoint(inc_bb);
        emit(stmt.increment()); // 值弃用
        builder_.CreateBr(cond_bb);
    }
    uninit_restore(uninit_snap);
    builder_.SetInsertPoint(end_bb);
    scopes_.pop_back();
}

void CodeGenerator::visitDoWhile(const DoWhileStmt& stmt) {
    // 先执行一次循环体再判条件；continue 跳条件块（与解释器对齐）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* body_bb = llvm::BasicBlock::Create(context_, "do.body", fn);
    auto* cond_bb = llvm::BasicBlock::Create(context_, "do.cond", fn);
    auto* end_bb = llvm::BasicBlock::Create(context_, "do.end", fn);
    builder_.CreateBr(body_bb);

    // 条件发射区（t110）：体内 break 可跳过体尾/条件，区内 uninit
    // 清除隔离不外泄（体必执行一次但非全体支配，保守隔离）；
    // 但体内无本层 break/continue 时体末无条件定赋线性支配 cont→end
    //（体必执行一次，嵌套 if/else 部分定赋已由 visitIf 交集处理），
    // 可安全外泄（t113：不 restore，体末清除集即到达出口必定赋集）
    const bool body_escapes = has_loop_escape(stmt.body());
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编
    builder_.SetInsertPoint(body_bb);
    loops_.push_back({cond_bb, end_bb});
    {
        ScopePushGuard body_guard(scope_stack_, stmt.body()); // t130：do-while 体作用域
        stmt.body()->accept(*this);
    }
    loops_.pop_back();
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(cond_bb);
    }

    builder_.SetInsertPoint(cond_bb);
    CGValue cond = emit(stmt.condition());
    if (cond.type != CGType::Bool) {
        unsupported("non-bool 'do-while' condition",
                    stmt.do_token().line(), stmt.do_token().column());
    }
    builder_.CreateCondBr(cond.value, body_bb, end_bb);
    if (body_escapes) {
        uninit_restore(uninit_snap);
    }
    builder_.SetInsertPoint(end_bb);
}

void CodeGenerator::visitSwitch(const SwitchStmt& stmt) {
    const Token& tok = stmt.switch_token();
    // 条件只求值一次；级联比较块链（gen_multi_match 的语句版，无结果 PHI），
    // 候选按 case 序/值序惰性求值、首命中即执行 body 后结束（无 fallthrough），
    // default 位置无关最后兜底，均对齐解释器 visitSwitch
    CGValue cond = emit(stmt.condition());
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* end_bb = llvm::BasicBlock::Create(context_, "switch.end", fn);
    // 条件发射区（t110）：比较链候选惰性求值、各 body 互为替代路径，
    // 区内 uninit 清除逐段隔离不外泄
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编

    const SwitchCase* default_case = nullptr;
    struct Pending {
        llvm::BasicBlock* body_bb;
        const Stmt* body;
    };
    std::vector<Pending> bodies;
    for (const auto& sc : stmt.cases()) {
        if (sc.is_default) {
            default_case = &sc; // 非 default 分支优先，链尾兜底
            continue;
        }
        auto* body_bb = llvm::BasicBlock::Create(context_, "switch.body", fn);
        for (const auto& value : sc.values) {
            CGValue cand = emit(value.get());
            llvm::Value* eq = gen_match_eq(cond, cand, tok);
            auto* next_bb = llvm::BasicBlock::Create(context_, "switch.next", fn);
            builder_.CreateCondBr(eq, body_bb, next_bb);
            builder_.SetInsertPoint(next_bb);
        }
        bodies.push_back({body_bb, sc.body.get()});
    }
    // 全部未命中：跳 default body（无 default 或其 body 为空则直接结束）
    llvm::BasicBlock* default_bb = end_bb;
    if (default_case != nullptr && default_case->body != nullptr) {
        default_bb = llvm::BasicBlock::Create(context_, "switch.default", fn);
        bodies.push_back({default_bb, default_case->body.get()});
    }
    builder_.CreateBr(default_bb);
    uninit_restore(uninit_snap); // 候选链清除不流入 body（后段候选不支配前段 body）

    // 各 body（BlockStmt 自带作用域）执行后跳 end；body 内 break/continue
    // 维持绑定外层循环（解释器 switch 不捕获 BreakSignal，loop 栈不动）
    for (const auto& p : bodies) {
        builder_.SetInsertPoint(p.body_bb);
        if (p.body != nullptr) {
            ScopePushGuard case_guard(scope_stack_, p.body); // t130：switch case 体作用域
            p.body->accept(*this);
        }
        uninit_restore(uninit_snap); // body 间互为替代路径，逐个隔离
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(end_bb);
        }
    }
    builder_.SetInsertPoint(end_bb);
}

void CodeGenerator::visitFunction(const FunctionStmt& stmt) {
    // 函数体生成（第二遍，S5 t52）：原型已在第一遍建好；
    // 嵌套函数（t91/t104）按改编键查表——函数体与类方法体内的嵌套均已
    // 登记，未登记位置维持拒编（防御）
    const std::string name(stmt.name().lexeme());
    auto nested_it = nested_fns_.find(&stmt);
    const bool is_nested = nested_it != nested_fns_.end();
    if (!is_nested && in_function_) {
        unsupported("nested function declaration",
                    stmt.name().line(), stmt.name().column());
    }
    const std::string& key = fn_stmt_to_key_.at(&stmt);
    // 同名遮蔽：仅胜出定义生成体（t131，对齐解释器 env_.define 覆盖——
    // 被遮蔽的前定义从不调用，跳过体生成，避免重复生成到同一 llvm 函数）
    if (&stmt != winning_stmt_[key]) {
        return;
    }
    auto it = functions_.find(key);
    if (it == functions_.end()) {
        // 非顶层位置的函数声明（顶层块内等）未进原型表
        unsupported("function declaration outside top level",
                    stmt.name().line(), stmt.name().column());
    }
    const CGFunction& info = it->second;

    if (is_nested) {
        // 声明处登记可见性绑定（t91，对齐解释器"执行到声明处 env_.define"：
        // 声明前调用不可见、遮蔽顶层同名、所在块退出即失效）
        CGVar binding;
        binding.fn_key = key;
        scopes_.back()[name] = binding;
    }
    if (is_nested && !info.fn->empty()) {
        // 方法体单态化副本重访（t104）：同一方法体按分派类多次生成，嵌套
        // 函数体与分派类无关（体内 this/字段/外层局部均不可见），首个副本
        // 已生成完整函数体，重访仅需登记可见性绑定
        return;
    }

    // 闭包按引用捕获物化（t141）：仅普通函数/块作用域内的嵌套函数——宿主
    // 非类方法（current_this_ 非空即处于方法体，其单态化副本各帧局部槽地址
    // 不同，跨副本共享捕获 ptr 会悬垂，保守维持体内标识符不可见拒编，同 t104）。
    // 须在搬迁宿主 scopes_ 之前调用：自由变量对宿主作用域的归属此刻才可得。
    if (is_nested && current_this_ == nullptr) {
        build_closure_prototype(stmt, key);
    }

    // 保存 @main（或外层函数）生成现场，函数体用独立的变量环境/循环栈；
    // 顶层作用域拷贝为链底（t73）：全局槽（GlobalVariable）跨函数可见；
    // Tup 条目一并拷贝（t76：顶层 tuple 解构槽组已升全局槽，tuple_vars_
    // 注册表为成员跨函数存活，函数内读/重赋值合法）；
    // 嵌套生成现场（t91）：in_function_/返回类型改为保存恢复（嵌套声明处
    // 生成完毕须还原外层函数上下文，而非复位顶层）
    llvm::BasicBlock* saved_bb = builder_.GetInsertBlock();
    auto saved_scopes = std::move(scopes_);
    auto saved_loops = std::move(loops_);
    const bool saved_in_function = in_function_;
    const FunctionStmt* saved_fn = current_fn_; // t128：宿主函数语境保存恢复
    ++fn_gen_count_; // 全局数组槽 elem 快照守卫（t106）
    const CGType saved_ret_type = current_ret_type_;
    const std::string saved_ret_cls = current_ret_cls_;
    const long long saved_ret_bit_max = current_ret_bit_max_;
    // 方法体内嵌套（t104）：this 上下文清空——嵌套体是独立函数（无 this
    // 隐藏参），体内 this/base 落 "'this' outside class method" 拒编不错编
    // （同 t91 外层局部捕获面范围外），不清空会跨函数引用方法 this 实参
    llvm::Value* saved_this = current_this_;
    const std::string saved_class_name = current_class_name_;
    const std::string saved_defining_class = current_defining_class_;
    current_this_ = nullptr;
    current_class_name_.clear();
    current_defining_class_.clear();
    scopes_.clear();
    scopes_.emplace_back();
    if (!saved_scopes.empty()) {
        scopes_.back() = saved_scopes.front();
        // 外层链上的函数绑定拷入链底（t91）：解释器动态作用域下"声明先于
        // 调用"即可见（含自身递归/前置兄弟嵌套）；变量槽不拷——嵌套体内
        // 引用外层局部即标识符不可见，拒编不错编（捕获面范围外）
        for (size_t si = 1; si < saved_scopes.size(); ++si) {
            for (const auto& entry : saved_scopes[si]) {
                if (!entry.second.fn_key.empty()) {
                    scopes_.back()[entry.first] = entry.second;
                }
            }
        }
    }
    scopes_.emplace_back(); // 参数层（可遮蔽全局）
    loops_.clear();
    in_function_ = true;
    current_fn_ = &stmt; // t128：函数体内声明的类以本函数为宿主
    ScopePushGuard scope_guard(scope_stack_, &stmt); // t130：进入函数体作用域
    current_ret_type_ = info.ret_type;
    current_ret_cls_ = info.ret_cls;
    current_ret_bit_max_ = info.ret_bit_max;

    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", info.fn));
    // 形参落栈槽（与局部变量同机制，可被赋值/遮蔽）；闭包捕获项（t141）先行
    // 绑定为「无 alloca 的 ptr 形参」——它们直接是宿主单元地址，体内读写经此
    // 指针透传到宿主槽（按引用共享，对齐解释器闭包共享变量单元语义：内层改
    // 外层可见、外层改内层迟绑定可见）。捕获先入参数层，同名形参随后覆盖即
    // 遮蔽（对齐解释器形参遮蔽外层同名；被遮蔽的捕获成死参，无害）。
    std::vector<llvm::Argument*> all_args;
    for (auto& arg : info.fn->args()) all_args.push_back(&arg);
    const size_t nparams = stmt.parameters().size();
    for (size_t j = 0; j < info.captures.size(); ++j) {
        const CGFunction::Capture& cap = info.captures[j];
        llvm::Argument* slot = all_args[nparams + j];
        slot->setName(cap.name);
        CGVar binding;
        binding.slot = slot; // 宿主单元地址即槽，无本地 alloca
        binding.type = cap.type;
        binding.elem = cap.elem;
        binding.cls = cap.cls;
        binding.bit_max = cap.bit_max;
        binding.cell_id = cap.cell_id; // 透传宿主单元 id：体内更深嵌套/自递归转发时
                                        // visitCall 据 id 一致性取本 ptr 再传
        binding.uninit = cap.uninit;   // 宿主登记时未定赋 → 体内读保守拒编（同 t92）
        scopes_.back()[cap.name] = binding;
    }
    for (size_t i = 0; i < nparams; ++i) {
        const Parameter& param = stmt.parameters()[i];
        const std::string pname(param.name.lexeme());
        llvm::Argument* arg = all_args[i];
        arg->setName(pname);
        llvm::AllocaInst* slot =
            create_entry_alloca(llvm_type_of(info.param_types[i]), pname);
        builder_.CreateStore(arg, slot);
        // Arr 形参 elem 记 Num 哨兵（t70：签名处元素类型不可知，动态域路径）
        CGVar binding{slot, info.param_types[i],
                      info.param_types[i] == CGType::Arr ? CGType::Num
                                                         : CGType::Int,
                      info.param_cls[i]};
        binding.cell_id = next_cell_id_++; // 形参亦是可捕获单元（t141）
        // byte/word 形参（t98）：置 bit_max，体内重赋走既有赋值点 check_bit_range
        const TokenType ptt = param.type.type();
        if (ptt == TokenType::KW_BYTE || ptt == TokenType::KW_WORD) {
            binding.bit_max = ptt == TokenType::KW_BYTE ? 255 : 65535;
        }
        scopes_.back()[pname] = binding;
    }

    for (const auto& body_stmt : stmt.body()->statements()) {
        body_stmt->accept(*this);
    }

    // 尾块收尾：none 函数补 ret void（与解释器无显式 return 返 none 对齐）；
    // 非 none 函数的不可达尾块（各分支均已 return）补 unreachable，
    // 可达尾块无 return 则拒编（解释器此处返 none，静态返回类型无对应表示）
    llvm::BasicBlock* tail = builder_.GetInsertBlock();
    if (!tail->getTerminator()) {
        if (info.ret_type == CGType::Void) {
            builder_.CreateRetVoid();
        } else if (!reachable_from_entry(tail)) {
            builder_.CreateUnreachable();
        } else {
            throw CodeGenError(
                "codegen: function '" + name + "' may reach end without return",
                stmt.name().line(), stmt.name().column());
        }
    }

    in_function_ = saved_in_function;
    current_fn_ = saved_fn;
    current_ret_type_ = saved_ret_type;
    current_ret_cls_ = saved_ret_cls;
    current_ret_bit_max_ = saved_ret_bit_max;
    current_this_ = saved_this;
    current_class_name_ = saved_class_name;
    current_defining_class_ = saved_defining_class;
    scopes_ = std::move(saved_scopes);
    loops_ = std::move(saved_loops);
    builder_.SetInsertPoint(saved_bb);
}

void CodeGenerator::visitReturn(const ReturnStmt& stmt) {
    if (!in_function_) {
        unsupported("'return' outside function",
                    stmt.keyword().line(), stmt.keyword().column());
    }
    if (current_ret_type_ == CGType::Void) {
        // none 函数：仅允许裸 return（带值属语义层拦截面，此处防御）
        if (stmt.value()) {
            unsupported("return value in 'none' function",
                        stmt.keyword().line(), stmt.keyword().column());
        }
        builder_.CreateRetVoid();
    } else {
        if (!stmt.value()) {
            unsupported("missing return value",
                        stmt.keyword().line(), stmt.keyword().column());
        }
        CGValue v = emit(stmt.value());
        // 返回值按声明返回类型对齐：仅 integer→decimal 提升（与解释器 t37 一致）；
        // Obj 要求类名严格相等（向上转型拒编，t61）
        if (v.type != current_ret_type_) {
            if (current_ret_type_ == CGType::Double && v.type == CGType::Int) {
                v = {to_double(v), CGType::Double};
            } else if (current_ret_type_ == CGType::Int &&
                       v.type == CGType::Num) {
                // number → integer 窄化返回（t111）：小数态运行期陷阱
                v = {num_to_int_checked(v.value), CGType::Int};
            } else if (current_ret_type_ == CGType::Double &&
                       v.type == CGType::Num) {
                // number → decimal 窄化返回（t111）：双态皆合法
                v = {to_double(v), CGType::Double};
            } else if (current_ret_type_ == CGType::Num &&
                       (v.type == CGType::Int || v.type == CGType::Double)) {
                // integer/decimal → number 加宽（t62）：保持原表示打 tag
                v = {to_num(v), CGType::Num};
            } else if (current_ret_type_ == CGType::Tri && v.type == CGType::Bool) {
                // bool → tribool 单向加宽（t65，与语义层一致）
                v = {to_tri(v), CGType::Tri};
            } else if (current_ret_type_ == CGType::Str &&
                       (v.type == CGType::Int || v.type == CGType::Double ||
                        v.type == CGType::Num || v.type == CGType::Bool)) {
                // 隐式 number/bool → string（t131，对齐解释器 coerce_to_declared
                // KW_STRING 分支）
                v = {coerce_to_string_value(v), CGType::Str};
            } else if (current_ret_type_ == CGType::TupDyn &&
                       v.type == CGType::Tup) {
                // 静态 tuple 返回到动态 tuple 返回类型（t140）：物化堆对象再 ret
                v = {materialize_tuple_dynamic(v.tup, stmt.keyword().line(),
                                               stmt.keyword().column()),
                     CGType::TupDyn};
            } else if (current_ret_type_ == CGType::TupDyn &&
                       v.type == CGType::Dyn) {
                // 动态承载值作 Tuple 返回值（t180）：解释器在 return 处按声明
                // 返回类型走同一 coerce_to_declared 的 KW_TUPLE 分支，只要求
                // is_tuple()——tag 8 守卫后直传堆对象，其余位运行期同消息陷阱
                v = {dyn_to_tuple_slot(v.value), CGType::TupDyn};
            } else if (v.type == CGType::Dyn &&
                       is_dyn_unbox_target(current_ret_type_)) {
                // 动态承载值作返回值（t147）：解释器在 return 处按声明返回
                // 类型走同一 coerce_to_declared 校验，tag 不符运行期陷阱
                v = {dyn_unbox_to(v.value, current_ret_type_,
                                  stmt.keyword().line(),
                                  stmt.keyword().column(),
                                  bit_type_name(current_ret_bit_max_)),
                     current_ret_type_};
            } else if (v.type == CGType::Dyn &&
                       current_ret_type_ == CGType::Arr) {
                // 动态承载值作 array 返回值（t165）：解释器在 return 处走
                // coerce_to_declared 的 KW_ARRAY 分支，只要求 is_array()；调用侧
                // Arr 返回值 elem 恒记 Num 动态域哨兵（t70），守卫后取 ptr 直传
                v = {dyn_to_arr_slot(v.value), CGType::Arr, CGType::Num};
            } else if (v.type == CGType::Dyn &&
                       current_ret_type_ == CGType::Obj &&
                       !current_ret_cls_.empty()) {
                // 动态承载值作类名返回值（t158）：解释器在 return 处对类名
                // default 直存不校验，codegen 需运行期守卫 tag 5 + 实例类 id 属
                // 声明返回类或其后代类才可静态解码；守卫不中陷阱退出
                v = {dyn_unbox_to_obj(v.value, current_ret_cls_,
                                      stmt.keyword().line(),
                                      stmt.keyword().column()),
                     CGType::Obj, CGType::Int, current_ret_cls_};
            } else if (current_ret_type_ == CGType::Dyn &&
                       is_dyn_carrier(v.type)) {
                // 任意可承载返回值出 object 返回类型（t161，S108）：解释器对
                // object 返回走 coerce_to_declared default 直存；codegen 统一打
                // {tag,bits}，调用侧按 Dyn 既有消费面（打印/长度/下标/成员/方法）取用
                v = {to_dyn(v), CGType::Dyn};
            } else {
                unsupported("return type mismatch",
                            stmt.keyword().line(), stmt.keyword().column());
            }
        } else if (v.type == CGType::Obj && !current_ret_cls_.empty() &&
                   v.cls != current_ret_cls_ &&
                   !is_subclass_of(v.cls, current_ret_cls_) &&
                   !is_subclass_of(current_ret_cls_, v.cls)) {
            // 同一继承树内 upcast/downcast 放行（t86/t103）；跨树拒编不错编
            unsupported("returning instance of class '" + v.cls +
                            "' where '" + current_ret_cls_ + "' is declared",
                        stmt.keyword().line(), stmt.keyword().column());
        }
        // Arr 返回值任意元素透传（t88 解除 t70 数值系守卫：kind 随数组对象
        // 自带，print/len/== rt 侧全 kind 覆盖；索引读按运行期 kind 产出 Dyn，t151）
        if (current_ret_bit_max_ > 0) {
            // byte/word 返回类型（t97）：值已对齐 Int，赋值点式插 check_bit_range
            // ——越界调 rt_trap_bit_range 报错退出，对齐解释器 coerce_to_declared
            // 返回值范围校验（核心消息一致，位置前缀缺失同既定 CG 陷阱分歧）
            v = {check_bit_range(v.value, current_ret_bit_max_,
                                 current_ret_bit_max_ == 255 ? "byte" : "word"),
                 CGType::Int};
        }
        builder_.CreateRet(v.value);
    }
    // return 后同块死代码仍需插入点（与 break/continue 同机制）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "ret.dead", fn));
}

void CodeGenerator::visitClass(const ClassStmt& stmt) {
    // 方法体生成（第二遍，t60/t61）：struct 布局与方法原型已在注册遍建好；
    // 沿继承链自子向父为本类分派上下文的全部单态化实例生成方法体
    // （collie.C.D.m：分派类 C = 本类，定义类 D = 链上各类），继承而来
    // 的方法在本类语境下重新生成一份——体内 this.m() 按本类分派表解析，
    // 模板方法模式得以正确（与解释器动态分派等价）
    const std::string name(stmt.name().lexeme());
    auto it = classes_.find(name);
    if (it == classes_.end()) {
        // 未进类表：函数/方法体内未在收集范围的位置（if/while/for/switch 体内等）
        if (in_function_) {
            unsupported("class declaration inside function",
                        stmt.name().line(), stmt.name().column());
        }
        // 非顶层位置且非裸块内的类声明（if/while/for/switch 体内）未进类表
        unsupported("class declaration outside top level",
                    stmt.name().line(), stmt.name().column());
    }
    CGClass& cls = it->second;
    // 执行序到声明处（t127）：裸块/函数体内类自此可实例化——对齐解释器
    // visitClass 执行到声明语句才登记 classes_（此前 new 即 Undefined class rc=1）
    cls.ready = true;
    if (cls.bodies_generated) {
        // 方法体已生成（t128）：同一函数体单态化副本重访（t104 同类机制）——
        // 重复建 entry 块会产生无效 IR，直接返回
        return;
    }
    cls.bodies_generated = true;
    for (const CGClass* c = &cls; c != nullptr;
         c = c->super.empty() ? nullptr : &classes_.at(c->super)) {
        const std::string dname(c->stmt->name().lexeme());
        for (const auto& member : c->stmt->members()) {
            if (const auto* method = dynamic_cast<const FunctionStmt*>(member.get())) {
                const CGMethod& m = cls.instances.at(
                    dname + "." + std::string(method->name().lexeme()));
                // 同名重载仅首个登记的成员生成体（t123，first-wins）：其余同名成员
                // stmt 指针不等即跳过，避免同一 llvm::Function 被重复建 entry 块
                if (m.stmt != method) continue;
                gen_method_body(cls, m);
            }
        }
    }
}

void CodeGenerator::visitBreak(const BreakStmt& stmt) {
    // 循环外的 break 由语义层拦截，此处防御
    if (loops_.empty()) {
        unsupported("'break' outside loop",
                    stmt.keyword().line(), stmt.keyword().column());
    }
    builder_.CreateBr(loops_.back().break_target);
    // break 后同块的死代码仍需插入点（IR 块只允许一个终结符）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "break.dead", fn));
}

void CodeGenerator::visitContinue(const ContinueStmt& stmt) {
    if (loops_.empty()) {
        unsupported("'continue' outside loop",
                    stmt.keyword().line(), stmt.keyword().column());
    }
    builder_.CreateBr(loops_.back().continue_target);
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "continue.dead", fn));
}

// ---------------- 辅助 ----------------

CodeGenerator::CGType CodeGenerator::declared_cgtype(const Token& type_token) {
    switch (type_token.type()) {
        case TokenType::KW_INTEGER: return CGType::Int;
        case TokenType::KW_DECIMAL: return CGType::Double;
        case TokenType::KW_BOOL:    return CGType::Bool;
        case TokenType::KW_STRING:  return CGType::Str;
        case TokenType::KW_CHAR:
        case TokenType::KW_CHARACTER:
            // char/character（t69）：解释器运行期即 string（coerce_to_declared
            // 走 default 不校验），Str 承载零新触点；KW_BYTE/KW_WORD 不在此
            // 映射（变量声明另有前置分支插范围陷阱，类字段/函数签名维持
            // 拒编，不静默丢范围校验）
            return CGType::Str;
        case TokenType::KW_ARRAY:   return CGType::Arr; // 元素类型由初始值推断（t59）
        case TokenType::KW_NUMBER:  return CGType::Num; // tagged 双表示（t62，CG5 收窄）
        case TokenType::KW_TRIBOOL: return CGType::Tri; // i8 三态编码（t65）
        case TokenType::KW_TUPLE:   return CGType::Tup; // 静态展开虚值（t68，形状取自初始值）
        default:
            unsupported("variable type '" + std::string(type_token.lexeme()) + "'",
                        type_token.line(), type_token.column());
    }
}

void CodeGenerator::declared_signature_type(const Token& type_token,
                                            CGType& type_out, std::string& cls_out) {
    // 函数/方法签名类型（t61）：IDENTIFIER 视为类名（实例作参数/返回值），
    // 须已注册（类声明在前）；其余走 declared_cgtype 标量面
    if (type_token.type() == TokenType::KW_TUPLE) {
        // tuple 进函数签名（t140）：静态展开形状跨函数边界不可知，改以动态
        // tuple 堆对象承载（CGType::TupDyn，ptr）——实参/返回值在调用/返回点
        // 物化；体内消费走各 TupDyn 臂，非 dyn_scalar 元素在物化处拒编不错编
        type_out = CGType::TupDyn;
        cls_out.clear();
        return;
    }
    if (type_token.type() == TokenType::KW_OBJECT) {
        // object 动态类型作函数形参/返回值（t132 起为 CGType::Obj 空 cls，
        // 仅能承载实例）→ 动态承载 {tag,bits}（t161，S108）：解释器对 object
        // 签名按 coerce_to_declared default 直存任意值，形参/返回值运行期可带
        // 标量、数组或实例，故与 object 变量（t146/t159/t160 的 Dyn 槽）统一表示。
        // 实例位打 tag 5 后仍并入既有空 cls 动态分派（t133/t142/t145 经
        // dyn_obj_guard），方法/字段面零回归；串/数组/tribool 等标量位经 S92–S107
        // 全部既有 tag 分派消费。Tup/Fn/Void 无 {tag,bits} 编码，维持拒编不错编
        type_out = CGType::Dyn;
        cls_out.clear();
        return;
    }
    if (type_token.type() == TokenType::IDENTIFIER) {
        const std::string cname(type_token.lexeme());
        if (classes_.count(cname) == 0) {
            unsupported("signature type '" + cname + "'",
                        type_token.line(), type_token.column());
        }
        type_out = CGType::Obj;
        cls_out = cname;
        return;
    }
    type_out = declared_cgtype(type_token);
    cls_out.clear();
}

llvm::Value* CodeGenerator::coerce_to_string_value(const CGValue& v) {
    // 隐式 number/bool → string（t131，对齐解释器 coerce_to_declared KW_STRING
    // 分支）；调用方须先确保 v.type ∈ {Int, Double, Num, Bool}
    switch (v.type) {
        case CGType::Int:
            return builder_.CreateCall(rt_i64_to_str_, {v.value}, "i64str");
        case CGType::Double:
            return builder_.CreateCall(rt_f64_to_str_, {v.value}, "f64str");
        case CGType::Num:
            return builder_.CreateCall(
                rt_num_to_str_, {num_tag(v.value), num_bits(v.value)}, "numstr");
        case CGType::Bool: {
            llvm::Value* ext =
                builder_.CreateZExt(v.value, builder_.getInt32Ty());
            return builder_.CreateCall(rt_bool_to_str_, {ext}, "boolstr");
        }
        default:
            return nullptr;  // 不应到达：调用方已判类型
    }
}

llvm::Value* CodeGenerator::coerce_call_arg(const CGValue& a, CGType want,
                                            const std::string& want_cls,
                                            size_t line, size_t column) {
    // 实参对齐形参类型：仅 integer→decimal 提升；Obj 允许同一继承树内
    // upcast/downcast（t86/t103，指针原样传递，调用点按对象头类 id 动态
    // 分派/陷阱保语义），跨树拒编不错编
    if (a.type == want) {
        if (want == CGType::Obj && !want_cls.empty() && a.cls != want_cls &&
            !is_subclass_of(a.cls, want_cls) &&
            !is_subclass_of(want_cls, a.cls)) {
            unsupported("passing instance of class '" + a.cls +
                            "' where '" + want_cls + "' is expected",
                        line, column);
        }
        // Arr 实参任意元素透传（t88 解除 t70 数值系守卫：kind 随数组对象
        // 自带，print/len/== rt 侧全 kind 覆盖；索引读按运行期 kind 产出 Dyn，t151）
        return a.value;
    }
    if (want == CGType::Double && a.type == CGType::Int) {
        return to_double(a);
    }
    // number → integer/decimal 窄化（t111，对齐解释器 coerce_to_declared）：
    // integer 需整数态——小数态运行期陷阱；decimal 双态皆合法，整数态
    // 加宽小数表示（to_double 的 Num 分支 tag select 免分支）
    if (want == CGType::Int && a.type == CGType::Num) {
        return num_to_int_checked(a.value);
    }
    if (want == CGType::Double && a.type == CGType::Num) {
        return to_double(a);
    }
    // integer/decimal → number 加宽（t62）：保持原表示打 tag；反向窄化
    // 已由上方 t111 分支覆盖
    if (want == CGType::Num &&
        (a.type == CGType::Int || a.type == CGType::Double)) {
        return to_num(a);
    }
    // bool → tribool 单向加宽（t65，与语义层一致）
    if (want == CGType::Tri && a.type == CGType::Bool) {
        return to_tri(a);
    }
    // 静态 tuple 实参传给动态 tuple 形参（t140）：物化为堆对象再传 ptr
    if (want == CGType::TupDyn && a.type == CGType::Tup) {
        return materialize_tuple_dynamic(a.tup, line, column);
    }
    // 动态承载值做 Tuple 形参实参（t180）：解释器绑定实参处同走 coerce_to_declared
    // 的 KW_TUPLE 分支（只要求 is_tuple()），tag 8 位守卫后 ptr 直传形参，
    // 其余位落运行期 "cannot assign <kind> to 'Tuple' variable" 同消息陷阱
    if (want == CGType::TupDyn && a.type == CGType::Dyn) {
        return dyn_to_tuple_slot(a.value);
    }
    // 隐式 number/bool → string（t131，对齐解释器 coerce_to_declared KW_STRING
    // 分支：number/bool 隐式转 string；其余类型拒编）
    if (want == CGType::Str &&
        (a.type == CGType::Int || a.type == CGType::Double ||
         a.type == CGType::Num || a.type == CGType::Bool)) {
        return coerce_to_string_value(a);
    }
    // 动态承载值做实参（t147）：形参为六个标量类型时按解释器 coerce_to_declared
    // （绑定实参处同函数）的接受表插运行期守卫；需静态信息的形参类型维持拒编
    if (a.type == CGType::Dyn && is_dyn_unbox_target(want)) {
        return dyn_unbox_to(a.value, want, line, column);
    }
    if (a.type == CGType::Dyn && want == CGType::Arr) {
        // 动态承载值做 array 形参实参（t165）：解释器绑定实参处走 KW_ARRAY 校验，
        // 只要求 is_array()；形参槽 elem 恒 Num 哨兵（t70），守卫后 ptr 直传
        return dyn_to_arr_slot(a.value);
    }
    if (a.type == CGType::Dyn && want == CGType::Obj && !want_cls.empty()) {
        // 动态承载值进类名形参（t158）：同声明槽规则——tag 5 且类 id 属形参声明
        // 类或其后代类才可静态解码，守卫不中陷阱退出
        return dyn_unbox_to_obj(a.value, want_cls, line, column);
    }
    if (want == CGType::Dyn && is_dyn_carrier(a.type)) {
        // 任意可承载实参进 object 形参（t161，S108）：解释器对 object 签名
        // default 直存不校验，codegen 统一打 {tag,bits}（实例 tag 5、数组 tag 4、
        // 标量按 arr_kind_of）；Tup/Fn 等非承载类型仍落下方拒编
        return to_dyn(a);
    }
    unsupported("argument type mismatch", line, column);
}

bool CodeGenerator::signatures_consistent(const std::vector<const CGClass*>& defs,
                                          const std::string& name,
                                          const CGMethod& base) {
    // 签名全等判定（t86 防御的谓词化，t183）：返回类型（含类名）与形参类型/
    // 类名逐项相等才算一致——byte/word 形参与返回在签名登记处即拒编（t98/t99），
    // 故无位类型上限差异需比较；一致时调用侧按基线签名一次对齐即可
    for (const CGClass* sub : defs) {
        const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
        if (m.ret_type != base.ret_type || m.ret_cls != base.ret_cls ||
            m.param_types != base.param_types || m.param_cls != base.param_cls) {
            return false;
        }
    }
    return true;
}

void CodeGenerator::emit_mismatched_dispatch(
    const std::vector<const CGClass*>& defs, const std::string& name,
    llvm::Value* receiver, const std::vector<CGValue>& arguments,
    llvm::Value* clsid, llvm::Value* dyn_tag, llvm::Value* dyn_bits, size_t line,
    size_t column) {
    // 跨类同名异签名方法的动态分派（t183）：原三类落点（object/Dyn 接收者、
    // 静态 cls 覆写、静态 cls 继承）一律拒编，因为统一 PHI 无法承载不同签名。
    // 解释器按动态命中方法各自校验实参与返回值，所以正确形态是逐臂拆槽：
    // 实参只求值一次并打框（与解释器同序，且 tuple/数组等需堆物化的类型不重复
    // 物化），臂内按该臂形参类型经 coerce_call_arg 的 Dyn 源分支运行期还原
    for (const CGClass* sub : defs) {
        const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
        if (m.param_types.size() != arguments.size()) {
            // 各臂元数不同 → 命中哪个臂运行期才定，实参无从静态对齐，拒编不错编
            //（解释器对元数不符同为运行期报错）
            unsupported("method arity mismatch for '" + name + "'", line, column);
        }
    }
    std::vector<llvm::Value*> boxed;
    boxed.reserve(arguments.size());
    for (const CGValue& a : arguments) {
        boxed.push_back(to_dyn(a, line, column));
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dispatch.trap", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, "dispatch.end", fn);
    llvm::SwitchInst* sw = builder_.CreateSwitch(
        clsid, trap_bb, static_cast<unsigned>(defs.size()));
    bool all_void = true;
    std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
    for (const CGClass* sub : defs) {
        const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
        auto* bb = llvm::BasicBlock::Create(
            context_, "dispatch." + std::string(sub->stmt->name().lexeme()), fn);
        sw->addCase(builder_.getInt64(sub->id), bb);
        builder_.SetInsertPoint(bb);
        std::vector<llvm::Value*> args{receiver};
        for (size_t i = 0; i < arguments.size(); ++i) {
            args.push_back(coerce_call_arg({boxed[i], CGType::Dyn},
                                           m.param_types[i], m.param_cls[i],
                                           line, column));
        }
        llvm::Value* call = builder_.CreateCall(m.fn, args);
        // 返回值按该臂声明类型打框：Void → tag 10 空位（t175，对齐解释器
        // 无显式 return 即 Value::none）；Arr 位带内层数组 ptr、Obj 位带实例 ptr
        if (m.ret_type != CGType::Void) {
            all_void = false;
        }
        incoming.emplace_back(
            builder_.GetInsertBlock(),
            to_dyn({call, m.ret_type,
                    m.ret_type == CGType::Arr ? CGType::Num : CGType::Int,
                    m.ret_cls},
                   line, column));
        builder_.CreateBr(merge_bb);
    }
    builder_.SetInsertPoint(trap_bb);
    if (name == "toString" && arguments.empty()) {
        // 动态类未定义 toString → 内建兜底（对齐解释器 find_method 优先顺序）：
        // Dyn 接收者拆分路径（t159）的兜底位按运行期 tag 转自身表示串，其余取
        // 常量 "<object>"（静态 cls 接收者的无该方法祖先/旁支动态类）
        llvm::Value* fallback =
            dyn_tag ? builder_.CreateCall(rt_num_to_str_, {dyn_tag, dyn_bits},
                                          "dynstr")
                    : static_cast<llvm::Value*>(
                          builder_.CreateGlobalString("<object>"));
        incoming.emplace_back(trap_bb,
                              to_dyn({fallback, CGType::Str}, line, column));
        builder_.CreateBr(merge_bb);
    } else {
        builder_.CreateCall(rt_trap_undefined_method_,
                            {builder_.CreateGlobalString(name)});
        builder_.CreateUnreachable();
    }
    builder_.SetInsertPoint(merge_bb);
    if (all_void) {
        // 全臂返回 none：结果仍记 Void（无需占位 tag，与既有 Void 消费面一致）
        last_value_ = {nullptr, CGType::Void};
        return;
    }
    // 合流为动态承载值：{i64,i64} 第一类 struct PHI，下游全走既有 tag 分派消费
    llvm::PHINode* phi = builder_.CreatePHI(
        llvm_type_of(CGType::Dyn), static_cast<unsigned>(incoming.size()),
        "dispatchdyn");
    for (const auto& in : incoming) {
        phi->addIncoming(in.second, in.first);
    }
    last_value_ = {phi, CGType::Dyn};
}

std::string CodeGenerator::find_defining_class(const std::string& start,
                                               const std::string& mname) {
    // 自 start 起沿父链静态查首个定义 mname 的类（对齐解释器 find_method
    // 自子向父顺序）；未找到返回空串由调用方拒编
    for (std::string cname = start; !cname.empty();) {
        const CGClass& c = classes_.at(cname);
        for (const auto& member : c.stmt->members()) {
            if (const auto* fn = dynamic_cast<const FunctionStmt*>(member.get())) {
                if (fn->name().lexeme() == mname) {
                    return cname;
                }
            }
        }
        cname = c.super;
    }
    return {};
}

bool CodeGenerator::is_subclass_of(const std::string& sub,
                                   const std::string& ancestor) {
    // 真后代判定（t86，upcast 放行用）：自 sub 的父类起沿 super 链向上比对
    auto it = classes_.find(sub);
    if (it == classes_.end()) return false;
    for (std::string cname = it->second.super; !cname.empty();
         cname = classes_.at(cname).super) {
        if (cname == ancestor) return true;
    }
    return false;
}

std::string CodeGenerator::nearest_common_ancestor(const std::string& a,
                                                   const std::string& b) {
    // 最近公共祖先（t93，分支实例合流用）：含自身端点——a 即 b 的祖先
    // 返 a、反之返 b；否则自 a 的父类起沿 super 链向上找首个同为 b 祖先
    // 的类；无公共祖先返回空串由调用方拒编
    if (a == b || is_subclass_of(b, a)) return a;
    if (is_subclass_of(a, b)) return b;
    auto it = classes_.find(a);
    if (it == classes_.end()) return {};
    for (std::string cname = it->second.super; !cname.empty();
         cname = classes_.at(cname).super) {
        if (is_subclass_of(b, cname)) return cname;
    }
    return {};
}

llvm::Type* CodeGenerator::llvm_type_of(CGType type) {
    switch (type) {
        case CGType::Int:    return builder_.getInt64Ty();
        case CGType::Double: return builder_.getDoubleTy();
        case CGType::Num:
            // number tagged 双表示（t62）：{i64 tag, i64 bits} first-class struct，
            // SSA 单值流转；仅 collie_rt 边界拆散标量（同模块内降级一致安全）
            return llvm::StructType::get(builder_.getInt64Ty(), builder_.getInt64Ty());
        case CGType::Dyn:
            // 动态标量承载（t138）：与 Num 同构 {i64 tag, i64 bits}，tag 复用数组
            // kind 编码；仅 print/toString/插值消费，其余落点拒编
            return llvm::StructType::get(builder_.getInt64Ty(), builder_.getInt64Ty());
        case CGType::Bool:   return builder_.getInt1Ty();
        case CGType::Tri:    return builder_.getInt8Ty(); // 三态编码（t65）
        case CGType::Str:    return llvm::PointerType::getUnqual(context_);
        case CGType::Arr:    return llvm::PointerType::getUnqual(context_);
        case CGType::Obj:    return llvm::PointerType::getUnqual(context_);
        case CGType::TupDyn: return llvm::PointerType::getUnqual(context_);
        case CGType::Fn:     return llvm::PointerType::getUnqual(context_);
        case CGType::Tup:
            // tuple 虚值无 LLVM 承载（t68）：落到此处即进了未静态展开的
            // 位置（如类字段），拒编不错编
            unsupported("tuple value in this position", 0, 0);
        case CGType::Void:   return builder_.getVoidTy();
    }
    return builder_.getInt64Ty(); // 不可达，压编译器警告
}

llvm::AllocaInst* CodeGenerator::create_entry_alloca(llvm::Type* type,
                                                     const std::string& name) {
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(type, nullptr, name);
}

llvm::Value* CodeGenerator::create_var_slot(llvm::Type* type,
                                            const std::string& name) {
    if (!in_function_ && scopes_.size() == 1) {
        // 顶层变量升全局槽（t73）：零初始化 GlobalVariable（内部链接 +
        // collie.g. 前缀防符号冲突），初始值仍在 @main 当前位置按源序
        // store——语义层在函数声明处分析函数体（只见此前声明的顶层变量），
        // 且调用必在函数声明之后，零初始化值不可能先于 store 被读到
        return new llvm::GlobalVariable(
            *module_, type, /*isConstant=*/false,
            llvm::GlobalValue::InternalLinkage,
            llvm::Constant::getNullValue(type), "collie.g." + name);
    }
    return create_entry_alloca(type, name);
}

llvm::Value* CodeGenerator::coerce_for_slot(const CGValue& v, CGType slot_type,
                                            const Token& where,
                                            const std::string& slot_cls,
                                            bool allow_dyn_unbox,
                                            const char* dyn_target_name) {
    if (v.type == slot_type) {
        // Arr 字段槽任意元素透传（t88 解除 t71 数值系守卫：字段槽 elem 恒
        // Num 哨兵，kind 随数组对象自带——print/len/== rt 侧全 kind 覆盖，
        // 索引读按运行期 kind 产出 Dyn，t151；Arr 目标仅字段路径可达）
        if (slot_type == CGType::Obj && v.cls != slot_cls &&
            !is_subclass_of(v.cls, slot_cls) &&
            !is_subclass_of(slot_cls, v.cls)) {
            // 字段允许同一继承树内 upcast/downcast（t86/t103，访问点按头部
            // 类 id 动态分派/陷阱）；跨树拒编不错编（Obj 目标仅字段路径可达）
            unsupported("storing instance of class '" + v.cls +
                            "' where '" + slot_cls + "' is declared",
                        where.line(), where.column());
        }
        return v.value;
    }
    // 静态 tuple 存入动态 tuple 槽（t140）：解释器整值替换，编译产物按动态
    // 对象物化再存 ptr（换形状天然支持）；v.type==TupDyn 已在上方等值透传
    if (slot_type == CGType::TupDyn && v.type == CGType::Tup) {
        return materialize_tuple_dynamic(v.tup, where.line(), where.column());
    }
    // 仅 integer → decimal 隐式提升（与语义层/解释器 coerce_to_declared 一致）
    if (slot_type == CGType::Double && v.type == CGType::Int) {
        return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
    }
    // number → integer/decimal 窄化（t111，同 coerce_call_arg）：integer 需
    // 整数态——小数态运行期陷阱；decimal 双态皆合法（number→byte/word
    // 由语义分析器前端静态拒绝，不经此处）
    if (slot_type == CGType::Int && v.type == CGType::Num) {
        return num_to_int_checked(v.value);
    }
    if (slot_type == CGType::Double && v.type == CGType::Num) {
        return to_double(v);
    }
    // integer/decimal → number 加宽（t62）：保持原表示打 tag
    if (slot_type == CGType::Num &&
        (v.type == CGType::Int || v.type == CGType::Double)) {
        return to_num(v);
    }
    // bool → tribool 单向加宽（t65，与语义层/解释器 coerce_to_declared 一致）
    if (slot_type == CGType::Tri && v.type == CGType::Bool) {
        return to_tri(v);
    }
    // 隐式 number/bool → string（t131，对齐解释器 coerce_to_declared KW_STRING
    // 分支：number/bool 隐式转 string；其余类型拒编）
    if (slot_type == CGType::Str &&
        (v.type == CGType::Int || v.type == CGType::Double ||
         v.type == CGType::Num || v.type == CGType::Bool)) {
        return coerce_to_string_value(v);
    }
    // 任意可承载值进 object 动态承载槽（t162，S109）：解释器对 object 声明
    // 的字段槽 default 直存不校验，codegen 统一打 {tag,bits}（实例 tag 5、
    // 数组 tag 4、标量按 arr_kind_of），与 object 变量/动态签名同一表示；
    // v.type == Dyn 已在上方等值透传，Tup/Fn 等非承载类型落下方拒编
    if (slot_type == CGType::Dyn && is_dyn_carrier(v.type)) {
        return to_dyn(v);
    }
    // 动态承载值进类型化槽（t147）：kind 7 数组元素/动态域读出与 object
    // 动态承载槽（t146）的值 tag 运行期才定——按解释器 coerce_to_declared
    // 的接受表插运行期守卫拆槽（不符者陷阱退出，两端同报错）；Arr/Tup/
    // Fn 目标静态表示需元素 kind/形状，维持下方拒编不错编
    if (allow_dyn_unbox && v.type == CGType::Dyn && is_dyn_unbox_target(slot_type)) {
        return dyn_unbox_to(v.value, slot_type, where.line(), where.column(),
                            dyn_target_name);
    }
    if (allow_dyn_unbox && v.type == CGType::Dyn && slot_type == CGType::Obj &&
        !slot_cls.empty()) {
        // 动态承载值进类名槽（t158）：字段/元素等带静态 cls 的 Obj 落点同声明
        // 槽规则——tag 5 且类 id 属声明类或后代类才还原实例 ptr，守卫不中陷阱
        // 退出；cls 空（动态 object 落点）无静态布局，维持下方拒编
        return dyn_unbox_to_obj(v.value, slot_cls, where.line(), where.column());
    }
    if (allow_dyn_unbox && v.type == CGType::Dyn && slot_type == CGType::Arr) {
        // 动态承载值进 array 字段槽（t165）：解释器 coerce_to_declared 的 KW_ARRAY
        // 分支只要求 is_array()，kind 随数组对象自带——tag ∈ {4,7} 守卫后取 ptr，
        // 字段槽 elem 恒 Num 哨兵（t71/t88）故读写天然走动态域；其余位落
        // collie_rt_trap_dyn_convert("array")，两端同消息报错退出
        return dyn_to_arr_slot(v.value);
    }
    if (allow_dyn_unbox && v.type == CGType::Dyn &&
        slot_type == CGType::TupDyn) {
        // 动态承载值进 Tuple 落点（t179）：解释器 coerce_to_declared 的 KW_TUPLE
        // 分支只要求 is_tuple()，tag 8 位的 bits 即堆 tuple 对象 ptr——守卫后直接
        // 还原并入既有动态 tuple 消费面；其余位落运行期同消息陷阱（Tuple 字段与
        // 动态 tuple 形参/返回值同表示，整条链无需新转换）
        return dyn_to_tuple_slot(v.value);
    }
    unsupported("implicit conversion for variable '" + std::string(where.lexeme()) + "'",
                where.line(), where.column());
}

CodeGenerator::CGVar* CodeGenerator::lookup_var(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

std::vector<std::pair<size_t, std::string>> CodeGenerator::uninit_snapshot() {
    // 定赋区域跟踪（t110）：收集当前全部 uninit 变量——句柄记
    // (scopes_ 下标, 变量名)：区内 visitBlock 可能扩容 scopes_ 重分配
    // （MSVC unordered_map 移动非 noexcept 时元素被拷贝），裸 CGVar*
    // 会悬垂；快照变量均居外层作用域，区内只 push/pop 更深层，
    // 恢复点按句柄重新寻址恒命中
    std::vector<std::pair<size_t, std::string>> snap;
    for (size_t i = 0; i < scopes_.size(); ++i) {
        for (auto& entry : scopes_[i]) {
            if (entry.second.uninit) {
                snap.emplace_back(i, entry.first);
            }
        }
    }
    return snap;
}

void CodeGenerator::uninit_restore(
    const std::vector<std::pair<size_t, std::string>>& snap) {
    // 条件发射区退出：区内清除的 uninit 全部恢复（运行期可能未执行）
    for (const auto& h : snap) {
        auto found = scopes_[h.first].find(h.second);
        if (found != scopes_[h.first].end()) {
            found->second.uninit = true;
        }
    }
}

bool CodeGenerator::has_loop_escape(const Stmt* s) {
    // do-while 体逃逸扫描（t113）：本层 break/continue 会跳过体尾后续
    // 赋值/直接跳出，致体末清除集不再支配出口；下探 block/if
    // 但不下探嵌套循环/switch（其内 break/continue 绑定内层，
    // 不影响本 do-while 体的支配）
    if (s == nullptr) {
        return false;
    }
    if (dynamic_cast<const BreakStmt*>(s) ||
        dynamic_cast<const ContinueStmt*>(s)) {
        return true;
    }
    if (const auto* block = dynamic_cast<const BlockStmt*>(s)) {
        for (const auto& inner : block->statements()) {
            if (has_loop_escape(inner.get())) {
                return true;
            }
        }
        return false;
    }
    if (const auto* if_stmt = dynamic_cast<const IfStmt*>(s)) {
        return has_loop_escape(if_stmt->then_branch()) ||
               has_loop_escape(if_stmt->else_branch());
    }
    // 嵌套 while/for/do-while/switch 不下探：其内逃逸绑定自身，
    // 其余语句（表达式/声明/返回等）不含本层逃逸
    return false;
}

void CodeGenerator::declare_function(const FunctionStmt& stmt,
                                     const std::string& prefix) {
    const std::string name(stmt.name().lexeme());
    // 嵌套函数注册键改编为 prefix.name（t91）：用户标识符无 '.'，
    // 与顶层名/其他外层的同名嵌套天然不冲突
    const std::string key = prefix.empty() ? name : prefix + "." + name;
    if (functions_.count(key) != 0) {
        // 同名后定义遮蔽前定义（t131，对齐解释器 env_.define 覆盖语义）：
        // 删去前定义的死 llvm 函数（第一遍仅建原型、尚无体），胜出者重建
        functions_[key].fn->eraseFromParent();
    }
    // none 返回降级 void；其余返回/参数类型限 declared_signature_type 支持面
    // （含类实例 IDENTIFIER → Obj+cls，t61）
    CGType ret = CGType::Void;
    std::string ret_cls;
    long long ret_bit_max = 0;
    const TokenType rtt = stmt.return_type().type();
    if (rtt == TokenType::KW_BYTE || rtt == TokenType::KW_WORD) {
        // byte/word 返回类型（t97）：i64 承载，ret_bit_max 记范围上限——返回值
        // 在 visitReturn 插 check_bit_range，越界陷阱对齐解释器 coerce_to_declared
        // 返回值校验（byte/word 形参见下方参数循环 t98；类方法返回走
        // declared_signature_type 维持拒编，不静默丢范围校验）
        ret = CGType::Int;
        ret_bit_max = rtt == TokenType::KW_BYTE ? 255 : 65535;
    } else if (rtt != TokenType::KW_NONE) {
        declared_signature_type(stmt.return_type(), ret, ret_cls);
    }
    // Arr 返回/形参放行（t70）：签名处无元素类型标注，elem 动态化为 Num 哨兵
    //（运行时 kind 驱动，调用点/返回点静态守卫 kind ∈ {0,1}）
    std::vector<CGType> param_types;
    std::vector<std::string> param_cls;
    std::vector<llvm::Type*> llvm_params;
    for (const auto& param : stmt.parameters()) {
        CGType t = CGType::Void;
        std::string t_cls;
        const TokenType ptt = param.type.type();
        if (ptt == TokenType::KW_BYTE || ptt == TokenType::KW_WORD) {
            // byte/word 形参（t98）：i64 承载（绕过 declared_signature_type 的
            // "variable type" 拒编）；形参落槽时置 CGVar.bit_max，体内重赋走
            // 既有赋值点 check_bit_range 陷阱。实参恒为 byte/word 类型（重载
            // 解析拒整数字面量/算术表达式），已在其来源处校验范围，调用点
            // 无需补陷阱；类方法/构造器参数走 declared_signature_type 维持拒编
            t = CGType::Int;
        } else {
            declared_signature_type(param.type, t, t_cls);
        }
        param_types.push_back(t);
        param_cls.push_back(t_cls);
        llvm_params.push_back(llvm_type_of(t));
    }
    auto* fn_type = llvm::FunctionType::get(llvm_type_of(ret), llvm_params,
                                            /*isVarArg=*/false);
    // 符号名加 "collie." 前缀：用户标识符无 '.'，天然不与 main/printf 等 C 符号冲突
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                      "collie." + key, module_.get());
    functions_[key] = {fn, std::move(param_types), std::move(param_cls), ret,
                       std::move(ret_cls), ret_bit_max};
    // 同名遮蔽：后定义恒为胜出者（t131，对齐解释器 env_.define 覆盖）
    winning_stmt_[key] = &stmt;
    fn_stmt_to_key_[&stmt] = key;
    // 递归下探函数体登记嵌套函数原型（t91）：可见性到 visitFunction
    // 声明处才登记（对齐解释器"执行到声明处 env_.define"）
    for (const auto& body_stmt : stmt.body()->statements()) {
        declare_nested_in(body_stmt.get(), key);
    }
}

void CodeGenerator::declare_nested_in(const Stmt* s, const std::string& prefix) {
    if (s == nullptr) {
        return;
    }
    if (const auto* fn_stmt = dynamic_cast<const FunctionStmt*>(s)) {
        declare_function(*fn_stmt, prefix);
        nested_fns_[fn_stmt] =
            prefix + "." + std::string(fn_stmt->name().lexeme());
        return;
    }
    if (const auto* block = dynamic_cast<const BlockStmt*>(s)) {
        for (const auto& inner : block->statements()) {
            declare_nested_in(inner.get(), prefix);
        }
        return;
    }
    if (const auto* if_stmt = dynamic_cast<const IfStmt*>(s)) {
        declare_nested_in(if_stmt->then_branch(), prefix);
        declare_nested_in(if_stmt->else_branch(), prefix);
        return;
    }
    if (const auto* while_stmt = dynamic_cast<const WhileStmt*>(s)) {
        declare_nested_in(while_stmt->body(), prefix);
        return;
    }
    if (const auto* for_stmt = dynamic_cast<const ForStmt*>(s)) {
        declare_nested_in(for_stmt->body(), prefix);
        return;
    }
    if (const auto* do_stmt = dynamic_cast<const DoWhileStmt*>(s)) {
        declare_nested_in(do_stmt->body(), prefix);
        return;
    }
    if (const auto* switch_stmt = dynamic_cast<const SwitchStmt*>(s)) {
        for (const auto& sc : switch_stmt->cases()) {
            declare_nested_in(sc.body.get(), prefix);
        }
        return;
    }
    // 其余语句（表达式/变量声明/return 等）不含语句子树，无嵌套函数可登记
}

void CodeGenerator::build_closure_prototype(const FunctionStmt& stmt,
                                            const std::string& key) {
    // 收集体内自由标识符（含更深层嵌套体，供逐层按引用转发捕获）后按宿主
    // 当前 scope 解析：仅「真实存储槽且非顶层全局」的形参/局部才是捕获候选。
    // 顶层全局经函数体链底槽拷贝直读（t73），无需 ptr；函数绑定/元组解构槽
    // （slot 恒 nullptr）/未分配 id 者一律跳过——体内引用它们维持既有可见性。
    // 捕获为空则保留第一遍无捕获原型，走原路径（现有嵌套函数零改动）。
    auto& info = functions_[key];
    std::set<std::string> free_names;
    collect_free_idents_stmt(stmt.body(), free_names);

    std::vector<CGFunction::Capture> captures;
    for (const auto& name : free_names) {
        CGVar* cv = lookup_var(name);
        if (cv == nullptr) continue;                        // 非外层可见（内建/类名/未定义）
        if (!cv->fn_key.empty()) continue;                  // 函数名绑定，非变量槽
        if (cv->slot == nullptr) continue;                  // 元组解构无单一槽（保守不捕获）
        if (cv->cell_id == 0) continue;                     // 未分配单元 id
        if (llvm::isa<llvm::GlobalVariable>(cv->slot)) continue; // 顶层全局：链底直读
        CGFunction::Capture cap;
        cap.name = name;
        cap.cell_id = cv->cell_id;
        cap.type = cv->type;
        cap.elem = cv->elem;
        cap.cls = cv->cls;
        cap.bit_max = cv->bit_max;
        cap.uninit = cv->uninit;
        captures.push_back(std::move(cap));
    }
    if (captures.empty()) return;

    // 重建原型：原声明参数 LLVM 类型 + 等长尾随 ptr 捕获参数（宿主单元地址）。
    // 此刻第一遍原型无任何调用引用（嵌套体的调用恒在声明语句之后生成），可安全
    // eraseFromParent 换建；体内引用与更深层转发经 visitFunction 的捕获绑定 +
    // visitCall 的 cell_id 一致性传参闭合。
    std::vector<llvm::Type*> llvm_params;
    llvm_params.reserve(info.param_types.size() + captures.size());
    for (CGType t : info.param_types) llvm_params.push_back(llvm_type_of(t));
    for (size_t j = 0; j < captures.size(); ++j)
        llvm_params.push_back(llvm::PointerType::getUnqual(context_));
    auto* fn_type = llvm::FunctionType::get(llvm_type_of(info.ret_type),
                                            llvm_params, /*isVarArg=*/false);
    info.fn->eraseFromParent();
    info.fn = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                     "collie." + key, module_.get());
    info.captures = std::move(captures);
}

void CodeGenerator::collect_classes_in_order(
    const std::vector<std::unique_ptr<Stmt>>& stmts,
    std::vector<const ClassStmt*>& out,
    std::unordered_map<std::string, const Stmt*>& deferred_classes,
    const Stmt* owner) {
    for (const auto& s : stmts) {
        if (const auto* c = dynamic_cast<const ClassStmt*>(s.get())) {
            out.push_back(c);
            if (owner != nullptr) {
                // 非顶层位置（裸块/函数/控制流块内）：记入延迟就绪表
                deferred_classes[std::string(c->name().lexeme())] = owner;
            }
            // 类方法/构造器体内类声明（t129）：方法体同属可重复进入的函数
            // 语境，递归收集其体内直线/裸块/控制流类声明，宿主记为该方法
            for (const auto& member : c->members()) {
                if (const auto* m = dynamic_cast<const FunctionStmt*>(member.get())) {
                    collect_classes_in_order(m->body()->statements(), out,
                                             deferred_classes, m);
                }
            }
        } else if (const auto* b = dynamic_cast<const BlockStmt*>(s.get())) {
            // 裸块恒执行（t127）：递归下钻（宿主沿用外层 owner）
            collect_classes_in_order(b->statements(), out, deferred_classes, owner);
        } else if (const auto* fn = dynamic_cast<const FunctionStmt*>(s.get())) {
            // 函数体（t128）：递归下钻体内直线与裸块，宿主函数记为 fn
            collect_classes_in_order(fn->body()->statements(), out, deferred_classes, fn);
        } else if (const auto* ifs = dynamic_cast<const IfStmt*>(s.get())) {
            // if 体（t130）：then/else 分支为条件执行块，宿主记为各自 BlockStmt；
            // 仅限"声明与 new 同块"由 scope_stack_ 守卫（编译期无条件注册会让
            // codegen 比解释器宽松：解释器条件为假不登记，属错编方向）
            if (const auto* tb = dynamic_cast<const BlockStmt*>(ifs->then_branch())) {
                collect_classes_in_order(tb->statements(), out, deferred_classes, tb);
            }
            if (const auto* eb = dynamic_cast<const BlockStmt*>(ifs->else_branch())) {
                collect_classes_in_order(eb->statements(), out, deferred_classes, eb);
            }
        } else if (const auto* ws = dynamic_cast<const WhileStmt*>(s.get())) {
            if (const auto* bb = dynamic_cast<const BlockStmt*>(ws->body())) {
                collect_classes_in_order(bb->statements(), out, deferred_classes, bb);
            }
        } else if (const auto* fs = dynamic_cast<const ForStmt*>(s.get())) {
            if (const auto* bb = dynamic_cast<const BlockStmt*>(fs->body())) {
                collect_classes_in_order(bb->statements(), out, deferred_classes, bb);
            }
        } else if (const auto* dws = dynamic_cast<const DoWhileStmt*>(s.get())) {
            if (const auto* bb = dynamic_cast<const BlockStmt*>(dws->body())) {
                collect_classes_in_order(bb->statements(), out, deferred_classes, bb);
            }
        } else if (const auto* ss = dynamic_cast<const SwitchStmt*>(s.get())) {
            for (const auto& sc : ss->cases()) {
                if (const auto* cb = dynamic_cast<const BlockStmt*>(sc.body.get())) {
                    collect_classes_in_order(cb->statements(), out, deferred_classes, cb);
                }
            }
        }
    }
}

void CodeGenerator::register_class_layout(const ClassStmt& stmt) {
    const std::string name(stmt.name().lexeme());
    if (classes_.count(name) != 0) {
        unsupported("duplicate class '" + name + "'",
                    stmt.name().line(), stmt.name().column());
    }
    CGClass cls;
    cls.stmt = &stmt;
    // 类 id（t86）：注册序分配，存对象头部（struct 元素 0），upcast 后
    // 方法调用点按 id switch 到动态类的单态化实例
    cls.id = classes_.size();

    // 继承布局（t61）：直接复用父类已合并好的字段列表作前缀（base-first，
    // 父类字段的 GEP 索引在子类 struct 中不变，父类方法副本可直接复用）
    if (stmt.has_superclass()) {
        cls.super = std::string(stmt.superclass().lexeme());
        auto sit = classes_.find(cls.super);
        if (sit == classes_.end()) {
            // 合并依赖父类已注册——要求父类声明在前（拒编不错编）
            unsupported("superclass '" + cls.super +
                            "' not declared before class '" + name + "'",
                        stmt.name().line(), stmt.name().column());
        }
        cls.fields = sit->second.fields;
        cls.field_index = sit->second.field_index;
    }

    // 自身字段追加（下标即 GEP 索引）；类型限 declared_cgtype 支持面
    for (const auto& member : stmt.members()) {
        const auto* field = dynamic_cast<const VarDeclStmt*>(member.get());
        if (!field) continue;
        const std::string fname(field->name().lexeme());
        bool field_uninit = false;
        if (!field->initializer()) {
            // 无初值字段（t109）：解释器绑 none——实例化点（visitNew）守卫
            // 构造器前缀定赋后 none 态不可观察，槽零值占位；守卫不满足
            // 处拒编不错编（none 无静态表示）
            field_uninit = true;
        }
        CGType ftype;
        std::string fcls;
        long long fbit_max = 0;
        // 承载映射单点见 field_carrier_of（S131 抽出：类名/object/Tuple/byte·word
        // 的分派与写侧校验共用同一份，避免遮蔽槽物理类型与声明承载分叉后两处漂移）
        field_carrier_of(*field, ftype, fcls, fbit_max);
        // array 字段放行（t71）：字段槽即 opaque ptr；无处标注元素类型，
        // 读出即动态域（visitProperty 置 Num 哨兵，t70 机制复用），
        // 写入守卫在 coerce_for_slot（elem 限数值系，保 kind ∈ {0,1}）
        // number 字段放行（t74）：StructType 按 llvm_type_of 逐字段拼装，
        // {i64,i64} 自动占位；malloc 上界在 visitNew 按字段类型累计（Num 16）
        // 遮蔽（子类重名声明继承字段，t182）：解释器实例字段是 name→value 的
        // 扁平表，父链 base-first 写入后被子类同名键覆写（父类初值仍求值，
        // 副作用可见），读写一律按动态类的声明转换——故本声明并入父链已有槽：
        // 下标与 field_index 不变（父类 struct 与既有 GEP 一字不动），被覆写声明
        // 按序记入 shadowed、本声明成为该槽最终声明（与解释器 find_field 的
        // 「子类优先沿链」一致）。承载一致时槽类型自此按遮蔽类声明走（t182）；
        // 承载不一致时登记冲突，由 resolve_shadow_conflicts 把本树全部持有者的
        // 槽统一升级为 {tag,bits}（S131），写侧校验仍按各类自身声明现算
        auto shadow = cls.field_index.find(fname);
        if (shadow != cls.field_index.end()) {
            CGField& prev = cls.fields[shadow->second];
            if (prev.type != ftype || prev.cls != fcls || prev.bit_max != fbit_max) {
                shadow_conflicts_.insert(
                    (cls.super.empty() ? name : class_root_of(cls.super)) + "." +
                    fname);
            }
            if (!prev.uninit) prev.shadowed.push_back(prev.decl);
            prev.decl = field;
            prev.uninit = field_uninit;
            continue;
        }
        cls.field_index[fname] = static_cast<unsigned>(cls.fields.size());
        cls.fields.push_back({fname, ftype, field, fcls, fbit_max, field_uninit});
    }
    classes_[name] = std::move(cls);
}

void CodeGenerator::field_carrier_of(const VarDeclStmt& field, CGType& type_out,
                                     std::string& cls_out, long long& bit_max_out) {
    // 字段声明 → 承载（t87/t162/t179 映射，S131 从 register_class_layout 抽出）
    cls_out.clear();
    bit_max_out = 0;
    const Token& tt = field.type();
    if (tt.type() == TokenType::IDENTIFIER) {
        // 类实例字段（t72）：IDENTIFIER 视为类名，须已注册（声明在前，同父类/
        // 签名要求）；自引用字段本类尚未入表自然落此拒编——解释器侧自引用 +
        // 字段必有初始值 = 无限递归 new，拒编不错编
        cls_out = std::string(tt.lexeme());
        if (classes_.count(cls_out) == 0) {
            unsupported("field type '" + cls_out + "'",
                        field.name().line(), field.name().column());
        }
        type_out = CGType::Obj;
        return;
    }
    if (tt.type() == TokenType::KW_OBJECT) {
        // object 型类字段（t162，S109）：解释器对 object 声明走 coerce_to_declared
        // default 直存任意值，字段槽运行期可带标量、数组或实例——与 object 变量
        // （t146/t160）及动态签名（t161）统一以 {tag,bits} 承载（CGType::Dyn，
        // 无静态 cls）；读出即 Dyn，消费走 S92–S130 全部既有 tag 分派；实例位经
        // dyn_obj_guard 并回 t133/t142 方法/字段动态分派。Tup/Fn 无编码，落
        // coerce_for_slot 拒编不错编
        type_out = CGType::Dyn;
        return;
    }
    if (tt.type() == TokenType::KW_TUPLE) {
        // Tuple 型类字段（t179，S126）：tuple 是静态展开虚值（CGType::Tup，无单一
        // LLVM 承载，见 llvm_type_of），字段槽无从落位——与 t140 的 tuple 进函数
        // 签名同解：以动态 tuple 堆对象承载（CGType::TupDyn，单 ptr 槽，malloc
        // 上界按 ≤ 8 计入），初值/赋值经 coerce_for_slot 物化，读出即 TupDyn 落
        // t140/t163/t170/t177 全部既有消费面（打印/转串/`.length`/位置与命名索引/
        // get(键)/判等/`==?`·`switch` 深比较/进 object 槽与 kind 7 元素槽）；
        // tuple 运行期不可变，堆对象多槽共享无别名风险
        type_out = CGType::TupDyn;
        return;
    }
    if (tt.type() == TokenType::KW_BYTE || tt.type() == TokenType::KW_WORD) {
        // byte/word 字段（t87）：i64 槽承载 + 记 bit_max，初始化/赋值两触点插
        // check_bit_range 陷阱（对齐解释器 coerce_to_declared 赋值点校验）；
        // 读出恒 Int，表达式域无截断（同 t69 变量语义）
        type_out = CGType::Int;
        bit_max_out = tt.type() == TokenType::KW_BYTE ? 255 : 65535;
        return;
    }
    type_out = declared_cgtype(tt);
}

CodeGenerator::CGValue CodeGenerator::field_slot_store(const CGField& field,
                                                       const VarDeclStmt& decl,
                                                       const CGValue& v,
                                                       const Token& where) {
    // 字段写的校验承载（S131）：解释器 coerce_to_declared 用的是 find_field 沿链
    // 命中的**当前生效声明**的类型（实例化点则是逐层各自的声明），遮蔽冲突槽把物理
    // 表示统一换成 {tag,bits} 后，校验仍须按声明走——否则子类把父类 string 字段
    // 改成 integer 时，写入值会跳过解释器会做的那次转换/范围校验而静默错值
    CGType ctype;
    std::string ccls;
    long long cbit = 0;
    field_carrier_of(decl, ctype, ccls, cbit);
    llvm::Value* conv = coerce_for_slot(v, ctype, where, ccls, true,
                                        bit_type_name(cbit));
    if (cbit > 0) {
        // byte/word 字段赋值点范围陷阱（t87，对齐解释器 coerce_to_declared）
        conv = check_bit_range(conv, cbit, cbit == 255 ? "byte" : "word");
    }
    CGValue stored{conv, ctype, ctype == CGType::Arr ? CGType::Num : CGType::Int,
                   ccls};
    if (!field.dyn_slot) {
        return stored;
    }
    // 遮蔽冲突槽：物理 {tag,bits}，把按本类声明转换后的值打框（object 声明的
    // 承载本身即 Dyn，上方 coerce_for_slot 已打框/透传，to_dyn 对 Dyn 恒等）
    return {to_dyn(stored, where.line(), where.column()), CGType::Dyn,
            CGType::Int, std::string()};
}

bool CodeGenerator::field_carrier_equal(const CGField& a, const CGField& b) {
    // 承载一致即「同一槽可同时满足两侧静态解码」（t182 合并判据，S131 改用
    // 声明现算：遮蔽后 CGField 的 type/cls/bit_max 是物理承载，冲突槽恒 Dyn，
    // 拿它比对会让 byte×word 这类上限分歧看着一致而漏判）
    if (a.dyn_slot != b.dyn_slot) {
        return false;
    }
    CGType ta;
    std::string ca;
    long long ba = 0;
    CGType tb;
    std::string cb;
    long long bb = 0;
    field_carrier_of(*a.decl, ta, ca, ba);
    field_carrier_of(*b.decl, tb, cb, bb);
    return ta == tb && ca == cb && ba == bb;
}

std::string CodeGenerator::class_root_of(const std::string& name) const {
    // 沿 super 链上溯到根（要求 name 已注册：注册进行中的类传其父类名）
    std::string cur = name;
    while (true) {
        const CGClass& cls = classes_.at(cur);
        if (cls.super.empty()) return cur;
        cur = cls.super;
    }
}

void CodeGenerator::resolve_shadow_conflicts() {
    // 遮蔽冲突槽动态承载化（S131）：解释器一个实例只有一份 name→value 扁平表，
    // 子类同名声明是**覆写同一个键**而非追加字段——故冲突槽必须在整棵继承树内
    // 统一换表示（父子 struct 前缀偏移要继续保持对齐），不能只在子类侧加槽。
    // 物理槽换 Dyn 后：读出即落 S92–S130 全部 tag 分派臂，写侧校验按各类自身
    // 生效声明现算（field_carrier_of），与解释器 find_field 子类优先沿链同源
    if (shadow_conflicts_.empty()) return;
    for (auto& entry : classes_) {
        const std::string root = class_root_of(entry.first);
        for (CGField& field : entry.second.fields) {
            if (shadow_conflicts_.count(root + "." + field.name) == 0) continue;
            field.dyn_slot = true;
            field.type = CGType::Dyn;
            field.cls.clear();
            field.bit_max = 0;
        }
    }
}

void CodeGenerator::finalize_class_layout(const ClassStmt& stmt) {
    // struct 建体（S131 起延后到遮蔽冲突解析之后）：元素 0 恒 i64 类 id 头部
    // （t86），字段 GEP 下标 = 逻辑下标 + 1
    const std::string name(stmt.name().lexeme());
    CGClass& cls = classes_.at(name);
    std::vector<llvm::Type*> field_types;
    field_types.push_back(builder_.getInt64Ty()); // 类 id 头部
    for (const CGField& field : cls.fields) {
        field_types.push_back(llvm_type_of(field.type));
    }
    cls.type = llvm::StructType::create(context_, field_types,
                                        "collie.class." + name);
}

void CodeGenerator::register_class_methods(const ClassStmt& stmt) {
    const std::string name(stmt.name().lexeme());
    CGClass& cls = classes_.at(name);

    // 继承父类全部单态化实例的本类副本（t61）：签名同、函数体独立生成——
    // 副本体内 this.m() 按本类分派表解析，模板方法模式（父类方法内调子类
    // 覆写）与解释器按实例实际类的动态分派等价（向上转型拒编所保）
    if (!cls.super.empty()) {
        const CGClass& super = classes_.at(cls.super);
        for (const auto& entry : super.instances) {
            CGMethod copy = entry.second;
            copy.fn = llvm::Function::Create(
                entry.second.fn->getFunctionType(),
                llvm::Function::InternalLinkage,
                "collie." + name + "." + entry.first, module_.get());
            cls.instances[entry.first] = std::move(copy);
        }
        cls.dispatch = super.dispatch;
    }

    // 自身方法（含构造器，键含类名前缀）：this 作隐藏首参 ptr，符号名
    // collie.<分派类>.<定义类>.<方法名>；同名于父链方法即覆写（dispatch 改指）
    for (const auto& member : stmt.members()) {
        const auto* method = dynamic_cast<const FunctionStmt*>(member.get());
        if (!method) continue;
        const std::string mname(method->name().lexeme());
        const std::string key = name + "." + mname;
        if (cls.instances.count(key) != 0) {
            // 本类内同名重复（方法/构造器重载，t123）：解释器 find_method 沿声明序
            // 取首个同名者胜、后续重载从不被调用（call_class_method 仅对首个做元数
            // 检查），故 codegen 首个登记后跳过其余同名成员（first-wins），与解释器
            // 逐字节对齐；调用非首个重载的元数不符面两端同为报错（rc=1），拒编不错编
            continue;
        }
        CGMethod info;
        info.defining = name;
        info.stmt = method;
        const TokenType mrt = method->return_type().type();
        if (mrt == TokenType::KW_BYTE || mrt == TokenType::KW_WORD) {
            // byte/word 方法返回（t99）：i64 承载 + ret_bit_max（t97 字段），
            // gen_method_body 设置 current_ret_bit_max_ 后 visitReturn 陷阱生效
            info.ret_type = CGType::Int;
            info.ret_bit_max = mrt == TokenType::KW_BYTE ? 255 : 65535;
        } else if (mrt != TokenType::KW_NONE) {
            declared_signature_type(method->return_type(), info.ret_type,
                                    info.ret_cls);
        }
        // Arr 返回/形参放行（t70，同 declare_function）
        std::vector<llvm::Type*> llvm_params;
        llvm_params.push_back(llvm::PointerType::getUnqual(context_)); // 隐藏 this
        for (const auto& param : method->parameters()) {
            CGType t = CGType::Void;
            std::string t_cls;
            const TokenType ptt = param.type.type();
            if (ptt == TokenType::KW_BYTE || ptt == TokenType::KW_WORD) {
                // byte/word 方法/构造器形参（t99）：i64 承载；方法为单签名
                // 按名解析（无顶层重载拦截），实参可为整数字面量，范围校验
                // 在 gen_method_body 绑定点插 check_bit_range
                t = CGType::Int;
            } else {
                declared_signature_type(param.type, t, t_cls);
            }
            info.param_types.push_back(t);
            info.param_cls.push_back(t_cls);
            llvm_params.push_back(llvm_type_of(t));
        }
        auto* fn_type = llvm::FunctionType::get(llvm_type_of(info.ret_type),
                                                llvm_params, /*isVarArg=*/false);
        info.fn = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                         "collie." + name + "." + key,
                                         module_.get());
        cls.dispatch[mname] = key;
        cls.instances[key] = std::move(info);
        // 方法体内嵌套函数原型登记（t104）：改编键 <定义类>.<方法名>.<名>
        // （同 t91 顶层嵌套规则，用户标识符无 '.' 天然不冲突；与顶层同名
        // 在语义层即拒绝）；继承副本共享同一 FunctionStmt，仅定义类注册
        // 一次，函数体由 visitFunction 首访生成、单态化副本重访跳过
        for (const auto& body_stmt : method->body()->statements()) {
            declare_nested_in(body_stmt.get(), key);
        }
    }
}

void CodeGenerator::gen_method_body(const CGClass& cls, const CGMethod& method) {
    const FunctionStmt& stmt = *method.stmt;
    const std::string name(stmt.name().lexeme());

    // 保存生成现场（同 visitFunction）：方法体用独立的变量环境/循环栈；
    // 顶层作用域拷贝为链底（t73；Tup 条目一并拷贝 t76，同 visitFunction）
    llvm::BasicBlock* saved_bb = builder_.GetInsertBlock();
    auto saved_scopes = std::move(scopes_);
    auto saved_loops = std::move(loops_);
    // 外层现场保存恢复（t128）：方法体可能在函数/方法体内声明的类处生成
    // （函数内类声明），收尾须还原外层语境而非复位顶层——同 visitFunction
    // 的 saved_in_function 机制（t91）
    const bool saved_in_function = in_function_;
    const FunctionStmt* saved_fn = current_fn_; // t128：宿主函数语境保存恢复
    llvm::Value* saved_this = current_this_;
    const std::string saved_class_name = current_class_name_;
    const std::string saved_defining_class = current_defining_class_;
    const CGType saved_ret_type = current_ret_type_;
    const std::string saved_ret_cls = current_ret_cls_;
    const long long saved_ret_bit_max = current_ret_bit_max_;
    ++fn_gen_count_; // 全局数组槽 elem 快照守卫（t106）
    scopes_.clear();
    scopes_.emplace_back();
    if (!saved_scopes.empty()) {
        scopes_.back() = saved_scopes.front();
    }
    scopes_.emplace_back(); // 参数层（可遮蔽全局）
    loops_.clear();
    in_function_ = true;
    current_fn_ = &stmt; // t129：方法体内声明的类以本方法为宿主
    ScopePushGuard scope_guard(scope_stack_, &stmt); // t130：进入方法体作用域
    current_ret_type_ = method.ret_type;
    current_ret_cls_ = method.ret_cls;
    current_ret_bit_max_ = method.ret_bit_max; // byte/word 方法返回（t99）
    // 分派上下文 = 分派类（this 的静态类）；base 解析上下文 = 定义类
    // （继承副本两者不同，与解释器 call_class_method 的 defining_class 同义，t61）
    current_class_name_ = std::string(cls.stmt->name().lexeme());
    current_defining_class_ = method.defining;

    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", method.fn));
    // 首参为隐藏 this（直接持 SSA 值不落栈槽：this 不可被赋值）；
    // 其余形参落栈槽（与 visitFunction 同机制，注意下标偏移 1）
    size_t i = 0;
    for (auto& arg : method.fn->args()) {
        if (i == 0) {
            arg.setName("this");
            current_this_ = &arg;
        } else {
            const Parameter& param = stmt.parameters()[i - 1];
            const std::string pname(param.name.lexeme());
            arg.setName(pname);
            llvm::AllocaInst* slot =
                create_entry_alloca(llvm_type_of(method.param_types[i - 1]), pname);
            llvm::Value* stored = &arg;
            // Arr 形参 elem 记 Num 哨兵（t70，同 visitFunction）
            CGVar binding{slot, method.param_types[i - 1],
                          method.param_types[i - 1] == CGType::Arr
                              ? CGType::Num
                              : CGType::Int,
                          method.param_cls[i - 1]};
            // byte/word 形参（t99）：绑定点插范围陷阱——方法/构造器实参可为
            // 整数字面量（单签名按名解析，无顶层重载拦截），对齐解释器绑定时
            // coerce_to_declared 校验；置 bit_max 使体内重赋走赋值点陷阱
            const TokenType ptt = param.type.type();
            if (ptt == TokenType::KW_BYTE || ptt == TokenType::KW_WORD) {
                const bool is_byte = ptt == TokenType::KW_BYTE;
                stored = check_bit_range(stored, is_byte ? 255 : 65535,
                                         is_byte ? "byte" : "word");
                binding.bit_max = is_byte ? 255 : 65535;
            }
            builder_.CreateStore(stored, slot);
            scopes_.back()[pname] = binding;
        }
        ++i;
    }

    for (const auto& body_stmt : stmt.body()->statements()) {
        body_stmt->accept(*this);
    }

    // 尾块收尾（同 visitFunction）：none 方法补 ret void（构造器返回类型即 none）；
    // 非 none 方法不可达尾块补 unreachable，可达无 return 拒编
    llvm::BasicBlock* tail = builder_.GetInsertBlock();
    if (!tail->getTerminator()) {
        if (method.ret_type == CGType::Void) {
            builder_.CreateRetVoid();
        } else if (!reachable_from_entry(tail)) {
            builder_.CreateUnreachable();
        } else {
            throw CodeGenError(
                "codegen: method '" + name + "' may reach end without return",
                stmt.name().line(), stmt.name().column());
        }
    }

    in_function_ = saved_in_function; // t128：还原外层语境（顶层调用时即 false）
    current_fn_ = saved_fn;           // t129：宿主函数语境一并还原
    current_ret_type_ = saved_ret_type;
    current_ret_cls_ = saved_ret_cls;
    current_ret_bit_max_ = saved_ret_bit_max;
    current_this_ = saved_this;
    current_class_name_ = saved_class_name;
    current_defining_class_ = saved_defining_class;
    scopes_ = std::move(saved_scopes);
    loops_ = std::move(saved_loops);
    builder_.SetInsertPoint(saved_bb);
}

void CodeGenerator::gen_ternary(const TernaryExpr& expr) {
    // 两分支：bool 条件直接分派，tribool 条件 unset 归 false 分支（对齐解释器
    // is_truthy 的三态归约，t65）；三分支 a ? x : y : z（t65）：条件限 tribool
    // （语义层已校验，此处防御），true/false/unset 三路分派（对齐解释器）
    CGValue cond = emit(expr.condition());
    const bool three_way = expr.unset_expr() != nullptr;
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();

    struct Arm {
        CGValue value;
        llvm::BasicBlock* end = nullptr; // 结果求值结束块（求值可能新建块）
        llvm::Value* aligned = nullptr;  // 对齐 result_type 后的值
    };
    std::vector<Arm> arms;
    // 条件发射区（t110）：三目臂运行期仅执行其一，臂内 uninit 清除
    // 逐臂隔离不外泄（条件已在区前发射，其定赋外泄合法）
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编
    auto eval_arm = [&](llvm::BasicBlock* bb, const Expr* e) {
        builder_.SetInsertPoint(bb);
        Arm arm;
        arm.value = emit(e);
        arm.end = builder_.GetInsertBlock();
        arms.push_back(std::move(arm));
        uninit_restore(uninit_snap);
    };

    auto* then_bb = llvm::BasicBlock::Create(context_, "tern.then", fn);
    auto* else_bb = llvm::BasicBlock::Create(context_, "tern.else", fn);
    if (three_way) {
        if (cond.type != CGType::Tri) {
            unsupported("non-tribool three-branch ternary condition",
                        expr.question_token().line(), expr.question_token().column());
        }
        auto* rest_bb = llvm::BasicBlock::Create(context_, "tern.rest", fn);
        auto* unset_bb = llvm::BasicBlock::Create(context_, "tern.unset", fn);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(cond.value, builder_.getInt8(2), "istrue"),
            then_bb, rest_bb);
        builder_.SetInsertPoint(rest_bb);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(cond.value, builder_.getInt8(0), "isfalse"),
            else_bb, unset_bb);
        eval_arm(then_bb, expr.then_expr());
        eval_arm(else_bb, expr.else_expr());
        eval_arm(unset_bb, expr.unset_expr());
    } else {
        llvm::Value* cond_i1 = nullptr;
        if (cond.type == CGType::Bool) {
            cond_i1 = cond.value;
        } else if (cond.type == CGType::Tri) {
            // tribool 条件的两分支形式：仅 true 走 then（unset 归 false 分支）
            cond_i1 = builder_.CreateICmpEQ(cond.value, builder_.getInt8(2), "istrue");
        } else {
            unsupported("non-bool ternary condition",
                        expr.question_token().line(), expr.question_token().column());
        }
        builder_.CreateCondBr(cond_i1, then_bb, else_bb);
        eval_arm(then_bb, expr.then_expr());
        eval_arm(else_bb, expr.else_expr());
    }

    // 分支类型统一（merge_static_arm_types）：同型直用（Arr elem / Obj cls 一致性
    // 校验）；数值混型任一 Num 统一 Num 否则提升 Double；tribool/bool 混型统一
    // tribool（单向加宽，t65）；合不上时按承载族回退 Dyn（见下）
    CGType result_type = arms.front().value.type;
    std::string result_cls = arms.front().value.cls;
    CGType result_elem = arms.front().value.elem;
    if (result_type == CGType::Void) {
        unsupported("ternary branch has no value",
                    expr.question_token().line(), expr.question_token().column());
    }
    {
        // tuple 族合流（t95 静态 + t140 动态）：全支同为 tuple 且递归同形时
        // 静态展开逐元素 PHI 合流（value=nullptr 虚值，形状编译期定）；否则
        // （形状不同、叶位类型不可合流或含动态 tuple）各支物化为动态堆对象 PHI 为
        // TupDyn——解释器三元/==? 择一返回整值 tuple，编译产物以运行期对象承载
        // 跨边界形状。嵌套 tuple 位与函数位在物化处拒编不错编
        const auto is_tup = [](CGType ty) {
            return ty == CGType::Tup || ty == CGType::TupDyn;
        };
        // tuple 位混入其它承载臂不再整面拒编（t170）：Tup 已入 is_dyn_carrier，
        // 全臂 tuple 仍走下方静态/动态 tuple 合流，混排则由「静态合流失败即按
        // 承载族回退 Dyn」接住（逐臂 to_dyn 打 tag 8）；含 Fn/Void 臂照旧拒编
        bool all_tup_family = true;
        for (const auto& arm : arms) {
            if (!is_tup(arm.value.type)) all_tup_family = false;
        }
        if (all_tup_family) {
            const size_t line = expr.question_token().line();
            const size_t column = expr.question_token().column();
            auto* tup_merge_bb = llvm::BasicBlock::Create(context_, "tern.end", fn);
            bool all_static = true;
            for (const auto& arm : arms) {
                if (arm.value.type != CGType::Tup) all_static = false;
            }
            if (all_static) {
                std::vector<int> tups;
                std::vector<llvm::BasicBlock*> arm_ends;
                for (const auto& arm : arms) {
                    tups.push_back(arm.value.tup);
                    arm_ends.push_back(arm.end);
                }
                if (tuple_arms_static_mergeable(tups)) {
                    const int idx =
                        merge_tuple_arms(tups, arm_ends, tup_merge_bb,
                                         "ternary branches", line, column);
                    last_value_ = {nullptr, CGType::Tup, CGType::Int, "", idx};
                    return;
                }
            }
            // 动态合流：逐支在其末块物化 ptr（静态支展开、动态支透传）后 Br，
            // merge 块 PHI 为 ptr（引用语义，各支独立堆对象无别名冲突）
            std::vector<llvm::Value*> dyn_vals;
            dyn_vals.reserve(arms.size());
            for (const auto& arm : arms) {
                builder_.SetInsertPoint(arm.end);
                llvm::Value* p = arm.value.type == CGType::TupDyn
                                     ? arm.value.value
                                     : materialize_tuple_dynamic(arm.value.tup,
                                                                 line, column);
                dyn_vals.push_back(p);
                builder_.CreateBr(tup_merge_bb);
            }
            builder_.SetInsertPoint(tup_merge_bb);
            llvm::PHINode* phi = builder_.CreatePHI(
                llvm_type_of(CGType::TupDyn), static_cast<unsigned>(arms.size()),
                "tupdynmerge");
            for (size_t k = 0; k < arms.size(); ++k) {
                phi->addIncoming(dyn_vals[k], arms[k].end);
            }
            last_value_ = {phi, CGType::TupDyn};
            return;
        }
    }
    // 静态合流失败但全部臂落 is_dyn_carrier 承载族（标量/实例位/数组位）时，
    // 逐臂 to_dyn 打框后 PHI 合流为 Dyn——解释器三元各支择一返回任意值，静态
    // 类型不可定（t166 覆盖含 Dyn 臂的混排，t168 把回退面推广到跨树两实例臂、
    // 实例位×标量臂、异型标量臂等静态合不上的一切承载组合）。合流值即 Dyn，
    // 消费全落 S92–S113 既有运行期 tag 分派臂，进 array/object 等静态落点即
    // t165/t158 既有守卫拆槽。
    // Tup/Fn/Void 臂已在上方 tuple 族分支与 Void 守卫处维持原拒编文案
    std::vector<CGValue> arm_values;
    arm_values.reserve(arms.size());
    for (const auto& arm : arms) {
        arm_values.push_back(arm.value);
    }
    const ArmMerge merge =
        merge_static_arm_types(arm_values, result_type, result_elem, result_cls);
    if (merge != ArmMerge::Ok) {
        if (!arms_all_dyn_carrier(arm_values)) {
            unsupported(merge == ArmMerge::NoCommonAncestor
                            ? "ternary branches yield instances of different classes"
                            : "ternary branches have incompatible types",
                        expr.question_token().line(),
                        expr.question_token().column());
        }
        result_type = CGType::Dyn;
        result_elem = CGType::Int;
        result_cls.clear();
    }

    // 各分支块尾把值对齐 result_type 后再跳 merge（提升指令须落在该分支块内）
    auto* merge_bb = llvm::BasicBlock::Create(context_, "tern.end", fn);
    for (auto& arm : arms) {
        builder_.SetInsertPoint(arm.end);
        arm.aligned = result_type == CGType::Double ? to_double(arm.value)
                    : result_type == CGType::Num    ? to_num(arm.value)
                    : result_type == CGType::Tri    ? to_tri(arm.value)
                    : result_type == CGType::Dyn    ? to_dyn(arm.value)
                                                    : arm.value.value;
        builder_.CreateBr(merge_bb);
    }
    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(
        llvm_type_of(result_type), static_cast<unsigned>(arms.size()), "terntmp");
    for (const auto& arm : arms) {
        phi->addIncoming(arm.aligned, arm.end);
    }
    // elem 仅 Arr 有意义——各支不同时统一 Num 动态域（t94）；cls 为各支最近公共祖先（t93）
    last_value_ = {phi, result_type, result_elem, result_cls};
}

llvm::Value* CodeGenerator::gen_match_eq(const CGValue& target, const CGValue& cand,
                                         const Token& op) {
    // Dyn 目标/候选（t169）：动态承载位的 kind 只有运行期可知——两侧统一 to_dyn
    // 盒化后下沉 rt_dyn_eq（与 t149 判等同源，复刻解释器 values_equal：Tri×Bool
    // 加宽三态、数值跨 tag 走 double 视图、串按内容、数组位深比较、tuple 位（tag
    // 8，t170）深比较、实例位与 kind 失配恒 false）；tuple 侧自 t170 起也入此分支
    // （Dyn 可承载 tuple 位，原「Tup × 非 Tup 恒 false」折叠只保留在两侧皆静态的
    // 各臂里）；函数位自 t171 起也入此分支（tag 9 与任何 tag 判等恒 false，与解释器
    // values_equal 的 Function default 一致）；空位自 t175 起也入此分支（tag 10，
    // 解释器 values_equal 的 None 分支恒 true，故 None×None 判等真、跨 kind 假）。
    // 静态×静态组合除函数位外不经此分支（函数位无静态判等路径，只能盒化比较）
    if (target.type == CGType::Dyn || cand.type == CGType::Dyn ||
        target.type == CGType::Fn || cand.type == CGType::Fn) {
        const auto dyn_match_side_ok = [](CGType ty) {
            return ty == CGType::Dyn || ty == CGType::Int || ty == CGType::Double ||
                   ty == CGType::Num || ty == CGType::Str || ty == CGType::Bool ||
                   ty == CGType::Tri || ty == CGType::Arr || ty == CGType::Obj ||
                   ty == CGType::Tup || ty == CGType::TupDyn || ty == CGType::Fn ||
                   ty == CGType::Void;
        };
        if (!dyn_match_side_ok(target.type) || !dyn_match_side_ok(cand.type)) {
            unsupported("'==?' comparison of these value types", op.line(), op.column());
        }
        llvm::Value* a = to_dyn(target);
        llvm::Value* b = to_dyn(cand);
        llvm::Value* c = builder_.CreateCall(
            rt_dyn_eq_, {num_tag(a), num_bits(a), num_tag(b), num_bits(b)}, "dynmatch");
        return builder_.CreateICmpNE(c, builder_.getInt32(0), "matcheq");
    }
    // 复用 visitBinary == 的四路降级（语义层已保证候选与目标可 == 比较）
    if (target.type == CGType::Str && cand.type == CGType::Str) {
        llvm::Value* c =
            builder_.CreateCall(rt_strcmp_, {target.value, cand.value}, "strcmptmp");
        return builder_.CreateICmpEQ(c, builder_.getInt32(0), "matcheq");
    }
    if (target.type == CGType::Tri || cand.type == CGType::Tri) {
        // tribool 三态判等（t65）：双方限 tribool/bool（bool 加宽三态后 icmp）；
        // 与其余 kind 恒不等（t177，对齐解释器 values_equal 的 tribool 首段——
        // 该段先于 kind 比较，任一侧 tribool 而另一侧非 bool/tribool 直接 false），
        // 故折成常量 false 而非拒编（switch 的候选字面量不经语义层可比性检查）
        if ((target.type != CGType::Tri && target.type != CGType::Bool) ||
            (cand.type != CGType::Tri && cand.type != CGType::Bool)) {
            return builder_.getInt1(false);
        }
        return builder_.CreateICmpEQ(to_tri(target), to_tri(cand), "matcheq");
    }
    if (target.type == CGType::Bool && cand.type == CGType::Bool) {
        return builder_.CreateICmpEQ(target.value, cand.value, "matcheq");
    }
    auto is_numeric = [](CGType t) {
        return t == CGType::Int || t == CGType::Double || t == CGType::Num;
    };
    if (is_numeric(target.type) && is_numeric(cand.type)) {
        if (target.type == CGType::Num || cand.type == CGType::Num) {
            // number 相等（t62）：collie_rt_num_cmp op 0（双整数精确、混合 double 视图）
            llvm::Value* a = to_num(target);
            llvm::Value* b = to_num(cand);
            llvm::Value* c = builder_.CreateCall(
                rt_num_cmp_,
                {builder_.getInt64(0), num_tag(a), num_bits(a), num_tag(b), num_bits(b)},
                "numcmp");
            return builder_.CreateICmpNE(c, builder_.getInt32(0), "matcheq");
        }
        if (target.type == CGType::Double || cand.type == CGType::Double) {
            // 混合表示按 double 视图相等（5 == 5.0，对齐解释器 values_equal）
            return builder_.CreateFCmpOEQ(to_double(target), to_double(cand), "matcheq");
        }
        return builder_.CreateICmpEQ(target.value, cand.value, "matcheq");
    }
    // tuple 目标/候选（t78 建，t177 补动态 tuple 位）：tuple 族（静态 Tup / t140
    // 动态 TupDyn）之间才深比较——双静态走 t75 静态展开（含 Arr 元素已于 t79
    // 下沉 rt_arr_eq、Dyn 位叶已于 t175 改走物化），任一侧为 TupDyn 时双方取/物化
    // 堆对象后交既有 rt_tuple_eq（与 visitBinary 的 tuple 族比较同源）；族 × 非族
    // 恒 false（对齐解释器 values_equal kind 不等）。原判定只看 CGType::Tup，
    // 动态 tuple 目标落到下方 `getInt1(false)` 是**静默不命中**（`switch (dyn)` 的
    // 同名静态候选恒判不等，解释器 rc=0 命中），t177 探针 t177_p17 抓到
    const auto is_tup = [](CGType ty) {
        return ty == CGType::Tup || ty == CGType::TupDyn;
    };
    if (is_tup(target.type) || is_tup(cand.type)) {
        if (!is_tup(target.type) || !is_tup(cand.type)) {
            return builder_.getInt1(false);
        }
        if (target.type == CGType::Tup && cand.type == CGType::Tup) {
            return gen_tuple_eq(target, cand, op);
        }
        llvm::Value* l =
            target.type == CGType::TupDyn
                ? target.value
                : materialize_tuple_dynamic(target.tup, op.line(), op.column());
        llvm::Value* r =
            cand.type == CGType::TupDyn
                ? cand.value
                : materialize_tuple_dynamic(cand.tup, op.line(), op.column());
        llvm::Value* c = builder_.CreateCall(rt_tuple_eq_, {l, r}, "tupmatch");
        return builder_.CreateICmpNE(c, builder_.getInt64(0), "matcheq");
    }
    // 数组目标/候选（t79）：双 Arr 下沉 rt_arr_eq 深比较；Arr × 非 Arr 恒
    // false（对齐解释器 values_equal kind 不等，语义层通常更早拦截，双保险）
    if (target.type == CGType::Arr || cand.type == CGType::Arr) {
        if (target.type == CGType::Arr && cand.type == CGType::Arr) {
            llvm::Value* c = builder_.CreateCall(
                rt_arr_eq_, {target.value, cand.value}, "arreqtmp");
            return builder_.CreateICmpNE(c, builder_.getInt64(0), "matcheq");
        }
        return builder_.getInt1(false);
    }
    // 实例（Obj）目标/候选（t82）：解释器 values_equal 无 Instance 分支落
    // default 恒 false（含同一实例），==?/switch 目标为实例时全部不命中；
    // 与 gen_tuple_eq Obj 元素恒 false 先例一致
    if (target.type == CGType::Obj || cand.type == CGType::Obj) {
        return builder_.getInt1(false);
    }
    // 静态 kind 互斥即恒不命中（t177）：解释器 values_equal 在 tribool 首段之后
    // 先比 kind、不等直接 false，而 switch 的候选字面量不经语义层可比性检查，
    // 故这类组合按 kind 折叠（探针 t177_p3~p8/p10~p12/p16/p18/p23 实证解释器
    // rc=0 恒走兜底支，none×none 反而恒命中）
    const auto static_kind = [](CGType ty) {
        switch (ty) {
            case CGType::Int:
            case CGType::Double:
            case CGType::Num: return 1; // integer/decimal/number 同属 Number kind
            case CGType::Str: return 2;
            case CGType::Bool: return 3;
            case CGType::Void: return 4; // none
            default: return 0; // Dyn/Tri/Arr/Obj/tuple 族/Fn 已由上方各臂接管
        }
    };
    const int lhs_kind = static_kind(target.type);
    const int rhs_kind = static_kind(cand.type);
    if (lhs_kind != 0 && rhs_kind != 0) {
        if (lhs_kind != rhs_kind) {
            return builder_.getInt1(false);
        }
        // 同 kind 却仍走到这里只剩空位×空位：values_equal 的 None 分支在 kind
        // 对齐后恒 true（与 t81 的 `nop() == nop()` 折叠同源，解释器
        // `switch (noise()) { noise() {...} }` 命中该支）
        return builder_.getInt1(target.type == CGType::Void &&
                                cand.type == CGType::Void);
    }
    // 现行 CGType 全集的两两组合已由上方各臂接管，此网只为新增承载类型未补
    // 比较臂时兜底（拒编不错编）
    unsupported("'==?' comparison of these value types", op.line(), op.column());
}

llvm::Value* CodeGenerator::gen_tuple_eq(const CGValue& lhs, const CGValue& rhs,
                                         const Token& op) {
    // 静态展开深比较（t75，对齐解释器 values_equal Tuple 分支）：
    // 元素数/名字表编译期全可知，形状不一致直接常量 false；
    // 按值拷贝不留引用（嵌套递归中 register_tuple 可能扩容 tuple_values_）
    const CGTuple lt = tuple_values_[lhs.tup];
    const CGTuple rt = tuple_values_[rhs.tup];
    if (lt.elems.size() != rt.elems.size() || lt.names != rt.names) {
        return builder_.getInt1(false);
    }
    // 含动态承载位的静态 tuple 深比较（t175）：空位（`none`）在 tuple 构造点即
    // 盒化为 tag 10 的 Dyn 元素，而静态展开对 Dyn 位没有合法降级——下方兜底分支
    // 的 is_numeric 判定会把 Dyn×Dyn 元素直接算成恒 false（解释器按元素值判等，
    // 属静默错编），故只要任一侧含 Dyn/Void 位就整值改走 t140 物化 + rt_tuple_eq
    // 按运行期 tag 逐元素 dyn_eq（与 t170 的 TupDyn 比较同源）
    const auto has_dyn_elem = [](const CGTuple& t) {
        for (const auto& e : t.elems) {
            if (e.type == CGType::Dyn || e.type == CGType::Void) {
                return true;
            }
        }
        return false;
    };
    if (has_dyn_elem(lt) || has_dyn_elem(rt)) {
        llvm::Value* l =
            materialize_tuple_dynamic(lhs.tup, op.line(), op.column());
        llvm::Value* r =
            materialize_tuple_dynamic(rhs.tup, op.line(), op.column());
        llvm::Value* c =
            builder_.CreateCall(rt_tuple_eq_, {l, r}, "tupeqdyndyn");
        return builder_.CreateICmpNE(c, builder_.getInt64(0), "tupeq");
    }
    llvm::Value* all = builder_.getInt1(true);
    for (size_t i = 0; i < lt.elems.size(); ++i) {
        const CGValue& a = lt.elems[i];
        const CGValue& b = rt.elems[i];
        llvm::Value* e = nullptr;
        if (a.type == CGType::Arr && b.type == CGType::Arr) {
            // 数组元素深比较（t79）：下沉 rt_arr_eq（数组动态长度静态展开
            // 不可达，C 层先比 len 再逐元素按运行时 kind 对齐解释器）
            llvm::Value* c =
                builder_.CreateCall(rt_arr_eq_, {a.value, b.value}, "arreqtmp");
            e = builder_.CreateICmpNE(c, builder_.getInt64(0), "tupeq");
        } else if (a.type == CGType::Arr || b.type == CGType::Arr) {
            // Arr × 非 Arr 元素（kind 不等）恒 false
            e = builder_.getInt1(false);
        } else if (a.type == CGType::Tup && b.type == CGType::Tup) {
            // 嵌套 tuple 递归
            e = gen_tuple_eq(a, b, op);
        } else if (a.type == CGType::Tup || b.type == CGType::Tup ||
                   a.type == CGType::Obj || b.type == CGType::Obj) {
            // Tup × 非 Tup（kind 不等）/ 任一 Obj（解释器 values_equal
            // 无 Instance 分支）恒 false
            e = builder_.getInt1(false);
        } else if (a.type == CGType::Str && b.type == CGType::Str) {
            llvm::Value* c =
                builder_.CreateCall(rt_strcmp_, {a.value, b.value}, "strcmptmp");
            e = builder_.CreateICmpEQ(c, builder_.getInt32(0), "tupeq");
        } else if (a.type == CGType::Tri || b.type == CGType::Tri) {
            // tribool 三态判等：另一侧限 tribool/bool（bool 加宽三态），
            // 与非布尔配对即 kind 不等恒 false
            const CGValue& other = a.type == CGType::Tri ? b : a;
            if (other.type != CGType::Tri && other.type != CGType::Bool) {
                e = builder_.getInt1(false);
            } else {
                e = builder_.CreateICmpEQ(to_tri(a), to_tri(b), "tupeq");
            }
        } else if (a.type == CGType::Bool && b.type == CGType::Bool) {
            e = builder_.CreateICmpEQ(a.value, b.value, "tupeq");
        } else {
            auto is_numeric = [](CGType t) {
                return t == CGType::Int || t == CGType::Double || t == CGType::Num;
            };
            if (!is_numeric(a.type) || !is_numeric(b.type)) {
                // 剩余异型标量配对（Str×Int、Bool×Str 等）kind 不等恒 false
                e = builder_.getInt1(false);
            } else if (a.type == CGType::Num || b.type == CGType::Num) {
                // number 相等（t62）：rt_num_cmp op 0（双整数精确、混合 double 视图）
                llvm::Value* x = to_num(a);
                llvm::Value* y = to_num(b);
                llvm::Value* c = builder_.CreateCall(
                    rt_num_cmp_,
                    {builder_.getInt64(0), num_tag(x), num_bits(x),
                     num_tag(y), num_bits(y)},
                    "numcmp");
                e = builder_.CreateICmpNE(c, builder_.getInt32(0), "tupeq");
            } else if (a.type == CGType::Double || b.type == CGType::Double) {
                // 混合表示按 double 视图相等（5 == 5.0，对齐解释器 values_equal）
                e = builder_.CreateFCmpOEQ(to_double(a), to_double(b), "tupeq");
            } else {
                e = builder_.CreateICmpEQ(a.value, b.value, "tupeq");
            }
        }
        all = builder_.CreateAnd(all, e, "tupeqall");
    }
    return all;
}

int CodeGenerator::merge_tuple_arms(const std::vector<int>& tups,
                                    const std::vector<llvm::BasicBlock*>& ends,
                                    llvm::BasicBlock* merge_bb, const char* what,
                                    size_t line, size_t column) {
    // t95：tuple 合流静态展开，三阶段——阶段一纯元数据递归校验形状（元素数+
    // 名字表+嵌套位置全支同为 tuple）并合并各叶位类型（复用标量合流规则）；
    // 阶段二逐支把叶值对齐指令落在该支末块内（DFS 序展平）并补 Br；阶段三
    // 在 merge 块按同一 DFS 序逐叶 PHI，自底向上 register_tuple 重建新鲜值
    auto is_numeric = [](CGType t) {
        return t == CGType::Int || t == CGType::Double || t == CGType::Num;
    };

    // 阶段一：合并类型树（局部 arena，叶位记合并后 type/elem/cls，嵌套位记子树）
    struct MergedElem {
        CGType type = CGType::Int;
        CGType elem = CGType::Int;
        std::string cls;
        int sub = -1; // 嵌套 tuple：shapes 局部下标
    };
    struct MergedShape {
        std::vector<MergedElem> elems;
        std::vector<std::string> names;
    };
    std::vector<MergedShape> shapes;
    std::function<int(const std::vector<CGTuple>&)> build =
        [&](const std::vector<CGTuple>& tuple_arms) -> int {
        const CGTuple& first = tuple_arms.front();
        for (size_t k = 1; k < tuple_arms.size(); ++k) {
            if (tuple_arms[k].elems.size() != first.elems.size() ||
                tuple_arms[k].names != first.names) {
                unsupported(std::string(what) + " yield tuples of different shapes",
                            line, column);
            }
        }
        MergedShape shape;
        shape.names = first.names;
        for (size_t j = 0; j < first.elems.size(); ++j) {
            MergedElem m;
            m.type = first.elems[j].type;
            m.elem = first.elems[j].elem;
            m.cls = first.elems[j].cls;
            if (m.type == CGType::Tup) {
                // 嵌套位置须全支同为 tuple，递归合流（按值拷贝不留引用）
                std::vector<CGTuple> subs;
                subs.reserve(tuple_arms.size());
                for (const auto& a : tuple_arms) {
                    if (a.elems[j].type != CGType::Tup) {
                        unsupported(std::string(what) +
                                        " yield tuples of different shapes",
                                    line, column);
                    }
                    subs.push_back(tuple_values_[a.elems[j].tup]);
                }
                m.sub = build(subs);
                shape.elems.push_back(std::move(m));
                continue;
            }
            for (size_t k = 1; k < tuple_arms.size(); ++k) {
                const CGValue& v = tuple_arms[k].elems[j];
                if (v.type == CGType::Tup) {
                    unsupported(std::string(what) +
                                    " yield tuples of different shapes",
                                line, column);
                }
                if (v.type == m.type) {
                    if (v.type == CGType::Arr && v.elem != m.elem) {
                        m.elem = CGType::Num; // elem 不等降动态域哨兵（t94 同规则）
                    }
                    if (v.type == CGType::Obj && v.cls != m.cls) {
                        const std::string nca = nearest_common_ancestor(m.cls, v.cls);
                        if (nca.empty()) {
                            unsupported(std::string(what) +
                                            " yield tuples with incompatible elements",
                                        line, column);
                        }
                        m.cls = nca; // 最近公共祖先（t93 同规则）
                    }
                    continue;
                }
                if (is_numeric(m.type) && is_numeric(v.type)) {
                    m.type = (m.type == CGType::Num || v.type == CGType::Num)
                                 ? CGType::Num
                                 : CGType::Double;
                    continue;
                }
                if ((m.type == CGType::Tri && v.type == CGType::Bool) ||
                    (m.type == CGType::Bool && v.type == CGType::Tri)) {
                    m.type = CGType::Tri;
                    continue;
                }
                unsupported(std::string(what) +
                                " yield tuples with incompatible elements",
                            line, column);
            }
            shape.elems.push_back(std::move(m));
        }
        shapes.push_back(std::move(shape));
        return static_cast<int>(shapes.size()) - 1;
    };
    // 根 tuple 按值拷贝不留引用（阶段三 register_tuple 会扩容 tuple_values_）
    std::vector<CGTuple> roots;
    roots.reserve(tups.size());
    for (int t : tups) {
        roots.push_back(tuple_values_[t]);
    }
    const int root = build(roots);

    // 阶段二：逐支叶值对齐（转换指令须落在该支末块内）+ Br 至 merge 块
    std::vector<std::vector<llvm::Value*>> flat(ends.size());
    for (size_t k = 0; k < ends.size(); ++k) {
        builder_.SetInsertPoint(ends[k]);
        std::function<void(int, const CGTuple&)> walk = [&](int si,
                                                            const CGTuple& arm) {
            const MergedShape& shape = shapes[si];
            for (size_t j = 0; j < shape.elems.size(); ++j) {
                const MergedElem& m = shape.elems[j];
                const CGValue& v = arm.elems[j];
                if (m.sub >= 0) {
                    walk(m.sub, tuple_values_[v.tup]);
                    continue;
                }
                flat[k].push_back(m.type == CGType::Double ? to_double(v)
                                  : m.type == CGType::Num  ? to_num(v)
                                  : m.type == CGType::Tri  ? to_tri(v)
                                                           : v.value);
            }
        };
        walk(root, roots[k]);
        builder_.CreateBr(merge_bb);
    }

    // 阶段三：merge 块逐叶 PHI（DFS 序与阶段二一致），自底向上重建新鲜 CGTuple
    builder_.SetInsertPoint(merge_bb);
    size_t leaf = 0;
    std::function<int(int)> rebuild = [&](int si) -> int {
        CGTuple out;
        out.names = shapes[si].names;
        for (const MergedElem& m : shapes[si].elems) {
            if (m.sub >= 0) {
                CGValue v;
                v.type = CGType::Tup;
                v.tup = rebuild(m.sub);
                out.elems.push_back(std::move(v));
                continue;
            }
            llvm::PHINode* phi = builder_.CreatePHI(
                llvm_type_of(m.type), static_cast<unsigned>(ends.size()), "tupmerge");
            for (size_t k = 0; k < ends.size(); ++k) {
                phi->addIncoming(flat[k][leaf], ends[k]);
            }
            ++leaf;
            CGValue v;
            v.value = phi;
            v.type = m.type;
            v.elem = m.elem;
            v.cls = m.cls;
            out.elems.push_back(std::move(v));
        }
        return register_tuple(std::move(out));
    };
    return rebuild(root);
}

void CodeGenerator::gen_multi_match(const MultiMatchExpr& expr) {
    const Token& op = expr.op();
    // 目标只求值一次；级联块链按分支序/候选序比较，命中跳分支结果块、
    // 未中顺延下一候选，天然对齐解释器首命中 + 惰性求值（visitMultiMatch）
    CGValue target = emit(expr.target());
    // 无默认分支仅 tribool 目标合法（t65）：语义层已保证候选字面量
    // 穷尽三态，此处防御目标类型；其余无默认拒编
    if (expr.default_expr() == nullptr && target.type != CGType::Tri) {
        unsupported("'==?' without a trailing default branch on non-tribool target",
                    op.line(), op.column());
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    // 条件发射区（t110）：候选惰性求值、分支结果互为替代路径，
    // 区内 uninit 清除逐段隔离不外泄（目标已在区前发射）
    const auto uninit_snap = uninit_snapshot();
    CondDepthGuard _cdg(cond_depth_); // t117：区内 tuple 换形状拒编

    struct Arm {
        CGValue value;
        llvm::BasicBlock* end = nullptr; // 结果求值结束块（求值可能新建块）
        llvm::Value* aligned = nullptr;  // 对齐 result_type 后的值
    };
    std::vector<Arm> arms;

    for (const auto& branch : expr.branches()) {
        auto* body_bb = llvm::BasicBlock::Create(context_, "match.body", fn);
        for (const auto& value : branch.values) {
            CGValue cand = emit(value.get());
            llvm::Value* eq = gen_match_eq(target, cand, op);
            auto* next_bb = llvm::BasicBlock::Create(context_, "match.next", fn);
            builder_.CreateCondBr(eq, body_bb, next_bb);
            builder_.SetInsertPoint(next_bb);
        }
        llvm::BasicBlock* chain_bb = builder_.GetInsertBlock(); // 比较链续点
        uninit_restore(uninit_snap); // 后段候选清除不支配 body（首候选命中即可进入）
        builder_.SetInsertPoint(body_bb);
        Arm arm;
        arm.value = emit(branch.result.get());
        arm.end = builder_.GetInsertBlock();
        arms.push_back(std::move(arm));
        uninit_restore(uninit_snap); // 臂间互为替代路径，逐个隔离
        builder_.SetInsertPoint(chain_bb);
    }
    // 全部未命中落到默认分支（比较链末端即默认块）；tribool 穷尽形式
    // 无默认（t65）：i8 值域严格 {0,1,2} 且候选已穷尽三态，链尾静态不可达
    if (expr.default_expr() != nullptr) {
        Arm def;
        def.value = emit(expr.default_expr());
        def.end = builder_.GetInsertBlock();
        arms.push_back(std::move(def));
        uninit_restore(uninit_snap); // 默认臂同为替代路径，隔离
    } else {
        builder_.CreateUnreachable();
    }

    // 结果类型统一：沿用 gen_ternary 规则扩展到 N+1 支（同型直用含 Arr elem/
    // Obj cls 一致性校验；数值混型任一 Num 统一 Num 否则 Double；合不上时按
    // 承载族回退 Dyn）
    CGType result_type = arms.front().value.type;
    std::string result_cls = arms.front().value.cls;
    CGType result_elem = arms.front().value.elem;
    if (result_type == CGType::Void) {
        unsupported("'==?' branch result has no value", op.line(), op.column());
    }
    {
        // tuple 族合流（t95 静态 + t140 动态，同 gen_ternary）：全支同为 tuple
        // 且递归同形 → 静态 PHI 合流；否则各支物化动态堆对象 PHI 为 TupDyn
        const auto is_tup = [](CGType ty) {
            return ty == CGType::Tup || ty == CGType::TupDyn;
        };
        // tuple 位混入其它承载臂不再整面拒编（t170，同三目触点）：全臂 tuple 仍走
        // 下方 tuple 合流，混排交由「静态合流失败即按承载族回退 Dyn」接住
        bool all_tup_family = true;
        for (const auto& arm : arms) {
            if (!is_tup(arm.value.type)) all_tup_family = false;
        }
        if (all_tup_family) {
            auto* tup_merge_bb = llvm::BasicBlock::Create(context_, "match.end", fn);
            bool all_static = true;
            for (const auto& arm : arms) {
                if (arm.value.type != CGType::Tup) all_static = false;
            }
            if (all_static) {
                std::vector<int> tups;
                std::vector<llvm::BasicBlock*> arm_ends;
                for (const auto& arm : arms) {
                    tups.push_back(arm.value.tup);
                    arm_ends.push_back(arm.end);
                }
                if (tuple_arms_static_mergeable(tups)) {
                    const int idx = merge_tuple_arms(tups, arm_ends, tup_merge_bb,
                                                     "'==?' branches", op.line(),
                                                     op.column());
                    last_value_ = {nullptr, CGType::Tup, CGType::Int, "", idx};
                    return;
                }
            }
            std::vector<llvm::Value*> dyn_vals;
            dyn_vals.reserve(arms.size());
            for (const auto& arm : arms) {
                builder_.SetInsertPoint(arm.end);
                llvm::Value* p = arm.value.type == CGType::TupDyn
                                     ? arm.value.value
                                     : materialize_tuple_dynamic(arm.value.tup,
                                                                 op.line(),
                                                                 op.column());
                dyn_vals.push_back(p);
                builder_.CreateBr(tup_merge_bb);
            }
            builder_.SetInsertPoint(tup_merge_bb);
            llvm::PHINode* phi = builder_.CreatePHI(
                llvm_type_of(CGType::TupDyn), static_cast<unsigned>(arms.size()),
                "tupdynmerge");
            for (size_t k = 0; k < arms.size(); ++k) {
                phi->addIncoming(dyn_vals[k], arms[k].end);
            }
            last_value_ = {phi, CGType::TupDyn};
            return;
        }
    }
    // 静态合流失败但全部臂落承载族时回退逐臂 to_dyn 打框 PHI 合流为 Dyn
    // （t168，同 gen_ternary）
    std::vector<CGValue> match_arm_values;
    match_arm_values.reserve(arms.size());
    for (const auto& arm : arms) {
        match_arm_values.push_back(arm.value);
    }
    const ArmMerge match_merge =
        merge_static_arm_types(match_arm_values, result_type, result_elem, result_cls);
    if (match_merge != ArmMerge::Ok) {
        if (!arms_all_dyn_carrier(match_arm_values)) {
            unsupported(match_merge == ArmMerge::NoCommonAncestor
                            ? "'==?' branches yield instances of different classes"
                            : "'==?' branches have incompatible types",
                        op.line(), op.column());
        }
        result_type = CGType::Dyn;
        result_elem = CGType::Int;
        result_cls.clear();
    }

    // 各分支结束块尾对齐 result_type 后跳 merge（转换指令须落在该分支块内）
    auto* merge_bb = llvm::BasicBlock::Create(context_, "match.end", fn);
    for (auto& arm : arms) {
        builder_.SetInsertPoint(arm.end);
        arm.aligned = result_type == CGType::Double ? to_double(arm.value)
                    : result_type == CGType::Num    ? to_num(arm.value)
                    : result_type == CGType::Tri    ? to_tri(arm.value)
                    : result_type == CGType::Dyn    ? to_dyn(arm.value)
                                                    : arm.value.value;
        builder_.CreateBr(merge_bb);
    }
    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(
        llvm_type_of(result_type), static_cast<unsigned>(arms.size()), "matchtmp");
    for (const auto& arm : arms) {
        phi->addIncoming(arm.aligned, arm.end);
    }
    // elem 仅 Arr 有意义——各支不同时统一 Num 动态域（t94）；cls 为各支最近公共祖先（t93）
    last_value_ = {phi, result_type, result_elem, result_cls};
}

llvm::Value* CodeGenerator::checked_int_arith(llvm::Intrinsic::ID id, llvm::Value* lhs,
                                              llvm::Value* rhs, const llvm::Twine& name) {
    // s{add,sub,mul}.with.overflow 返 {i64 结果, i1 溢出位}；溢出分支调陷阱
    // 报错退出（不回返，unreachable 收尾），把 CG1 的静默回绕变为显式报错；
    // 每检查点独立 trap/cont 块，后续 LLVM 优化自行合并
    llvm::Value* pair = builder_.CreateBinaryIntrinsic(id, lhs, rhs);
    llvm::Value* result = builder_.CreateExtractValue(pair, 0, name);
    llvm::Value* overflowed = builder_.CreateExtractValue(pair, 1, "ovf");
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::BasicBlock* trap_bb = llvm::BasicBlock::Create(context_, "ovf.trap", fn);
    llvm::BasicBlock* cont_bb = llvm::BasicBlock::Create(context_, "ovf.cont", fn);
    builder_.CreateCondBr(overflowed, trap_bb, cont_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_int_overflow_);
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    return result;
}

llvm::Value* CodeGenerator::check_bit_range(llvm::Value* v, long long max_val,
                                            const char* type_name) {
    // byte/word 赋值点范围检查（t69）：无符号比较 (u64)v > max 一次覆盖
    // 负数与超上限，越界调陷阱报错退出（对齐解释器 coerce_to_declared 报错）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* trap_bb = llvm::BasicBlock::Create(context_, "bitrange.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "bitrange.cont", fn);
    llvm::Value* bad = builder_.CreateICmpUGT(
        v, builder_.getInt64(static_cast<uint64_t>(max_val)), "bitrange.bad");
    builder_.CreateCondBr(bad, trap_bb, cont_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_bit_range_,
                        {builder_.CreateGlobalString(type_name),
                         builder_.getInt64(static_cast<uint64_t>(max_val)), v});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    return v;
}

llvm::Value* CodeGenerator::num_to_int_checked(llvm::Value* num) {
    // number → integer 窄化（t111）：tag 1（小数态）陷阱退出（对齐解释器
    // coerce_to_declared "cannot assign decimal value to 'integer' variable"，
    // 核心消息一致、位置前缀缺失同既定 CG 陷阱分歧）；整数态 bits 即 i64
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* trap_bb = llvm::BasicBlock::Create(context_, "numnarrow.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "numnarrow.cont", fn);
    llvm::Value* is_dec = builder_.CreateICmpNE(
        num_tag(num), builder_.getInt64(0), "numnarrow.bad");
    builder_.CreateCondBr(is_dec, trap_bb, cont_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_num_narrow_);
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    return num_bits(num);
}

llvm::Value* CodeGenerator::index_from_double(llvm::Value* d) {
    // double 视图下标（t122 引入，t150 抽为 helper）：非整数态（d != floor(d)，
    // NaN 经 FCmpOEQ 为 false）陷阱退出 "Index must be an integer"；整数态 floor
    // 后 clamp ±4e18 防 ±Infinity/超大值 FPToSI poison（随后落 rt 越界陷阱，与
    // 解释器 double 域 cast 后越界报错殊途同归），FPToSI 得 i64。负索引回绕与
    // 越界由 rt_arr_norm_index / rt_str_index 统一处理，故此处只产 i64 下标
    llvm::Value* fl =
        builder_.CreateUnaryIntrinsic(llvm::Intrinsic::floor, d, nullptr, "idxfl");
    llvm::Value* is_int = builder_.CreateFCmpOEQ(d, fl, "idxisint");
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* trap_bb = llvm::BasicBlock::Create(context_, "idxint.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "idxint.cont", fn);
    builder_.CreateCondBr(is_int, cont_bb, trap_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_index_integer_);
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    llvm::Value* lo = llvm::ConstantFP::get(builder_.getDoubleTy(), -4.0e18);
    llvm::Value* hi = llvm::ConstantFP::get(builder_.getDoubleTy(), 4.0e18);
    llvm::Value* cl =
        builder_.CreateBinaryIntrinsic(llvm::Intrinsic::maxnum, fl, lo);
    cl = builder_.CreateBinaryIntrinsic(llvm::Intrinsic::minnum, cl, hi);
    return builder_.CreateFPToSI(cl, builder_.getInt64Ty(), "idx");
}

llvm::Value* CodeGenerator::index_to_int(const CGValue& index, size_t line,
                                         size_t col) {
    if (index.type == CGType::Int) {
        return index.value;  // 静态整数下标零开销透传
    }
    if (index.type == CGType::Dyn) {
        // 动态承载值作下标（t150）：解释器 normalize_index 先判 is_number() 再
        // 判 floor——tag 0（整数态）bits 即 i64 直接透传；tag 1（小数态）还原
        // double 位模式走既有 floor/clamp 路径（1.0 可用、2.5 与 NaN 落陷阱）；
        // 其余位（bool/tribool/串/数组/实例）落新增 rt 陷阱，消息照解释器
        // "Index must be a number, got <kind>"（拒错编从陷阱）
        llvm::Function* fn = builder_.GetInsertBlock()->getParent();
        auto* tag_bb = llvm::BasicBlock::Create(context_, "dynidx.tag", fn);
        auto* dec_bb = llvm::BasicBlock::Create(context_, "dynidx.dec", fn);
        auto* trap_bb = llvm::BasicBlock::Create(context_, "dynidx.trap", fn);
        auto* done_bb = llvm::BasicBlock::Create(context_, "dynidx.done", fn);
        llvm::Value* tag = num_tag(index.value);
        llvm::Value* bits = num_bits(index.value);
        llvm::BasicBlock* entry_bb = builder_.GetInsertBlock();
        llvm::Value* is_int_tag =
            builder_.CreateICmpEQ(tag, builder_.getInt64(0), "dynidx.isint");
        builder_.CreateCondBr(is_int_tag, done_bb, tag_bb);
        builder_.SetInsertPoint(tag_bb);
        llvm::Value* is_dec_tag =
            builder_.CreateICmpEQ(tag, builder_.getInt64(1), "dynidx.isdec");
        builder_.CreateCondBr(is_dec_tag, dec_bb, trap_bb);
        builder_.SetInsertPoint(dec_bb);
        llvm::Value* d =
            builder_.CreateBitCast(bits, builder_.getDoubleTy(), "dynidx.f64");
        llvm::Value* from_dec = index_from_double(d);
        // helper 自带 floor 陷阱块，返回值定义在其 cont 块而非 dec 块
        llvm::BasicBlock* dec_from_bb = builder_.GetInsertBlock();
        builder_.CreateBr(done_bb);
        builder_.SetInsertPoint(trap_bb);
        builder_.CreateCall(rt_trap_index_kind_, {tag});
        builder_.CreateUnreachable();
        builder_.SetInsertPoint(done_bb);
        llvm::PHINode* result = builder_.CreatePHI(builder_.getInt64Ty(), 2, "dynidx");
        result->addIncoming(bits, entry_bb);
        result->addIncoming(from_dec, dec_from_bb);
        return result;
    }
    if (index.type != CGType::Double && index.type != CGType::Num) {
        // Bool/Str 等非数值下标：解释器报 "Index must be a number"（错误对齐面，
        // 非 stdout 差分），维持拒编不错编
        unsupported("non-integer index", line, col);
    }
    // number/decimal 下标（t122）：镜像解释器 normalize_index——转 double 视图
    //（to_double：Num 按 tag select、Double 直用）后走同一 floor/clamp 路径
    return index_from_double(to_double(index));
}

llvm::Value* CodeGenerator::to_double(const CGValue& v) {
    switch (v.type) {
        case CGType::Double: return v.value;
        case CGType::Int:    return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
        case CGType::Bool:   return builder_.CreateUIToFP(v.value, builder_.getDoubleTy());
        case CGType::Num: {
            // Num → double 视图（t90）：tag 0 整数 SIToFP、tag 1 位模式还原，
            // 两分支均无副作用，select 免分支
            llvm::Value* tag = num_tag(v.value);
            llvm::Value* bits = num_bits(v.value);
            llvm::Value* as_int =
                builder_.CreateSIToFP(bits, builder_.getDoubleTy());
            llvm::Value* as_dbl =
                builder_.CreateBitCast(bits, builder_.getDoubleTy());
            llvm::Value* is_int =
                builder_.CreateICmpEQ(tag, builder_.getInt64(0), "numisint");
            return builder_.CreateSelect(is_int, as_int, as_dbl, "numdbl");
        }
        default:             unsupported("numeric conversion of non-numeric value", 0, 0);
    }
}

llvm::Value* CodeGenerator::to_tri(const CGValue& v) {
    // bool → tribool 单向加宽（t65）：false→0 / true→2（select 免分支）；
    // Tri 透传；其余类型拒编（编码 False=0 < Unset=1 < True=2）
    switch (v.type) {
        case CGType::Tri:  return v.value;
        case CGType::Bool:
            return builder_.CreateSelect(v.value, builder_.getInt8(2),
                                         builder_.getInt8(0), "tritmp");
        default:           unsupported("conversion to 'tribool'", 0, 0);
    }
}

llvm::Value* CodeGenerator::make_num(llvm::Value* tag, llvm::Value* bits) {
    // number 值组装（t62）：{i64 tag, i64 bits} first-class struct，SSA 单值流转
    llvm::Value* v = llvm::PoisonValue::get(llvm_type_of(CGType::Num));
    v = builder_.CreateInsertValue(v, tag, 0);
    return builder_.CreateInsertValue(v, bits, 1, "numtmp");
}

llvm::Value* CodeGenerator::num_tag(llvm::Value* num) {
    return builder_.CreateExtractValue(num, 0, "numtag");
}

llvm::Value* CodeGenerator::num_bits(llvm::Value* num) {
    return builder_.CreateExtractValue(num, 1, "numbits");
}

llvm::Value* CodeGenerator::dyn_obj_guard(const CGValue& v,
                                          const std::string& member,
                                          bool is_method) {
    // kind 7 动态槽上的实例消费（t145）：tag 5 才是类实例，非 5 落运行期
    // 陷阱退出——解释器对非实例接收者同为运行期报错，拒错编从陷阱
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* obj_bb = llvm::BasicBlock::Create(context_, "dynobj", fn);
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dynobj.trap", fn);
    llvm::Value* tag = num_tag(v.value);
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(tag, builder_.getInt64(5), "isdynobj"),
        obj_bb, trap_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(
        is_method ? rt_trap_undefined_method_ : rt_trap_undefined_property_,
        {builder_.CreateGlobalString(member)});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(obj_bb);
    return builder_.CreateIntToPtr(num_bits(v.value),
                                   llvm_type_of(CGType::Obj), "dynobjptr");
}

CodeGenerator::CGValue CodeGenerator::read_obj_field(const CGValue& object,
                                      const std::string& name,
                                      size_t line,
                                      size_t column,
                                      bool trap_when_undefined) {
    // 类实例字段读（t60/t102/t103，t170 自 visitProperty 抽出供 Dyn tag 分派
    // 复用）：同树定义者收集——downcast 放行后动态类可为整棵继承树上任意类
    // （旁支可经共同祖先中转写入槽）；单类树维持直 GEP + load 零开销，多类树
    // 读对象头类 id switch 到定义者字段槽（各定义者按自身 field_index 下标
    // GEP），default（动态类实例无此字段）走运行期陷阱不错值——消息对齐解释器
    // "Undefined property 'X' on object"
    std::vector<const CGClass*> defs;
    bool lone_tree = true;
    if (object.cls.empty()) {
        // 空 cls = 动态 object（跨树实例数组元素 t137 / object 形参与返回值
        // S81 / 无初值变量 S69）字段读（t142）：静态类无从知晓，全体定义了
        // 该字段的类均入 switch case（不限同树，同 t133 方法动态分派），按
        // 对象头类 id 运行期取值；动态类未定义该字段落 default 陷阱臂（解释器
        // 同为运行期报错，非 stdout 差分面）；lone_tree 恒 false——真实类
        // 可能不在 defs 中，必须保留 default 兜底；各定义者字段声明类型全等时
        // 维持单型 PHI（t102 静态精度），不一致时自 t176 起逐臂打框合流为 Dyn
        for (const auto& entry : classes_) {
            if (entry.second.field_index.count(name) != 0) {
                defs.push_back(&entry.second);
            }
        }
        lone_tree = false;
    } else {
        for (const auto& entry : classes_) {
            if (entry.first != object.cls) {
                if (nearest_common_ancestor(entry.first, object.cls).empty()) {
                    continue;
                }
                lone_tree = false;
            }
            if (entry.second.field_index.count(name) != 0) {
                defs.push_back(&entry.second);
            }
        }
    }
    if (defs.empty()) {
        if (!trap_when_undefined) {
            unsupported("undefined property '" + name + "' on " +
                            (object.cls.empty()
                                 ? std::string("dynamic object")
                                 : "class '" + object.cls + "'"),
                        line, column);
        }
        // Dyn 接收者的实例位（t170）：整臂为运行期陷阱（tuple 臂由调用侧另
        // 分派），块 terminate 后回空值占位让调用侧跳过合流边
        builder_.CreateCall(rt_trap_undefined_property_,
                            {builder_.CreateGlobalString(name)});
        builder_.CreateUnreachable();
        return {nullptr, CGType::Dyn, CGType::Int, std::string()};
    }
    if (lone_tree) {
        // 单类树：动态类必为静态类，直 GEP + load 零开销（现状保持）
        const CGClass& cls = classes_.at(object.cls);
        auto it = cls.field_index.find(name);
        const CGField& field = cls.fields[it->second];
        llvm::Value* slot =
            builder_.CreateStructGEP(cls.type, object.value, it->second + 1, name);
        // Arr 字段 elem 记 Num 哨兵（t71：CGField 无元素类型伴随，读出
        // 即动态域，同 t70 形参机制）；Obj 字段带类名（t72）
        return {builder_.CreateLoad(llvm_type_of(field.type), slot, name),
                field.type,
                field.type == CGType::Arr ? CGType::Num : CGType::Int,
                field.cls};
    }
    const CGField& ref = defs[0]->fields.at(defs[0]->field_index.at(name));
    std::sort(defs.begin(), defs.end(),
              [](const CGClass* a, const CGClass* b) {
                  return a->id < b->id;
              });
    // 各定义者字段类型全等时维持单型 PHI（t86 静态精度不丢）；不一致时
    // （t176）逐臂按自身声明类型 load 后 to_dyn 打框合流为 Dyn——解释器读的是
    // 动态类的实际字段值，「静态类型无法统一」不等于「值无法读」，合流后即落
    // S92–S122 全部运行期 tag 分派消费面
    bool uniform = true;
    for (const CGClass* sub : defs) {
        const CGField& f = sub->fields.at(sub->field_index.at(name));
        if (f.type != ref.type || f.cls != ref.cls || f.bit_max != ref.bit_max) {
            uniform = false;
        }
    }
    if (!uniform) {
        // 全部字段声明类型都在动态承载集内（CGType 全集皆承载），逐臂打框即可
        llvm::Value* cclsid = builder_.CreateLoad(
            builder_.getInt64Ty(), object.value, "clsid");
        llvm::Function* cfn = builder_.GetInsertBlock()->getParent();
        auto* ctrap_bb = llvm::BasicBlock::Create(context_, "propty.trap", cfn);
        auto* cmerge_bb =
            llvm::BasicBlock::Create(context_, "propty.end", cfn);
        llvm::SwitchInst* csw = builder_.CreateSwitch(
            cclsid, ctrap_bb, static_cast<unsigned>(defs.size()));
        std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> cincoming;
        for (const CGClass* sub : defs) {
            auto* bb = llvm::BasicBlock::Create(
                context_,
                "propty." + std::string(sub->stmt->name().lexeme()), cfn);
            csw->addCase(builder_.getInt64(sub->id), bb);
            builder_.SetInsertPoint(bb);
            const CGField& f = sub->fields.at(sub->field_index.at(name));
            const unsigned idx = sub->field_index.at(name) + 1; // 跳过类 id 头部
            llvm::Value* slot =
                builder_.CreateStructGEP(sub->type, object.value, idx, name);
            llvm::Value* loaded =
                builder_.CreateLoad(llvm_type_of(f.type), slot, name);
            CGValue arm{loaded, f.type,
                        f.type == CGType::Arr ? CGType::Num : CGType::Int,
                        f.cls};
            cincoming.emplace_back(
                builder_.GetInsertBlock(), to_dyn(arm, line, column));
            builder_.CreateBr(cmerge_bb);
        }
        builder_.SetInsertPoint(ctrap_bb);
        builder_.CreateCall(rt_trap_undefined_property_,
                            {builder_.CreateGlobalString(name)});
        builder_.CreateUnreachable();
        builder_.SetInsertPoint(cmerge_bb);
        llvm::PHINode* dphi = builder_.CreatePHI(
            llvm_type_of(CGType::Dyn), static_cast<unsigned>(cincoming.size()),
            "propdyn");
        for (const auto& in : cincoming) {
            dphi->addIncoming(in.second, in.first);
        }
        // 读出即 Dyn：类名/elem 元数据无静态归属，消费按运行期 tag 分派
        return {dphi, CGType::Dyn, CGType::Int, std::string()};
    }
    // switch：case 各定义者（按 id 排序），default = 陷阱
    llvm::Value* clsid = builder_.CreateLoad(
        builder_.getInt64Ty(), object.value, "clsid");
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* trap_bb = llvm::BasicBlock::Create(context_, "prop.trap", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, "prop.end", fn);
    llvm::SwitchInst* sw = builder_.CreateSwitch(
        clsid, trap_bb, static_cast<unsigned>(defs.size()));
    std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> incoming;
    for (const CGClass* sub : defs) {
        auto* bb = llvm::BasicBlock::Create(
            context_,
            "prop." + std::string(sub->stmt->name().lexeme()), fn);
        sw->addCase(builder_.getInt64(sub->id), bb);
        builder_.SetInsertPoint(bb);
        const unsigned idx = sub->field_index.at(name) + 1; // 跳过类 id 头部
        llvm::Value* slot =
            builder_.CreateStructGEP(sub->type, object.value, idx, name);
        llvm::Value* loaded =
            builder_.CreateLoad(llvm_type_of(ref.type), slot, name);
        incoming.emplace_back(builder_.GetInsertBlock(), loaded);
        builder_.CreateBr(merge_bb);
    }
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_undefined_property_,
                        {builder_.CreateGlobalString(name)});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(
        llvm_type_of(ref.type), static_cast<unsigned>(incoming.size()), "proptmp");
    for (const auto& in : incoming) {
        phi->addIncoming(in.second, in.first);
    }
    // Arr 字段 elem 记 Num 哨兵（t71）；Obj 字段带类名（t72）
    return {phi, ref.type,
            ref.type == CGType::Arr ? CGType::Num : CGType::Int, ref.cls};
}

llvm::Value* CodeGenerator::dyn_obj_clsid_or_sentinel(llvm::Value* tag,
                                                      llvm::Value* ptr) {
    // Dyn 接收者的实例位类 id 取址拆分（t159）：只有 tag==5 的 bits 才是实例
    // ptr，非实例位若直接解引用会把标量位模式读成类 id（可能撞真类 id 而错派）；
    // 故 tag!=5 走哨兵 id（~0，注册序从 0 起永不命中）落分派 default 臂，
    // 该臂按 {tag,bits} 转串。this 实参在哨兵路径不被使用（default 不调方法）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* load_bb = llvm::BasicBlock::Create(context_, "dynobj.load", fn);
    auto* sentinel_bb = llvm::BasicBlock::Create(context_, "dynobj.sentinel", fn);
    auto* join_bb = llvm::BasicBlock::Create(context_, "dynobj.join", fn);
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(tag, builder_.getInt64(5), "isdynobj"),
        load_bb, sentinel_bb);
    builder_.SetInsertPoint(load_bb);
    llvm::Value* id =
        builder_.CreateLoad(builder_.getInt64Ty(), ptr, "dynclsid");
    builder_.CreateBr(join_bb);
    builder_.SetInsertPoint(sentinel_bb);
    builder_.CreateBr(join_bb);
    builder_.SetInsertPoint(join_bb);
    llvm::PHINode* phi = builder_.CreatePHI(builder_.getInt64Ty(), 2, "dynobjclsid");
    phi->addIncoming(id, load_bb);
    phi->addIncoming(builder_.getInt64(~0ULL), sentinel_bb);
    return phi;
}

bool CodeGenerator::is_dyn_unbox_target(CGType t) {
    // 解释器 coerce_to_declared 对这六个声明类型有运行期 kind 校验，Dyn 值
    // 的 tag 也恰好覆盖其全部可接受来源；其余落点需静态信息，拒编不误
    return t == CGType::Int || t == CGType::Double || t == CGType::Num ||
           t == CGType::Bool || t == CGType::Tri || t == CGType::Str;
}

const char* CodeGenerator::bit_type_name(long long bit_max) {
    return bit_max == 255 ? "byte" : (bit_max > 0 ? "word" : nullptr);
}

llvm::Value* CodeGenerator::dyn_unbox_to(llvm::Value* dyn, CGType slot_type,
                                         size_t line, size_t column,
                                         const char* target_name) {
    // 动态承载值拆槽（t147）：tag 运行期才定，按解释器 coerce_to_declared
    // 的接受表插守卫链——命中块还原静态表示后合流，全不命中落类型陷阱
    // （解释器同为运行期报错退出，陷阱路径 stdout 无增量，拒错编从陷阱）；
    // target_name 为陷阱消息里的落点类型名（byte/word 落点由调用侧给定），
    // 范围校验仍走调用侧既有 t69 check_bit_range 通道
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::Value* tag = num_tag(dyn);
    llvm::Value* bits = num_bits(dyn);
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dynbox.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "dynbox.cont", fn);
    const char* target = target_name;
    std::vector<std::pair<long long, std::function<llvm::Value*(llvm::Value*)>>> arms;
    switch (slot_type) {
        case CGType::Int:
            target = "integer";
            arms.push_back({0, [](llvm::Value* b) { return b; }});
            break;
        case CGType::Double:
            target = "decimal";
            // 整数态加宽小数表示、小数态位模式还原（对齐解释器整值隐式加宽）
            arms.push_back({0, [this](llvm::Value* b) {
                return builder_.CreateSIToFP(b, builder_.getDoubleTy());
            }});
            arms.push_back({1, [this](llvm::Value* b) {
                return builder_.CreateBitCast(b, builder_.getDoubleTy());
            }});
            break;
        case CGType::Num:
            target = "number";
            // 双态皆合法：整值透传（Num 与 Dyn 同 {i64,i64} 布局、tag 编码兼容）
            arms.push_back({0, [dyn](llvm::Value*) { return dyn; }});
            arms.push_back({1, [dyn](llvm::Value*) { return dyn; }});
            break;
        case CGType::Bool:
            target = "bool";
            arms.push_back({2, [this](llvm::Value* b) {
                return builder_.CreateTrunc(b, builder_.getInt1Ty());
            }});
            break;
        case CGType::Tri:
            target = "tribool";
            arms.push_back({6, [this](llvm::Value* b) {
                return builder_.CreateTrunc(b, builder_.getInt8Ty());
            }});
            // bool 位加宽 tribool（解释器 t43：false→0 / true→2）
            arms.push_back({2, [this](llvm::Value* b) {
                llvm::Value* bo = builder_.CreateTrunc(b, builder_.getInt1Ty());
                return builder_.CreateSelect(bo, builder_.getInt8(2),
                                             builder_.getInt8(0), "dyntri");
            }});
            break;
        case CGType::Str:
            target = "string";
            arms.push_back({3, [this](llvm::Value* b) {
                return builder_.CreateIntToPtr(b, llvm_type_of(CGType::Str),
                                               "dynstrptr");
            }});
            // 数值/布尔隐式转串（解释器 coerce_to_declared KW_STRING：number/
            // bool 直存 to_string 结果，转串垫片与 to_str 同源格式）
            arms.push_back({0, [this](llvm::Value* b) {
                return builder_.CreateCall(rt_i64_to_str_, {b}, "dynboxstr");
            }});
            arms.push_back({1, [this](llvm::Value* b) {
                llvm::Value* d = builder_.CreateBitCast(b, builder_.getDoubleTy());
                return builder_.CreateCall(rt_f64_to_str_, {d}, "dynboxstr");
            }});
            arms.push_back({2, [this](llvm::Value* b) {
                llvm::Value* ext = builder_.CreateTrunc(b, builder_.getInt32Ty());
                return builder_.CreateCall(rt_bool_to_str_, {ext}, "dynboxstr");
            }});
            break;
        default:
            unsupported("dynamic value in this slot", line, column);
    }
    if (target_name != nullptr) {
        target = target_name; // byte/word 落点消息用声明类型名（解释器同文案）
    }
    // 逐 tag 判定链：命中即求值并跳合流块，链末端（全不命中）落陷阱
    std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> incoming;
    for (const auto& arm : arms) {
        auto* hit_bb = llvm::BasicBlock::Create(context_, "dynbox.hit", fn);
        auto* next_bb = llvm::BasicBlock::Create(context_, "dynbox.next", fn);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(tag, builder_.getInt64(static_cast<uint64_t>(arm.first)),
                                  "dynboxis"),
            hit_bb, next_bb);
        builder_.SetInsertPoint(hit_bb);
        llvm::Value* unboxed = arm.second(bits);
        incoming.emplace_back(unboxed, builder_.GetInsertBlock());
        builder_.CreateBr(cont_bb);
        builder_.SetInsertPoint(next_bb);
    }
    builder_.CreateBr(trap_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_dyn_convert_,
                        {tag, builder_.CreateGlobalString(target)});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    if (incoming.size() == 1) {
        return incoming.front().first;
    }
    llvm::PHINode* phi = builder_.CreatePHI(incoming.front().first->getType(),
                                            static_cast<unsigned>(incoming.size()),
                                            "dynbox");
    for (const auto& inc : incoming) {
        phi->addIncoming(inc.first, inc.second);
    }
    return phi;
}

llvm::Value* CodeGenerator::dyn_unbox_to_obj(llvm::Value* dyn,
                                             const std::string& cls,
                                             size_t line, size_t column) {
    // 动态承载值进类名落点（t158）：静态解码需两件事同时成立——tag 5（实例位）
    // 且对象头类 id 属声明类或其后代类（字段沿继承链前缀布局偏移可定、方法按
    // id 走 t61/t86 动态分派）。解释器 coerce_to_declared 对类名 default 分支
    // 直存不校验，故其余情形（标量/数组位、跨树或兄弟类实例）在解释器可运行
    // 到成员访问才报错；codegen 无运行期动态字段解码面，落既有
    // collie_rt_trap_dyn_convert 陷阱（消息核心对齐 "Type mismatch: cannot
    // assign <kind> to '<cls>' variable"，拒错编从陷阱）
    const auto class_it = classes_.find(cls);
    if (class_it == classes_.end()) {
        unsupported("class '" + cls + "' for dynamic value", line, column);
    }
    std::vector<uint64_t> ids{class_it->second.id};
    for (const auto& entry : classes_) {
        if (entry.first != cls && is_subclass_of(entry.first, cls)) {
            ids.push_back(entry.second.id);
        }
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* ok_bb = llvm::BasicBlock::Create(context_, "dynobj.ok", fn);
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dynobj.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "dynobj.cont", fn);
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(num_tag(dyn), builder_.getInt64(5), "dynobjis"),
        ok_bb, trap_bb);
    builder_.SetInsertPoint(ok_bb);
    llvm::Value* ptr = builder_.CreateIntToPtr(num_bits(dyn),
                                               llvm_type_of(CGType::Obj),
                                               "dynobjptr");
    llvm::Value* clsid =
        builder_.CreateLoad(builder_.getInt64Ty(), ptr, "dynobjclsid");
    if (ids.size() == 1) {
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(clsid, builder_.getInt64(ids[0]), "dynobjsame"),
            cont_bb, trap_bb);
    } else {
        // 后代类并存：各合法 id 汇入同一命中块（id 顺序无关，switch 去重由
        // LLVM 处理）
        auto* hit_bb = llvm::BasicBlock::Create(context_, "dynobj.hit", fn);
        llvm::SwitchInst* sw =
            builder_.CreateSwitch(clsid, trap_bb, static_cast<unsigned>(ids.size()));
        for (uint64_t id : ids) {
            sw->addCase(builder_.getInt64(id), hit_bb);
        }
        builder_.SetInsertPoint(hit_bb);
        builder_.CreateBr(cont_bb);
    }
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_dyn_convert_,
                        {num_tag(dyn), builder_.CreateGlobalString(cls)});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    return ptr;
}

llvm::Value* CodeGenerator::dyn_to_arr_slot(llvm::Value* dyn) {
    // 动态承载值进 array 落点（t165）：解释器 coerce_to_declared 的 KW_ARRAY 分支
    // 仅要求 is_array()，元素 kind 随数组对象自带，故 tag ∈ {4,7}（同质嵌套数组位
    // t85/t160、异质数组位 t139）即可还原 opaque ptr 并入既有动态域（调用侧把落点
    // elem 降 Num 哨兵，读写按运行期 kind 产出 Dyn，t70/t151）；其余位（标量/实例）
    // 落既有 collie_rt_trap_dyn_convert，复刻解释器 "Type mismatch: cannot assign
    // <kind> to 'array' variable"（探针实证 integer 位为 number、Obj 位为 object）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::Value* tag = num_tag(dyn);
    auto* ok_bb = llvm::BasicBlock::Create(context_, "dynarr.ok", fn);
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dynarr.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "dynarr.cont", fn);
    llvm::Value* is_arr = builder_.CreateOr(
        builder_.CreateICmpEQ(tag, builder_.getInt64(4), "dynarris4"),
        builder_.CreateICmpEQ(tag, builder_.getInt64(7), "dynarris7"), "dynarris");
    builder_.CreateCondBr(is_arr, ok_bb, trap_bb);
    builder_.SetInsertPoint(ok_bb);
    llvm::Value* ptr = builder_.CreateIntToPtr(num_bits(dyn),
                                               llvm_type_of(CGType::Arr),
                                               "dynarrptr");
    builder_.CreateBr(cont_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_dyn_convert_,
                        {tag, builder_.CreateGlobalString("array")});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    return ptr;
}

llvm::Value* CodeGenerator::dyn_to_tuple_slot(llvm::Value* dyn) {
    // 动态承载值进 Tuple 落点（t179）：解释器 coerce_to_declared 的 KW_TUPLE 分支
    // 只要求 is_tuple()，而 tag 8（t170）的 bits 就是堆 tuple 对象 ptr——守卫命中即
    // 还原并入既有动态 tuple 消费面（t140/t163/t170/t177）；tuple 运行期不可变，
    // 多槽共享同一堆对象无别名风险。其余位落既有 collie_rt_trap_dyn_convert，
    // 复刻解释器 "Type mismatch: cannot assign <kind> to 'Tuple' variable"
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::Value* tag = num_tag(dyn);
    auto* ok_bb = llvm::BasicBlock::Create(context_, "dyntup.ok", fn);
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dyntup.trap", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "dyntup.cont", fn);
    builder_.CreateCondBr(
        builder_.CreateICmpEQ(tag, builder_.getInt64(8), "dyntupis8"), ok_bb,
        trap_bb);
    builder_.SetInsertPoint(ok_bb);
    llvm::Value* ptr = builder_.CreateIntToPtr(num_bits(dyn),
                                               llvm_type_of(CGType::TupDyn),
                                               "dyntupptr");
    builder_.CreateBr(cont_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_dyn_convert_,
                        {tag, builder_.CreateGlobalString("Tuple")});
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(cont_bb);
    return ptr;
}

llvm::Value* CodeGenerator::dyn_num_operand(const CGValue& v, const Token& op) {
    // 非 Dyn 侧走既有 to_num 加宽；Dyn 侧 tag 运行期才定——整数态/小数态
    // （tag < 2）直接透传 {tag,bits}（与 Num 同构同编码），其余（bool/串/
    // tribool/实例/数组）落陷阱，对齐解释器 eval_arithmetic 的
    // "Arithmetic operands must be numbers" 运行期报错
    if (v.type != CGType::Dyn) {
        return to_num(v);
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* ok_bb = llvm::BasicBlock::Create(context_, "dynnum.ok", fn);
    auto* trap_bb = llvm::BasicBlock::Create(context_, "dynnum.trap", fn);
    llvm::Value* numeric = builder_.CreateICmpULT(
        num_tag(v.value), builder_.getInt64(2), "dynnumis");
    builder_.CreateCondBr(numeric, ok_bb, trap_bb);
    builder_.SetInsertPoint(trap_bb);
    builder_.CreateCall(rt_trap_non_numeric_arith_);
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(ok_bb);
    return v.value;
}

llvm::Value* CodeGenerator::dyn_concat_or_add(const CGValue& lhs,
                                              const CGValue& rhs,
                                              const Token& op) {
    // '+' 含 Dyn 侧（t148）：解释器任一侧为字符串即两侧 to_string 拼接，
    // 而 Dyn 侧是否字符串只有运行期才知道，故按 tag==3 分派两路后合流为
    // CGType::Dyn——拼接路打 tag 3 存串 ptr，算术路经数值守卫后下沉
    // rt_num_arith（结果自带 tag 0/1）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::Value* is_str = builder_.getInt1(false);
    for (const CGValue* v : {&lhs, &rhs}) {
        if (v->type != CGType::Dyn) {
            continue;
        }
        llvm::Value* eq = builder_.CreateICmpEQ(num_tag(v->value),
                                                builder_.getInt64(3),
                                                "dynplusisstr");
        is_str = builder_.CreateOr(is_str, eq, "dynplusstr");
    }
    auto* str_bb = llvm::BasicBlock::Create(context_, "dynplus.str", fn);
    auto* num_bb = llvm::BasicBlock::Create(context_, "dynplus.num", fn);
    auto* cont_bb = llvm::BasicBlock::Create(context_, "dynplus.cont", fn);
    builder_.CreateCondBr(is_str, str_bb, num_bb);

    builder_.SetInsertPoint(str_bb);
    llvm::Value* cat = builder_.CreateCall(
        rt_concat_, {to_str(lhs, op), to_str(rhs, op)}, "concattmp");
    llvm::Value* boxed = make_num(
        builder_.getInt64(3),
        builder_.CreatePtrToInt(cat, builder_.getInt64Ty(), "concatbits"));
    llvm::BasicBlock* str_end = builder_.GetInsertBlock();
    builder_.CreateBr(cont_bb);

    builder_.SetInsertPoint(num_bb);
    // 守卫求值顺序固定（两侧陷阱块嵌套在同一区，IR 位置依赖插入点）
    llvm::Value* a = dyn_num_operand(lhs, op);
    llvm::Value* b = dyn_num_operand(rhs, op);
    llvm::Value* sum = call_num_arith(0, a, b);
    llvm::BasicBlock* num_end = builder_.GetInsertBlock();
    builder_.CreateBr(cont_bb);

    builder_.SetInsertPoint(cont_bb);
    llvm::PHINode* phi = builder_.CreatePHI(sum->getType(), 2, "dynplus");
    phi->addIncoming(boxed, str_end);
    phi->addIncoming(sum, num_end);
    return phi;
}

llvm::Value* CodeGenerator::dyn_cmp_box(const CGValue& v, const Token& op) {
    // 比较侧统一盒化为 {tag,bits}（t149，编码同数组 kind）：Dyn/Num 原样，
    // Int 打 tag 0、Double 打 tag 1（位模式），Str 打 tag 3 存串 ptr 位模式；
    // 其余侧由 visitBinary 的 cmp_side_ok 先行拒编，此处防御
    switch (v.type) {
        case CGType::Dyn:
        case CGType::Num:
            return v.value;
        case CGType::Int:
            return make_num(builder_.getInt64(0), v.value);
        case CGType::Double:
            return make_num(builder_.getInt64(1),
                            builder_.CreateBitCast(v.value, builder_.getInt64Ty(), "bits"));
        case CGType::Str:
            return make_num(builder_.getInt64(3),
                            builder_.CreatePtrToInt(v.value, builder_.getInt64Ty(), "strbits"));
        default:
            unsupported("comparison of dynamic value with this type",
                        op.line(), op.column());
    }
}

llvm::Value* CodeGenerator::to_num(const CGValue& v) {
    // integer/decimal → number 加宽保持原表示打 tag（对齐解释器
    // coerce_to_declared 的 KW_NUMBER 分支）；Num 原样返回
    switch (v.type) {
        case CGType::Num:    return v.value;
        case CGType::Int:    return make_num(builder_.getInt64(0), v.value);
        case CGType::Double:
            return make_num(builder_.getInt64(1),
                            builder_.CreateBitCast(v.value, builder_.getInt64Ty(), "bits"));
        default:             unsupported("conversion to 'number'", 0, 0);
    }
}

llvm::Value* CodeGenerator::to_dyn(const CGValue& v, size_t line, size_t column) {
    // 动态承载组装（t146）：Num/Dyn 原样透传（同 {i64,i64} 布局、tag 编码
    // 兼容）；Obj 打 tag 5 存实例 ptr 位模式（同 t144 kind7）；Arr 打 tag 4
    // 存内层数组 ptr 位模式（同 t160 kind7，与 t85 kind 4 单数组表示重合）；
    // Tup/TupDyn 打 tag 8 存动态 tuple 堆对象 ptr（t170，静态展开值先经
    // t140 物化）；Fn 打 tag 9 存函数 ptr 位模式（t171）；Void 打 tag 10、
    // bits 恒 0（t175）；其余标量按 arr_kind_of/elem_to_bits 编码
    switch (v.type) {
        case CGType::Num:
        case CGType::Dyn:
            return v.value;
        case CGType::Obj:
            return make_num(builder_.getInt64(5),
                            builder_.CreatePtrToInt(
                                v.value, builder_.getInt64Ty(), "dynobjbits"));
        case CGType::Tup:
            return make_num(
                builder_.getInt64(8),
                builder_.CreatePtrToInt(
                    materialize_tuple_dynamic(v.tup, line, column),
                    builder_.getInt64Ty(), "dyntupbits"));
        case CGType::TupDyn:
            return make_num(builder_.getInt64(8),
                            builder_.CreatePtrToInt(
                                v.value, builder_.getInt64Ty(), "dyntupbits"));
        case CGType::Fn:
            // 函数位（t171）：bits 即函数 ptr 位模式（与 t143 静态 Fn 承载同源），
            // 消费面只按 tag 打 `<function>` 与恒不等，无间接调用（语义层已拦
            // 非函数变量的调用，两端同报 Undefined function）
            return make_num(builder_.getInt64(9),
                            builder_.CreatePtrToInt(
                                v.value, builder_.getInt64Ty(), "dynfnbits"));
        case CGType::Void:
            // 空位（t175）：`none` 函数的返回值在解释器是 Value::Kind::None——可
            // 直存、可打印（to_string 打 "none"）、与自身判等（values_equal 的
            // None 分支恒 true）；LLVM 侧无对应值，故 bits 恒记 0，求值副作用已在
            // emit 该调用时发生（v.value 为 nullptr，不可参与任何 load/位转换）
            return make_num(builder_.getInt64(10), builder_.getInt64(0));
        default:
            return make_num(
                builder_.getInt64(
                    static_cast<uint64_t>(arr_kind_of(v.type))),
                elem_to_bits(v));
    }
}

llvm::Value* CodeGenerator::to_number_num(const CGValue& v, size_t line, size_t column) {
    // toNumber 降级（t63，对齐解释器 to_number_value）：number 透传与
    // 整数/小数加宽复用 to_num；bool → 0/1 整数表示；string 解析下沉
    // collie_rt（剥空白/严格 Infinity/纯整数精确/strtod 等价，失败返 NaN）
    switch (v.type) {
        case CGType::Num:
        case CGType::Int:
        case CGType::Double:
            return to_num(v);
        case CGType::Bool:
            return make_num(builder_.getInt64(0),
                            builder_.CreateZExt(v.value, builder_.getInt64Ty(), "bits"));
        case CGType::Str: {
            // 结果经出参写回（同 call_num_arith 的 ABI 规避），槽落 entry alloca
            llvm::AllocaInst* otag = create_entry_alloca(builder_.getInt64Ty(), "tonum.otag");
            llvm::AllocaInst* obits = create_entry_alloca(builder_.getInt64Ty(), "tonum.obits");
            builder_.CreateCall(rt_str_to_num_, {v.value, otag, obits});
            return make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag, "numtag"),
                            builder_.CreateLoad(builder_.getInt64Ty(), obits, "numbits"));
        }
        case CGType::Dyn:
            // 动态承载值（t155）：tag 运行期才定，下沉 rt 按 tag 复刻解释器
            // to_number_value 接受表（内建形式 mode 0）
            return dyn_to_number(v, false);
        default:
            unsupported("toNumber() of this value type", line, column);
    }
}

llvm::Value* CodeGenerator::dyn_to_number(const CGValue& v, bool method_form) {
    // 动态承载值（Dyn）的 toNumber（t155）：0/1 数值位透传、2 bool→整数 0/1、
    // 3 串复用 collie_rt_str_to_num 解析，其余位运行期报错（方法形式的实例位
    // 走解释器方法查找失败分支，其余与内建形式同报 toNumber() cannot convert
    // <kind>）；结果经 entry alloca 出参写回 {tag,bits}，静态类型恒 Num
    //（消费面全走既有 Num 臂，无需 Dyn 拆框）
    llvm::AllocaInst* otag = create_entry_alloca(builder_.getInt64Ty(), "dynto.otag");
    llvm::AllocaInst* obits = create_entry_alloca(builder_.getInt64Ty(), "dynto.obits");
    builder_.CreateCall(rt_dyn_to_number_,
                        {num_tag(v.value), num_bits(v.value), otag, obits,
                         builder_.getInt64(method_form ? 1 : 0)});
    return make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag, "dyntotag"),
                    builder_.CreateLoad(builder_.getInt64Ty(), obits, "dyntobits"));
}

void CodeGenerator::gen_tribool_method(llvm::Value* tri,
                                       const std::string& name) {
    // tribool 三态判定方法（t65，对齐解释器 call_tribool_method）：与对应三态
    // 常量 icmp 出 bool；静态 tribool（i8）与动态承载值 tag 6 的 bits 同编码，
    // 故两路共用（t157）
    const uint8_t code = name == "isTrue" ? 2 : name == "isFalse" ? 0 : 1;
    last_value_ = {builder_.CreateICmpEQ(tri, builder_.getInt8(code), "tritest"),
                   CGType::Bool};
}

void CodeGenerator::gen_number_method(const CGValue& object, const std::string& name,
                                      size_t line, size_t column) {
    // number 专属方法降级（t67，对齐解释器 call_number_method 双路分发）：
    // 整数路径纯 IR（恒有限、恒非 NaN/Infinity；abs 对 INT64_MIN 走溢出
    // 陷阱报错退出——解释器 BigInt 可精确表示，i64 不可，拒错编从陷阱）；
    // 小数路径 fabs/trunc/floor intrinsic + fcmp（isNaN 用 uno 自反比较，
    // isFinite/isInfinity 用 |a| 与 +inf 有序比较，NaN 天然 false）
    (void)line;
    (void)column;
    const bool numeric_result =
        name == "abs" || name == "integerPart" || name == "decimalPart";

    // 整数路径：i64 → 数值结果 i64（abs 后插入点可能落在 ovf.cont 新块）
    auto int_result = [&](llvm::Value* n) -> llvm::Value* {
        if (name == "abs") {
            // 同一元负号降级：checked 0-n，n 非负时 select 取原值
            llvm::Value* neg = checked_int_arith(llvm::Intrinsic::ssub_with_overflow,
                                                 builder_.getInt64(0), n, "negtmp");
            llvm::Value* is_neg =
                builder_.CreateICmpSLT(n, builder_.getInt64(0), "isneg");
            return builder_.CreateSelect(is_neg, neg, n, "abstmp");
        }
        if (name == "integerPart") {
            return n;
        }
        return builder_.getInt64(0); // decimalPart：整数无小数部分
    };
    auto int_pred = [&](llvm::Value* n) -> llvm::Value* {
        if (name == "isPositive") {
            return builder_.CreateICmpSGT(n, builder_.getInt64(0), "ispos");
        }
        if (name == "isNegative") {
            return builder_.CreateICmpSLT(n, builder_.getInt64(0), "isneg");
        }
        // isInteger/isFinite 恒真；isDecimal/isNaN/isInfinity 恒假
        return builder_.getInt1(name == "isInteger" || name == "isFinite");
    };

    // 小数路径：double → 数值结果 double / 谓词 i1
    auto dbl_result = [&](llvm::Value* a) -> llvm::Value* {
        if (name == "abs") {
            return builder_.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, a, nullptr,
                                                 "abstmp");
        }
        llvm::Value* tr = builder_.CreateUnaryIntrinsic(llvm::Intrinsic::trunc, a,
                                                        nullptr, "trunctmp");
        if (name == "integerPart") {
            return tr; // 向零取整：-123.456 → -123
        }
        return builder_.CreateFSub(a, tr, "fractmp"); // decimalPart 保留符号
    };
    auto dbl_pred = [&](llvm::Value* a) -> llvm::Value* {
        llvm::Value* zero = llvm::ConstantFP::get(builder_.getDoubleTy(), 0.0);
        if (name == "isNaN") {
            return builder_.CreateFCmpUNO(a, a, "isnan");
        }
        if (name == "isPositive") {
            return builder_.CreateFCmpOGT(a, zero, "ispos"); // NaN 与 0 均 false
        }
        if (name == "isNegative") {
            return builder_.CreateFCmpOLT(a, zero, "isneg");
        }
        llvm::Value* inf = llvm::ConstantFP::getInfinity(builder_.getDoubleTy());
        llvm::Value* mag = builder_.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, a,
                                                         nullptr, "magtmp");
        if (name == "isInfinity") {
            return builder_.CreateFCmpOEQ(mag, inf, "isinf");
        }
        llvm::Value* finite = builder_.CreateFCmpOLT(mag, inf, "isfin");
        if (name == "isFinite") {
            return finite;
        }
        // isInteger/isDecimal：文档规定 Infinity/NaN 均 false，先并有限性
        llvm::Value* fl = builder_.CreateUnaryIntrinsic(llvm::Intrinsic::floor, a,
                                                        nullptr, "floortmp");
        llvm::Value* eq = name == "isInteger"
                              ? builder_.CreateFCmpOEQ(a, fl, "inteq")
                              : builder_.CreateFCmpONE(a, fl, "intne");
        return builder_.CreateAnd(finite, eq, "andtmp");
    };

    if (object.type == CGType::Int) {
        last_value_ = numeric_result ? CGValue{int_result(object.value), CGType::Int}
                                     : CGValue{int_pred(object.value), CGType::Bool};
        return;
    }
    if (object.type == CGType::Double) {
        last_value_ = numeric_result
                          ? CGValue{dbl_result(object.value), CGType::Double}
                          : CGValue{dbl_pred(object.value), CGType::Bool};
        return;
    }
    // Num 接收者：tag 分支两路 + PHI 合并（整数态保持整数态打 tag 0，
    // 对齐解释器 is_integer_value 先行的 BigInt/double 双路分发）
    llvm::Value* tag = num_tag(object.value);
    llvm::Value* bits = num_bits(object.value);
    llvm::Value* is_int = builder_.CreateICmpEQ(tag, builder_.getInt64(0), "numisint");
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* int_bb = llvm::BasicBlock::Create(context_, "nummeth.int", fn);
    auto* dbl_bb = llvm::BasicBlock::Create(context_, "nummeth.dbl", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, "nummeth.merge", fn);
    builder_.CreateCondBr(is_int, int_bb, dbl_bb);

    builder_.SetInsertPoint(int_bb);
    llvm::Value* iv = numeric_result ? make_num(builder_.getInt64(0), int_result(bits))
                                     : int_pred(bits);
    llvm::BasicBlock* int_end = builder_.GetInsertBlock();
    builder_.CreateBr(merge_bb);

    builder_.SetInsertPoint(dbl_bb);
    llvm::Value* a = builder_.CreateBitCast(bits, builder_.getDoubleTy(), "numf64");
    llvm::Value* dv;
    if (numeric_result) {
        llvm::Value* r = dbl_result(a);
        dv = make_num(builder_.getInt64(1),
                      builder_.CreateBitCast(r, builder_.getInt64Ty(), "bits"));
    } else {
        dv = dbl_pred(a);
    }
    llvm::BasicBlock* dbl_end = builder_.GetInsertBlock();
    builder_.CreateBr(merge_bb);

    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(
        numeric_result ? llvm_type_of(CGType::Num)
                       : static_cast<llvm::Type*>(builder_.getInt1Ty()),
        2, "nummeth");
    phi->addIncoming(iv, int_end);
    phi->addIncoming(dv, dbl_end);
    last_value_ = {phi, numeric_result ? CGType::Num : CGType::Bool};
}

llvm::Value* CodeGenerator::call_num_arith(int op, llvm::Value* a, llvm::Value* b) {
    // 结果经出参写回（16 字节 struct 返回在 Win x64 走隐藏指针，出参避开
    // ABI 错配）；出参槽落 entry alloca（IR 规范位置，利于 mem2reg）
    llvm::AllocaInst* otag = create_entry_alloca(builder_.getInt64Ty(), "num.otag");
    llvm::AllocaInst* obits = create_entry_alloca(builder_.getInt64Ty(), "num.obits");
    builder_.CreateCall(rt_num_arith_,
                        {builder_.getInt64(static_cast<uint64_t>(op)),
                         num_tag(a), num_bits(a), num_tag(b), num_bits(b),
                         otag, obits});
    return make_num(builder_.CreateLoad(builder_.getInt64Ty(), otag, "numtag"),
                    builder_.CreateLoad(builder_.getInt64Ty(), obits, "numbits"));
}

llvm::Value* CodeGenerator::to_str(const CGValue& v, const Token& where) {
    // 对齐解释器 Value::to_string：整数 %lld、小数四步格式、bool true/false（垫片实现）
    switch (v.type) {
        case CGType::Str:
            return v.value;
        case CGType::Int:
            return builder_.CreateCall(rt_i64_to_str_, {v.value}, "i64str");
        case CGType::Double:
            return builder_.CreateCall(rt_f64_to_str_, {v.value}, "f64str");
        case CGType::Num:
            // number 转串（t62）：垫片按 tag 分派，对齐 Value::to_string
            return builder_.CreateCall(
                rt_num_to_str_, {num_tag(v.value), num_bits(v.value)}, "numstr");
        case CGType::Dyn:
            // 动态标量转串（t138）：与打印同一垫片 tag 分派（toString/插值共用）
            return builder_.CreateCall(
                rt_num_to_str_, {num_tag(v.value), num_bits(v.value)}, "dynstr");
        case CGType::Bool: {
            // i1 → i32（C 接口边界），垫片返静态串
            llvm::Value* ext = builder_.CreateZExt(v.value, builder_.getInt32Ty());
            return builder_.CreateCall(rt_bool_to_str_, {ext}, "boolstr");
        }
        case CGType::Tri: {
            // tribool 转串（t65）：双 select 三常量串（零新增垫片接口，
            // 对齐 Value::to_string 的 "true"/"false"/"unset"）
            llvm::Value* is_true =
                builder_.CreateICmpEQ(v.value, builder_.getInt8(2), "istrue");
            llvm::Value* is_false =
                builder_.CreateICmpEQ(v.value, builder_.getInt8(0), "isfalse");
            llvm::Value* fu = builder_.CreateSelect(
                is_false, builder_.CreateGlobalString("false"),
                builder_.CreateGlobalString("unset"), "tristr");
            return builder_.CreateSelect(
                is_true, builder_.CreateGlobalString("true"), fu, "tristr");
        }
        case CGType::Arr:
            // [1, 2, 3] 格式（对齐 Value::to_string 的 Array 分支，t59）
            return builder_.CreateCall(rt_arr_to_str_, {v.value}, "arrstr");
        case CGType::Obj:
            // 实例转串固定 "<object>"（对齐 Value::to_string Instance 分支，t60）
            return builder_.CreateGlobalString("<object>");
        case CGType::Fn:
            // 函数值转串固定 "<function>"（t143，对齐 Value::to_string Function
            // 分支；toString/插值/"+" 拼接共用）
            return builder_.CreateGlobalString("<function>");
        case CGType::Tup:
            // tuple 转串（t68）：静态展开拼接（拉通 toString/插值/'+' 拼接）
            return tuple_to_str(v, where);
        case CGType::TupDyn:
            // 动态 tuple 转串（t140）：垫片整对象转 (1, 2)/(name: v) 格式
            return builder_.CreateCall(rt_tuple_to_str_, {v.value}, "tupdynstr");
        case CGType::Void:
            // none 转串（t81）："none" 常量串（对齐 Value::to_string None 分支，
            // 覆盖 toString(none) 与插值脱糖；none 拼接语义层已拦截）
            return builder_.CreateGlobalString("none");
        default:
            unsupported("string conversion of this value", where.line(), where.column());
    }
}

llvm::Value* CodeGenerator::elem_to_bits(const CGValue& v) {
    // 数组槽统一 i64 位模式（t59）：bitcast/zext/ptrtoint 均无损，bits_to_elem 逆转
    switch (v.type) {
        case CGType::Int:    return v.value;
        case CGType::Double: return builder_.CreateBitCast(v.value, builder_.getInt64Ty(), "bits");
        case CGType::Bool:   return builder_.CreateZExt(v.value, builder_.getInt64Ty(), "bits");
        case CGType::Str:    return builder_.CreatePtrToInt(v.value, builder_.getInt64Ty(), "bits");
        case CGType::Arr:    return builder_.CreatePtrToInt(v.value, builder_.getInt64Ty(), "bits");
        case CGType::Obj:    return builder_.CreatePtrToInt(v.value, builder_.getInt64Ty(), "bits"); // 实例数组：槽存实例 ptr 位模式（t100）
        case CGType::Tri:    return builder_.CreateZExt(v.value, builder_.getInt64Ty(), "bits"); // 三态 i8→i64（t136，0/1/2 直存）
        default:             unsupported("array element type", 0, 0);
    }
}

std::pair<llvm::Value*, llvm::Value*> CodeGenerator::materialize_dyn_scalars(
    const CGTuple& t, size_t line, size_t column) {
    // 异质 tuple Dyn 物化（t138）：tags+bits 双 int 数组（均 kind 0），tag 复用
    // 数组 kind 编码 0=int/1=dec/2=bool/3=str/6=tribool（Num 元素取运行期 tag
    // 0/1，两分支落点同编码天然兼容）；bits 编码与 elem_to_bits 同规则。
    // 调用侧同一索引/键两次取回（首次即负索引归一化/越界/未命中陷阱）
    const long long n = static_cast<long long>(t.elems.size());
    llvm::Value* tags = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(0)},
        "dyntags");
    llvm::Value* bits = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(0)},
        "dynbits");
    for (long long i = 0; i < n; ++i) {
        const CGValue& e = t.elems[static_cast<size_t>(i)];
        llvm::Value* tag = nullptr;
        llvm::Value* elem_bits = nullptr;
        if (e.type == CGType::Num || e.type == CGType::Dyn) {
            // Num 取运行期 tag 0/1；Dyn 透传自身 tag/bits（同 {i64,i64} 布局，
            // tag 编码兼容，t138 元组套元组动态访问）
            tag = num_tag(e.value);
            elem_bits = num_bits(e.value);
        } else if (e.type == CGType::Fn || e.type == CGType::Tup ||
                   e.type == CGType::TupDyn) {
            // 函数位（t171）tag 9 / tuple 位（t172）tag 8：单点走 to_dyn（静态
            // 展开 tuple 先经 t140 物化为堆对象），异质 tuple 动态索引/取键的
            // 读出即对应 Dyn 位；Arr/Obj 位走下方 else（arr_kind_of 4/5 +
            // elem_to_bits ptrToInt，与 to_dyn 的 tag 4/5 编码逐位相同）
            llvm::Value* vd = to_dyn(e, line, column);
            tag = num_tag(vd);
            elem_bits = num_bits(vd);
        } else {
            tag = builder_.getInt64(static_cast<uint64_t>(arr_kind_of(e.type)));
            elem_bits = elem_to_bits(e);
        }
        builder_.CreateCall(
            rt_arr_set_,
            {tags, builder_.getInt64(static_cast<uint64_t>(i)), tag});
        builder_.CreateCall(
            rt_arr_set_,
            {bits, builder_.getInt64(static_cast<uint64_t>(i)), elem_bits});
    }
    return {tags, bits};
}

llvm::Value* CodeGenerator::bits_to_elem(llvm::Value* bits, CGType elem) {
    switch (elem) {
        case CGType::Int:    return bits;
        case CGType::Double: return builder_.CreateBitCast(bits, builder_.getDoubleTy(), "elemtmp");
        case CGType::Bool:   return builder_.CreateTrunc(bits, builder_.getInt1Ty(), "elemtmp");
        case CGType::Str:
        case CGType::Arr:
        case CGType::Obj:    // 实例数组读出（t100）：位模式还原实例 ptr，cls 由调用点传播
            return builder_.CreateIntToPtr(bits, llvm::PointerType::getUnqual(context_), "elemtmp");
        case CGType::Tri:    // 三态还原 i64→i8（t136）：槽恒 0/1/2，截断无损
            return builder_.CreateTrunc(bits, builder_.getInt8Ty(), "elemtmp");
        default:             unsupported("array element type", 0, 0);
    }
}

std::string CodeGenerator::tuple_obj_merged_cls(const CGTuple& t) {
    // 同质实例位 tuple 的统一静态 cls（t114）：逐元素取最近公共祖先，跨树无
    // 共同祖先返回空串（调用侧回退 Dyn 承载，t172）
    std::string cls = t.elems.front().cls;
    for (size_t i = 1; i < t.elems.size(); ++i) {
        if (t.elems[i].cls != cls) {
            cls = nearest_common_ancestor(cls, t.elems[i].cls);
            if (cls.empty()) {
                return {};
            }
        }
    }
    return cls;
}

CodeGenerator::CGValue CodeGenerator::tuple_dynamic_result(
    llvm::Value* bits, CGType elem, const CGTuple& t, size_t line, size_t column) {
    // t114：同质 Arr/Obj 元素 tuple 非常量索引/get 结果回填。单数组物化
    // 路径只给出 {ptr, elem}，Arr 结果需内层 elem、Obj 结果需 cls 才能供下游
    // 索引/字段/方法调用解码。元素均为静态展开值，编译期扫描各内层
    // 元数据即可得统一型（与 t100 数组/t93 合流同规则）
    CGValue r{bits_to_elem(bits, elem), elem};
    if (elem == CGType::Arr) {
        CGType inner = t.elems.front().elem;
        for (const auto& e : t.elems) {
            if (e.elem != inner) {
                inner = CGType::Num; // 内层 elem 不一致降动态域哨兵（t94）
                break;
            }
        }
        r.elem = inner;
    } else if (elem == CGType::Obj) {
        r.cls = tuple_obj_merged_cls(t); // 最近公共祖先（t93/t100）
        if (r.cls.empty()) {
            // 调用侧已按 tuple_obj_merged_cls 回退 Dyn 承载路径（t172），
            // 此处保留保守拒编以免空 cls 被下游按静态布局解码
            unsupported("non-constant tuple access on incompatible "
                        "instance elements",
                        line, column);
        }
    }
    return r;
}

llvm::Value* CodeGenerator::materialize_tuple_dynamic(int tup_idx, size_t line,
                                                      size_t column) {
    // 静态展开 tuple → 动态 tuple 堆对象（t140）：跨函数边界/不同形状合流用。
    // names 建 kind 3 字符串数组（无名槽存空串，供 get()/eq/转串按非空名匹配），
    // values 建 kind 7 动态数组（tag/bits 编码同 to_dyn 单点：标量按 arr_kind_of、
    // 数组位 tag 4、实例位 tag 5，t163 起与 object 承载族同表示）。
    // 元素含 Void 等非 dyn_carrier → 拒编不错编（解释器动态 tuple 可容，
    // 编译产物无统一承载表示）；嵌套 tuple 元素自 t170 起按 tag 8 入承载、
    // 函数元素自 t171 起按 tag 9 入承载
    const CGTuple t = tuple_values_[tup_idx];
    const long long n = static_cast<long long>(t.elems.size());
    llvm::Value* names = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(3)},
        "tupdn.names");
    llvm::Value* values = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(n)), builder_.getInt64(7)},
        "tupdn.values");
    for (long long i = 0; i < n; ++i) {
        const CGValue& e = t.elems[static_cast<size_t>(i)];
        if (!is_dyn_carrier(e.type)) {
            unsupported("dynamic tuple with unsupported element type", line,
                        column);
        }
        llvm::Value* nm = builder_.CreateGlobalString(
            i < static_cast<long long>(t.names.size()) ? t.names[i] : std::string());
        builder_.CreateCall(
            rt_arr_set_,
            {names, builder_.getInt64(static_cast<uint64_t>(i)),
             builder_.CreatePtrToInt(nm, builder_.getInt64Ty(), "tupdn.nmbits")});
        llvm::Value* boxed = to_dyn(e, line, column);
        builder_.CreateCall(rt_arr_set_dyn_,
                            {values, builder_.getInt64(static_cast<uint64_t>(i)),
                             num_tag(boxed), num_bits(boxed)});
    }
    return builder_.CreateCall(
        rt_tuple_new_,
        {builder_.getInt64(static_cast<uint64_t>(n)), names, values}, "tupdn");
}

bool CodeGenerator::tuple_arms_static_mergeable(const std::vector<int>& tups) {
    // 各支 tuple 可否走 t95 静态展开合流的判定（t140 同形 + t164 叶位可合流）：
    // 顶层与嵌套各层的元素数、名字表全支一致，且每个叶位的类型按 merge_tuple_arms
    // 的既有规则可合并（同为数值、Tri×Bool、同 Arr 异 elem 降动态域哨兵、同 Obj 异
    // cls 需最近公共祖先）——任一条不满足即回退动态合流（逐支物化 TupDyn），
    // 解释器这些分支本就可择一返回任意 tuple 值，静态展开无处承载异型叶位
    const auto is_numeric = [](CGType ty) {
        return ty == CGType::Int || ty == CGType::Double || ty == CGType::Num;
    };
    std::function<bool(const std::vector<int>&)> rec =
        [&](const std::vector<int>& idxs) -> bool {
        const CGTuple& first = tuple_values_[idxs.front()];
        for (size_t k = 1; k < idxs.size(); ++k) {
            const CGTuple& other = tuple_values_[idxs[k]];
            if (other.elems.size() != first.elems.size() || other.names != first.names) {
                return false;
            }
        }
        for (size_t j = 0; j < first.elems.size(); ++j) {
            const bool nested = first.elems[j].type == CGType::Tup;
            for (int idx : idxs) {
                if ((tuple_values_[idx].elems[j].type == CGType::Tup) != nested) {
                    return false;
                }
            }
            if (!nested) {
                // 叶位类型可合流判定，规则与 merge_tuple_arms 阶段一严格一致
                CGType merged = first.elems[j].type;
                std::string cls = first.elems[j].cls;
                for (int idx : idxs) {
                    const CGValue& v = tuple_values_[idx].elems[j];
                    if (v.type == merged) {
                        if (v.type == CGType::Obj && v.cls != cls &&
                            nearest_common_ancestor(cls, v.cls).empty()) {
                            return false;
                        }
                        continue;
                    }
                    if (is_numeric(merged) && is_numeric(v.type)) continue;
                    if ((merged == CGType::Tri && v.type == CGType::Bool) ||
                        (merged == CGType::Bool && v.type == CGType::Tri)) {
                        merged = CGType::Tri;
                        continue;
                    }
                    return false;
                }
                continue;
            }
            std::vector<int> subs;
            subs.reserve(idxs.size());
            for (int idx : idxs) {
                subs.push_back(tuple_values_[idx].elems[j].tup);
            }
            if (!rec(subs)) return false;
        }
        return true;
    };
    if (tups.empty()) return false;
    return rec(tups);
}

int CodeGenerator::arr_kind_of(CGType elem) {
    // 编码与 collie_rt_array.kind 约定一致（collie_rt.c 数组运行时段）
    switch (elem) {
        case CGType::Int:    return 0;
        case CGType::Double: return 1;
        case CGType::Bool:   return 2;
        case CGType::Str:    return 3;
        case CGType::Arr:    return 4; // 嵌套数组：槽存内层数组 ptr 位模式（t85）
        case CGType::Obj:    return 5; // 实例数组：槽存实例 ptr 位模式（t100）
        case CGType::Tri:    return 6; // tribool 数组：槽存三态 0/1/2（t136）
        case CGType::Dyn:    return 7; // 异质动态标量数组：{tag,bits} 步长 2（t139）
        default:             unsupported("array element type", 0, 0);
    }
}

bool CodeGenerator::is_dyn_scalar(CGType t) {
    // 异质数组 kind 7 的标量承载范围（t139）：基本标量 + 已动态化标量（Dyn，支持
    // 嵌套异质数组字面量二次物化透传）；Obj/Arr/Tup 各有专属表示，混入维持拒编
    // （Obj/Arr/Tup 由 is_dyn_carrier 在数组字面量与动态承载槽处另行放行）
    return t == CGType::Int || t == CGType::Double || t == CGType::Num ||
           t == CGType::Bool || t == CGType::Str || t == CGType::Tri ||
           t == CGType::Dyn;
}

bool CodeGenerator::is_dyn_carrier(CGType t) {
    // kind 7 动态承载全集（t160）：标量（t139）+ 实例位 tag 5（t144）+ 数组位
    // tag 4（t160，bits 即内层数组 ptr 位模式，镜像 kind 4 单数组表示）+ tuple 位
    // tag 8（t170，bits 即 t140 动态 tuple 堆对象 ptr，静态 Tup 经物化入承载）+
    // 函数位 tag 9（t171，bits 即函数 ptr 位模式，与 t143 静态 Fn 承载同源）+
    // 空位 tag 10（t175，`none` 函数返回的 Void——解释器 Value::Kind::None 是
    // 一种可直存/可打印/可判等的值，bits 无内容恒记 0）
    return is_dyn_scalar(t) || t == CGType::Obj || t == CGType::Arr ||
           t == CGType::Tup || t == CGType::TupDyn || t == CGType::Fn ||
           t == CGType::Void;
}

bool CodeGenerator::arms_all_dyn_carrier(const std::vector<CGValue>& values) {
    // 三目/`==?` 臂的动态合流准入（t168）：全部臂都在动态承载集内（标量/实例位/
    // 数组位/tuple 位（t170）/函数位（t171）/空位（t175））才可逐臂 to_dyn 打框
    // 合流为 Dyn
    for (const auto& v : values) {
        if (!is_dyn_carrier(v.type)) return false;
    }
    return true;
}

CodeGenerator::ArmMerge CodeGenerator::merge_static_arm_types(
    const std::vector<CGValue>& values, CGType& result_type, CGType& result_elem,
    std::string& result_cls) {
    // 臂类型静态合流（t93/t94/t65 规则，原 gen_ternary/gen_multi_match 内联循环）：
    // 同型直用（Arr elem / Obj cls 一致性校验）；数值混型任一 Num 统一 Num 否则
    // 提升 Double；tribool/bool 混型统一 tribool。失败只返回种类，消息与行号由
    // 调用方按其触点文案给出
    auto is_numeric = [](CGType t) {
        return t == CGType::Int || t == CGType::Double || t == CGType::Num;
    };
    for (size_t i = 1; i < values.size(); ++i) {
        const CGValue& v = values[i];
        if (v.type == result_type) {
            if (v.type == CGType::Arr && v.elem != result_elem) {
                // 同为数组但元素类型不同：统一 elem=Num 动态域哨兵（t94）——
                // 数组值同为不透明 ptr，PHI 无关 elem；kind 随数组对象运行期
                // 自带，print/len/== rt 侧全 kind 覆盖，索引读按运行期 kind
                // 产出 Dyn（t151 消除缺口 CG9）
                result_elem = CGType::Num;
            }
            if (v.type == CGType::Obj && v.cls != result_cls) {
                // 类不同：统一到最近公共祖先（t93）——Obj 的 LLVM 表示同为
                // 指针，PHI 无关 cls；t86 对象头类 id + 动态分派保证合流值
                // 按运行期真实类解析方法；无公共祖先交调用方回退或拒编
                const std::string nca = nearest_common_ancestor(result_cls, v.cls);
                if (nca.empty()) return ArmMerge::NoCommonAncestor;
                result_cls = nca;
            }
            continue;
        }
        if (is_numeric(result_type) && is_numeric(v.type)) {
            result_type = (result_type == CGType::Num || v.type == CGType::Num)
                              ? CGType::Num
                              : CGType::Double;
            continue;
        }
        if ((result_type == CGType::Tri && v.type == CGType::Bool) ||
            (result_type == CGType::Bool && v.type == CGType::Tri)) {
            result_type = CGType::Tri;
            continue;
        }
        return ArmMerge::Incompatible;
    }
    return ArmMerge::Ok;
}

// ---------------- tuple 静态展开（t68） ----------------

bool CodeGenerator::const_int_of(const Expr* e, long long& out) {
    // AST 层模式匹配：整数字面量或一元负号包字面量（t[-1] 的 AST 为
    // UnaryExpr('-')+LiteralExpr，若走 emit 会经溢出检查产非常量指令）；
    // 字面量分类与 visitLiteral 一致（含 .eEf 即小数；Infinity 含 'f' 天然落入）
    if (const auto* lit = dynamic_cast<const LiteralExpr*>(e)) {
        if (lit->token().type() != TokenType::LITERAL_NUMBER) {
            return false;
        }
        const std::string lexeme(lit->token().lexeme());
        const bool is_hex = lexeme.size() > 1 && lexeme[0] == '0' &&
                            (lexeme[1] == 'x' || lexeme[1] == 'X');
        if (!is_hex && lexeme.find_first_of(".eEf") != std::string::npos) {
            return false;
        }
        errno = 0;
        char* end = nullptr;
        const long long v = std::strtoll(lexeme.c_str(), &end, is_hex ? 16 : 10);
        if (errno == ERANGE || (end && *end != '\0')) {
            return false;
        }
        out = v;
        return true;
    }
    if (const auto* un = dynamic_cast<const UnaryExpr*>(e)) {
        long long inner = 0;
        if (un->op().type() != TokenType::OP_MINUS ||
            !const_int_of(un->operand(), inner)) {
            return false;
        }
        out = -inner; // inner ≥ 0（字面量无符号），取负不溢出
        return true;
    }
    return false;
}

int CodeGenerator::register_tuple(CGTuple t) {
    tuple_values_.push_back(std::move(t));
    return static_cast<int>(tuple_values_.size()) - 1;
}

int CodeGenerator::create_tuple_var(CGTuple t, const std::string& name) {
    // 逐元素独立槽（经 create_var_slot：顶层升 GlobalVariable，t76；
    // 否则 entry alloca）+ store 初始值；嵌套 tuple 元素递归建
    // 子槽组（本层槽仅记子条目下标）；参数按值：递归 push 注册表防引用失效
    CGTupleVar tv;
    tv.names = t.names;
    tv.slots.reserve(t.elems.size());
    for (size_t i = 0; i < t.elems.size(); ++i) {
        const CGValue& v = t.elems[i];
        const std::string slot_name = name + "." + std::to_string(i);
        CGVar var;
        var.type = v.type;
        var.elem = v.elem;
        var.cls = v.cls;
        if (v.type == CGType::Tup) {
            var.tup = create_tuple_var(tuple_values_[v.tup], slot_name);
        } else {
            var.slot = create_var_slot(llvm_type_of(v.type), slot_name);
            builder_.CreateStore(v.value, var.slot);
        }
        tv.slots.push_back(std::move(var));
    }
    tuple_vars_.push_back(std::move(tv));
    return static_cast<int>(tuple_vars_.size()) - 1;
}

CodeGenerator::CGValue CodeGenerator::load_tuple_var(int var_idx,
                                                     const std::string& name) {
    // 逐槽 load 重组展开值（嵌套递归）；注册表下标访问不留引用
    //（register_tuple 会扩容 tuple_values_）
    CGTuple t;
    t.names = tuple_vars_[var_idx].names;
    const size_t n = tuple_vars_[var_idx].slots.size();
    t.elems.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const CGVar var = tuple_vars_[var_idx].slots[i];
        const std::string slot_name = name + "." + std::to_string(i);
        if (var.type == CGType::Tup) {
            t.elems.push_back(load_tuple_var(var.tup, slot_name));
        } else {
            t.elems.push_back({builder_.CreateLoad(llvm_type_of(var.type),
                                                   var.slot, slot_name),
                               var.type, var.elem, var.cls});
        }
    }
    return {nullptr, CGType::Tup, CGType::Int, "", register_tuple(std::move(t))};
}

bool CodeGenerator::tuple_shape_matches(int var_idx, int val_idx) const {
    // 同形状判定（t117）：元素数+名字表逐层一致，嵌套 Tup 元素递归
    //（visitAssign Tup 分支换形状重建前的守卫；一致才走 store_tuple_var
    // 逐槽写，其内同名守卫保持防御）
    const CGTupleVar& tv = tuple_vars_[var_idx];
    const CGTuple& t = tuple_values_[val_idx];
    if (tv.slots.size() != t.elems.size() || tv.names != t.names) {
        return false;
    }
    for (size_t i = 0; i < t.elems.size(); ++i) {
        const CGVar& var = tv.slots[i];
        const CGValue& e = t.elems[i];
        if (var.type == CGType::Tup || e.type == CGType::Tup) {
            if (var.type != CGType::Tup || e.type != CGType::Tup) {
                return false;
            }
            if (!tuple_shape_matches(var.tup, e.tup)) {
                return false;
            }
        }
    }
    return true;
}

bool CodeGenerator::has_global_slot(int var_idx) const {
    // 槽组含全局槽判定（t117）：顶层槽组整体升 GlobalVariable（t76），
    // 递归查嵌套子槽组任一 GlobalVariable 即真
    for (const CGVar& var : tuple_vars_[var_idx].slots) {
        if (var.type == CGType::Tup) {
            if (has_global_slot(var.tup)) {
                return true;
            }
        } else if (var.slot != nullptr &&
                   llvm::isa<llvm::GlobalVariable>(var.slot)) {
            return true;
        }
    }
    return false;
}

int CodeGenerator::store_tuple_var(int var_idx, const CGValue& v,
                                   const Token& where) {
    // 同形状重赋值：元素数 + 名字表一致才逐槽写（形状不同拒编不错编，
    // 解释器元组动态换形状，静态解构槽无法承载）
    if (v.type != CGType::Tup) {
        unsupported("assigning non-tuple value to tuple variable '" +
                        std::string(where.lexeme()) + "'",
                    where.line(), where.column());
    }
    const CGTuple t = tuple_values_[v.tup]; // 按值：下方 register 会扩容注册表
    if (t.elems.size() != tuple_vars_[var_idx].slots.size() ||
        t.names != tuple_vars_[var_idx].names) {
        unsupported("assigning tuple of different shape to '" +
                        std::string(where.lexeme()) + "'",
                    where.line(), where.column());
    }
    CGTuple stored; // 存入后的展开值（标量元素可能经加宽）
    stored.names = t.names;
    stored.elems.reserve(t.elems.size());
    for (size_t i = 0; i < t.elems.size(); ++i) {
        const CGVar var = tuple_vars_[var_idx].slots[i];
        const CGValue& e = t.elems[i];
        if (var.type == CGType::Tup) {
            int sub = store_tuple_var(var.tup, e, where);
            stored.elems.push_back({nullptr, CGType::Tup, CGType::Int, "", sub});
            continue;
        }
        if (var.type == CGType::Arr) {
            // 数组元素指针拷贝（同 visitAssign Arr 分支）：异型元素拒编
            if (e.type != CGType::Arr || e.elem != var.elem) {
                unsupported("assigning tuple with incompatible array element to '" +
                                std::string(where.lexeme()) + "'",
                            where.line(), where.column());
            }
            builder_.CreateStore(e.value, var.slot);
            stored.elems.push_back(e);
            continue;
        }
        if (var.type == CGType::Obj) {
            // tuple Obj 元素同继承树互赋放行（t116，镜像标量 Obj L1151 与实例
            // 数组 t115 L1128）：同树内元素类沿链前缀布局兼容，t[i].field 按
            // 槽声明类 var.cls 偏移解码（load_tuple_var 恒用 var.cls）、方法按
            // 对象头类 id 动态分派；槽 var.cls 保持不变（不收敛 NCA），stored
            // 元数据也保留 var.cls 与槽解码一致。兄弟类/无关类（互不为子类）
            // 特有字段偏移错位可能错值，保守拒编不错编
            if (e.type != CGType::Obj ||
                (e.cls != var.cls &&
                 !is_subclass_of(e.cls, var.cls) &&
                 !is_subclass_of(var.cls, e.cls))) {
                unsupported("assigning tuple with incompatible instance element to '" +
                                std::string(where.lexeme()) + "'",
                            where.line(), where.column());
            }
            builder_.CreateStore(e.value, var.slot);
            stored.elems.push_back({e.value, CGType::Obj, e.elem, var.cls});
            continue;
        }
        // tuple 槽关 Dyn 拆槽（t147 范围外）：解释器 tuple 整值替换、元素无
        // 类型校验，Dyn 元素存进按初始形状建的静态槽运行期可能任意 kind，
        // 拆槽守卫会把解释器可运行面变陷阱，故维持拒编不错编
        llvm::Value* sv = coerce_for_slot(e, var.type, where, "", false);
        builder_.CreateStore(sv, var.slot);
        stored.elems.push_back({sv, var.type});
    }
    return register_tuple(std::move(stored));
}

llvm::Value* CodeGenerator::tuple_to_str(const CGValue& v, const Token& where) {
    // 静态展开拼接（对齐 Value::to_string Tuple 分支）：", " 分隔、命名
    // 前缀 "name: "、string 元素不加引号、空元组 "()"、嵌套递归（经
    // to_str 的 Tup case）；括号/分隔/名字等常量段编译期合并，动态段
    // rt_concat 链接（malloc 串不 free，同缺口 CG6）
    const CGTuple t = tuple_values_[v.tup]; // 按值：嵌套递归可能扩容注册表
    std::string pending = "(";
    llvm::Value* acc = nullptr;
    auto flush = [&]() {
        if (pending.empty()) {
            return;
        }
        llvm::Value* c = builder_.CreateGlobalString(pending);
        acc = acc ? builder_.CreateCall(rt_concat_, {acc, c}, "concattmp") : c;
        pending.clear();
    };
    for (size_t i = 0; i < t.elems.size(); ++i) {
        if (i != 0) {
            pending += ", ";
        }
        if (!t.names[i].empty()) {
            pending += t.names[i] + ": ";
        }
        flush();
        llvm::Value* s = to_str(t.elems[i], where);
        acc = acc ? builder_.CreateCall(rt_concat_, {acc, s}, "concattmp") : s;
    }
    pending += ")";
    flush();
    return acc;
}

} // namespace collie
