/**
 * @file code_generator.h
 * @brief AST → LLVM IR 代码生成器（M6 t49–t52，S1–S5 子集）
 *
 * 设计文档：compiler/codegen/README.md（类型映射/降级映射/阶段范围）。
 * 支持面：print、字符串/整数/小数/布尔字面量、算术 + - * / %、一元负号；
 * S3（t50）：变量声明/赋值（integer/decimal/bool/string）、比较、
 * 短路 && || 与 !、if/else、while、块作用域遮蔽；
 * S4（t51）：for/do-while/break/continue、二元三元表达式 a ? x : y；
 * S5（t52）：顶层函数声明/调用/return/递归（两遍：先建原型再生成函数体）。
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
        Int,     // i64（integer / number 整数表示，妥协点 CG1）
        Double,  // double（decimal / number 小数表示）
        Bool,    // i1
        Str,     // ptr → 常量字符串
        Void,    // 无值（none 返回函数的调用结果，S5 t52）
    };

    struct CGValue {
        llvm::Value* value = nullptr;
        CGType type = CGType::Int;
    };

    /// @brief 变量存储槽（entry 块 alloca）+ 编译期类型（S3 t50）
    struct CGVar {
        llvm::AllocaInst* slot = nullptr;
        CGType type = CGType::Int;
    };

    /// @brief 循环上下文（S4 t51）：break/continue 跳转目标块
    /// continue_target：while/do-while 为条件块，for 为增量块（无增量则条件块）
    struct LoopContext {
        llvm::BasicBlock* continue_target = nullptr;
        llvm::BasicBlock* break_target = nullptr;
    };

    /// @brief 顶层函数信息（S5 t52）：两遍处理第一遍建原型登记于此
    struct CGFunction {
        llvm::Function* fn = nullptr;
        std::vector<CGType> param_types;
        CGType ret_type = CGType::Void; // Void 即 none 返回
    };

    /// @brief 求值一个表达式子树，返回其 IR 值（accept + 侧信道取回）
    CGValue emit(const Expr* expr);

    /// @brief print 内建：逐参调用 collie_rt 垫片接口（空格分隔 + 换行），输出对齐解释器
    void gen_print(const CallExpr& expr);

    /// @brief 逻辑 && / ||：短路求值（与解释器对齐），仅 bool 域（tribool 属后续）
    void gen_logical(const BinaryExpr& expr);

    /// @brief 声明类型 token → CGType（S3 支持 integer/decimal/bool/string，其余报缺口）
    CGType declared_cgtype(const Token& type_token);

    /// @brief CGType → 对应的 LLVM 存储类型
    llvm::Type* llvm_type_of(CGType type);

    /// @brief 在函数 entry 块创建 alloca（IR 规范位置，利于后续 mem2reg）
    llvm::AllocaInst* create_entry_alloca(llvm::Type* type, const std::string& name);

    /// @brief 待存值对齐槽类型：仅 integer→decimal 隐式提升（与语义层一致），其余不匹配报错
    llvm::Value* coerce_for_slot(const CGValue& v, CGType slot_type, const Token& where);

    /// @brief 由内向外逐层查找变量；未找到返回 nullptr（语义层已保证先声明，防御用）
    CGVar* lookup_var(const std::string& name);

    /// @brief 二元三元表达式 a ? x : y：bool 条件 + PHI 汇合（分支类型不同时 int→double 提升）
    void gen_ternary(const TernaryExpr& expr);

    /// @brief 顶层函数建原型（第一遍，S5 t52）：同名重载拒编；none 返回降 void
    void declare_function(const FunctionStmt& stmt);

    /// @brief 把 Int/Bool 值提升为 double（算术混型时用）
    llvm::Value* to_double(const CGValue& v);

    /// @brief i64 带溢出检查的加/减/乘（CG1 t58）：s*.with.overflow intrinsic，
    /// 溢出分支调 collie_rt 陷阱报错退出，插入点落在继续块后返回结果值
    llvm::Value* checked_int_arith(llvm::Intrinsic::ID id, llvm::Value* lhs,
                                   llvm::Value* rhs, const llvm::Twine& name);

    /// @brief 把任意标量值转为字符串 ptr（S7 t54：拼接/toString 用，对齐 Value::to_string）
    llvm::Value* to_str(const CGValue& v, const Token& where);

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
    CGValue last_value_;
    /// 作用域栈：块进出 push/pop，支持遮蔽（与解释器 Environment 对齐）
    std::vector<std::unordered_map<std::string, CGVar>> scopes_;
    /// 循环上下文栈：break/continue 查最内层跳转目标（S4 t51）
    std::vector<LoopContext> loops_;
    /// 顶层函数表（S5 t52）：名字 → 原型；第一遍填充，递归/前向调用天然可用
    std::unordered_map<std::string, CGFunction> functions_;
    /// 当前是否在生成函数体（顶层 return / 嵌套函数拒编用）
    bool in_function_ = false;
    /// 当前函数的返回类型（visitReturn 校验/提升用；Void 即 none）
    CGType current_ret_type_ = CGType::Void;
};

} // namespace collie
