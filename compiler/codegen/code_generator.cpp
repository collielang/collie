/**
 * @file code_generator.cpp
 * @brief AST → LLVM IR 代码生成器实现（M6 t49–t52，S1–S5 子集）
 *
 * 降级规则见 compiler/codegen/README.md 第四节；语义依据 compiler/SPEC.md。
 */
#include "code_generator.h"

#include <cerrno>
#include <cstdlib>
#include <set>

#include <llvm/IR/CFG.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace collie {

namespace {

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

    // collie_rt 类实例分配声明（t60）：字段块 malloc，struct 布局读写全在 codegen 侧
    rt_obj_new_ = module_->getOrInsertFunction(
        "collie_rt_obj_new",
        llvm::FunctionType::get(ptr_ty, {builder_.getInt64Ty()}, false));

    // 第一遍（S5 t52 / t60）：顶层函数建原型、类注册 struct 布局与方法原型，
    // 递归与前向引用天然可用
    functions_.clear();
    classes_.clear();
    in_function_ = false;
    current_this_ = nullptr;
    current_class_name_.clear();
    for (const auto& stmt : statements) {
        if (const auto* class_stmt = dynamic_cast<const ClassStmt*>(stmt.get())) {
            register_class(*class_stmt);
        } else if (const auto* fn_stmt = dynamic_cast<const FunctionStmt*>(stmt.get())) {
            declare_function(*fn_stmt);
        }
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

    // 仅数值算术在范围内（比较/逻辑/位运算属 S3+）
    auto require_numeric = [&](const CGValue& v) {
        if (v.type != CGType::Int && v.type != CGType::Double) {
            unsupported("non-numeric operand of '" + std::string(op.lexeme()) + "'",
                        op.line(), op.column());
        }
    };

    switch (op.type()) {
        case TokenType::OP_DIVIDE: {
            // '/' 恒产小数（SPEC §4，Python 式 true division）；
            // 除零走 IEEE 754 得 ±Infinity/NaN（t33），fdiv 天然满足
            require_numeric(lhs);
            require_numeric(rhs);
            last_value_ = {builder_.CreateFDiv(to_double(lhs), to_double(rhs), "divtmp"),
                           CGType::Double};
            return;
        }
        case TokenType::OP_MODULO: {
            // '%' floor 取模（SPEC §4，结果符号与除数一致）：
            //   r = srem(a, b); r 非零且与 b 异号时 r += b（select 无分支实现）
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
            require_numeric(lhs);
            require_numeric(rhs);
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
            if (lhs.type == CGType::Bool && rhs.type == CGType::Bool &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                llvm::Value* v = t == TokenType::OP_EQUAL
                                     ? builder_.CreateICmpEQ(lhs.value, rhs.value, "cmptmp")
                                     : builder_.CreateICmpNE(lhs.value, rhs.value, "cmptmp");
                last_value_ = {v, CGType::Bool};
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
        default:
            unsupported("binary operator '" + std::string(op.lexeme()) + "'",
                        op.line(), op.column());
    }
}

void CodeGenerator::visitUnary(const UnaryExpr& expr) {
    const Token& op = expr.op();
    if (op.type() == TokenType::OP_NOT) {
        // 逻辑非（S3 t50）：仅 bool 域（tribool 属后续阶段）
        CGValue v = emit(expr.operand());
        if (v.type != CGType::Bool) {
            unsupported("'!' on non-bool operand", op.line(), op.column());
        }
        last_value_ = {builder_.CreateNot(v.value, "nottmp"), CGType::Bool};
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
        unsupported("len() of this value type",
                    expr.paren().line(), expr.paren().column());
    }
    // 用户自定义顶层函数（S5 t52）：查第一遍建好的原型表
    if (callee) {
        auto it = functions_.find(std::string(callee->name().lexeme()));
        if (it != functions_.end()) {
            const CGFunction& info = it->second;
            const auto& arguments = expr.arguments();
            if (arguments.size() != info.param_types.size()) {
                // 元数不匹配属重载选择（语义层支持但 codegen 仅单签名）
                unsupported("call arity mismatch (overloads not supported)",
                            expr.paren().line(), expr.paren().column());
            }
            std::vector<llvm::Value*> args;
            for (size_t i = 0; i < arguments.size(); ++i) {
                CGValue a = emit(arguments[i].get());
                // 实参按形参类型对齐：仅 integer→decimal 隐式提升（与解释器 coerce 一致）
                if (a.type != info.param_types[i]) {
                    if (info.param_types[i] == CGType::Double && a.type == CGType::Int) {
                        a = {to_double(a), CGType::Double};
                    } else {
                        unsupported("argument type mismatch",
                                    expr.paren().line(), expr.paren().column());
                    }
                }
                args.push_back(a.value);
            }
            llvm::CallInst* call = builder_.CreateCall(info.fn, args);
            last_value_ = (info.ret_type == CGType::Void)
                              ? CGValue{nullptr, CGType::Void}
                              : CGValue{call, info.ret_type};
            return;
        }
    }
    unsupported("function call", expr.paren().line(), expr.paren().column());
}

void CodeGenerator::gen_print(const CallExpr& expr) {
    // print(a, b, ...)：与解释器 call_builtin_print 对齐——空格分隔 + 末尾换行；
    // 逐参按 CGType 调对应 collie_rt 接口（垫片接管格式化，输出对齐解释器）
    const auto& arguments = expr.arguments();
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) builder_.CreateCall(rt_print_sep_, {}); // 参数间单个空格
        CGValue v = emit(arguments[i].get());
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
            case CGType::Bool: {
                // i1 零扩展为 i32（C 接口参数为 int）
                llvm::Value* as_i32 = builder_.CreateZExt(v.value, builder_.getInt32Ty());
                builder_.CreateCall(rt_print_bool_, {as_i32});
                break;
            }
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
            case CGType::Void:
                // none 返回函数的调用结果无值可打（解释器打 none，降级无对应表示）
                unsupported("print of 'none' value",
                            expr.paren().line(), expr.paren().column());
        }
    }
    builder_.CreateCall(rt_print_newline_, {}); // 一行结束换行
}

void CodeGenerator::gen_logical(const BinaryExpr& expr) {
    // 短路降级（与解释器对齐）：&& 左侧为 false / || 左侧为 true 时右侧不求值；
    // 仅 bool 域（tribool Kleene 逻辑属后续阶段）；phi 汇合两条边的值
    const Token& op = expr.op();
    const bool is_and = op.type() == TokenType::OP_AND;

    CGValue lhs = emit(expr.left());
    if (lhs.type != CGType::Bool) {
        unsupported("non-bool operand of '" + std::string(op.lexeme()) + "'",
                    op.line(), op.column());
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* rhs_bb = llvm::BasicBlock::Create(context_, is_and ? "and.rhs" : "or.rhs", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, is_and ? "and.end" : "or.end", fn);
    llvm::BasicBlock* lhs_end = builder_.GetInsertBlock();
    if (is_and) {
        builder_.CreateCondBr(lhs.value, rhs_bb, merge_bb); // false 短路
    } else {
        builder_.CreateCondBr(lhs.value, merge_bb, rhs_bb); // true 短路
    }

    builder_.SetInsertPoint(rhs_bb);
    CGValue rhs = emit(expr.right());
    if (rhs.type != CGType::Bool) {
        unsupported("non-bool operand of '" + std::string(op.lexeme()) + "'",
                    op.line(), op.column());
    }
    llvm::BasicBlock* rhs_end = builder_.GetInsertBlock(); // 右侧可能自带嵌套分支
    builder_.CreateBr(merge_bb);

    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(builder_.getInt1Ty(), 2,
                                            is_and ? "andtmp" : "ortmp");
    phi->addIncoming(lhs.value, lhs_end); // 短路边：左值即结果
    phi->addIncoming(rhs.value, rhs_end);
    last_value_ = {phi, CGType::Bool};
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
        unsupported("identifier '" + name + "'",
                    expr.name().line(), expr.name().column());
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
    CGValue v = emit(expr.value());
    if (var->type == CGType::Arr) {
        // 数组赋值即指针拷贝（引用语义对齐解释器，t59）；元素类型不同的数组
        // 拒编不错编（后续索引读写按变量记录的元素类型解码，错型会错值）
        if (v.type != CGType::Arr || v.elem != var->elem) {
            unsupported("assigning array with different element type to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        builder_.CreateStore(v.value, var->slot);
        last_value_ = {v.value, CGType::Arr, var->elem};
        return;
    }
    if (var->type == CGType::Obj) {
        // 实例赋值即指针拷贝（引用语义对齐解释器 shared_ptr，t60）；
        // 跨类赋值拒编不错编（字段布局不同，错类会错值）
        if (v.type != CGType::Obj || v.cls != var->cls) {
            unsupported("assigning incompatible value to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        builder_.CreateStore(v.value, var->slot);
        last_value_ = {v.value, CGType::Obj, CGType::Int, var->cls};
        return;
    }
    llvm::Value* stored = coerce_for_slot(v, var->type, expr.name());
    builder_.CreateStore(stored, var->slot);
    // 赋值表达式的值 = 存入后的值（与解释器一致）
    last_value_ = {stored, var->type};
}

void CodeGenerator::visitTuple(const TupleExpr&) { unsupported("tuple", 0, 0); }
void CodeGenerator::visitTernary(const TernaryExpr& expr) { gen_ternary(expr); }
void CodeGenerator::visitMultiMatch(const MultiMatchExpr&) { unsupported("'==?'", 0, 0); }
void CodeGenerator::visitArrayLiteral(const ArrayLiteralExpr& expr) {
    // 数组字面量（t59）：语义层不追踪元素类型（一刀切 KW_ARRAY），codegen 自行
    // 做同质推断——Int/Double 混合整体提升 Double（提升后输出与解释器一致：整值
    // double 按整数打印），其余混合与嵌套数组拒编不错编；空字面量元素类型记
    // Int（print 得 []，索引必越界报错，行为与解释器一致）
    const Token& bracket = expr.bracket();
    std::vector<CGValue> elements;
    elements.reserve(expr.elements().size());
    CGType elem = CGType::Int;
    for (const auto& element : expr.elements()) {
        CGValue v = emit(element.get());
        if (v.type == CGType::Arr) {
            unsupported("nested array literal", bracket.line(), bracket.column());
        }
        if (v.type == CGType::Void) {
            unsupported("array element without value", bracket.line(), bracket.column());
        }
        if (elements.empty()) {
            elem = v.type;
        } else if (v.type != elem) {
            if ((elem == CGType::Int || elem == CGType::Double) &&
                (v.type == CGType::Int || v.type == CGType::Double)) {
                elem = CGType::Double;
            } else {
                unsupported("heterogeneous array literal",
                            bracket.line(), bracket.column());
            }
        }
        elements.push_back(v);
    }
    llvm::Value* arr = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(elements.size())),
         builder_.getInt64(static_cast<uint64_t>(arr_kind_of(elem)))},
        "arrnew");
    for (size_t i = 0; i < elements.size(); ++i) {
        CGValue v = elements[i];
        if (elem == CGType::Double && v.type == CGType::Int) {
            v = {to_double(v), CGType::Double}; // 同质提升：整数元素升 double
        }
        builder_.CreateCall(rt_arr_set_,
                            {arr, builder_.getInt64(i), elem_to_bits(v)});
    }
    last_value_ = {arr, CGType::Arr, elem};
}
void CodeGenerator::visitIndex(const IndexExpr& expr) {
    // string 索引（S8 t56）/数组索引（t59）：负索引与越界报错在 collie_rt
    // 运行期（对齐解释器 normalize_index）；tuple 索引待对应类型 codegen 支持，
    // 非 Int 索引拒编不错编
    CGValue object = emit(expr.object());
    if (object.type != CGType::Str && object.type != CGType::Arr) {
        unsupported("indexing this value type",
                    expr.bracket().line(), expr.bracket().column());
    }
    CGValue index = emit(expr.index());
    if (index.type != CGType::Int) {
        unsupported("non-integer index",
                    expr.bracket().line(), expr.bracket().column());
    }
    if (object.type == CGType::Arr) {
        // 8 字节槽位模式按元素类型解码（字面量同质推断/变量槽记录，t59）
        llvm::Value* bits = builder_.CreateCall(
            rt_arr_get_, {object.value, index.value}, "arrget");
        last_value_ = {bits_to_elem(bits, object.elem), object.elem};
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
    if (object.type != CGType::Arr) {
        unsupported("index assignment on this value type",
                    expr.bracket().line(), expr.bracket().column());
    }
    CGValue index = emit(expr.index());
    if (index.type != CGType::Int) {
        unsupported("non-integer index",
                    expr.bracket().line(), expr.bracket().column());
    }
    CGValue v = emit(expr.value());
    if (v.type != object.elem) {
        if (object.elem == CGType::Double && v.type == CGType::Int) {
            v = {to_double(v), CGType::Double};
        } else {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
    }
    builder_.CreateCall(rt_arr_set_, {object.value, index.value, elem_to_bits(v)});
    last_value_ = v; // 赋值表达式的值为右侧值（与解释器一致，支持链式赋值）
}
void CodeGenerator::visitMethodCall(const MethodCallExpr& expr) {
    // string 方法（S10 t57）：trim 系列/subString 降级到 collie_rt；toString()
    // 方法形式对任意标量接收者复用 to_str（与内建 toString(x) 同一降级）；
    // toNumber（返动态 number）与 number/tribool/tuple 方法维持拒编不错编
    const std::string name(expr.name().lexeme());
    size_t line = expr.name().line();
    size_t column = expr.name().column();
    CGValue object = emit(expr.object());

    if (object.type == CGType::Obj) {
        // 类实例方法（t60）：查类方法表（单类无继承）调 this 隐藏首参函数；
        // 未命中时 toString 内建兜底返 "<object>"（对齐解释器分派顺序：
        // find_method 优先，类自定义 toString 覆盖内建）
        const CGClass& cls = classes_.at(object.cls);
        auto mit = cls.methods.find(name);
        if (mit != cls.methods.end()) {
            const CGFunction& info = mit->second;
            const auto& arguments = expr.arguments();
            if (arguments.size() != info.param_types.size()) {
                // 解释器运行期报错；codegen 静态可判，拒编不错编
                unsupported("method arity mismatch for '" + name + "'", line, column);
            }
            std::vector<llvm::Value*> args{object.value};
            for (size_t i = 0; i < arguments.size(); ++i) {
                CGValue a = emit(arguments[i].get());
                // 实参按形参类型对齐：仅 integer→decimal 提升（coerce 等价，t60）
                if (a.type != info.param_types[i]) {
                    if (info.param_types[i] == CGType::Double && a.type == CGType::Int) {
                        a = {to_double(a), CGType::Double};
                    } else {
                        unsupported("method argument type mismatch", line, column);
                    }
                }
                args.push_back(a.value);
            }
            llvm::CallInst* call = builder_.CreateCall(info.fn, args);
            last_value_ = (info.ret_type == CGType::Void)
                              ? CGValue{nullptr, CGType::Void}
                              : CGValue{call, info.ret_type};
            return;
        }
        if (name == "toString" && expr.arguments().empty()) {
            last_value_ = {to_str(object, expr.name()), CGType::Str};
            return;
        }
        unsupported("undefined method '" + name + "' on class '" + object.cls + "'",
                    line, column);
    }

    if (name == "toString" && expr.arguments().empty()) {
        last_value_ = {to_str(object, expr.name()), CGType::Str};
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
            // subString(start[, end])：参数限 Int（Double/NaN 特例拒编）；
            // 缺省 end 传 -1，运行时取 length（对齐解释器 end 缺省/-1 语义）
            const auto& arguments = expr.arguments();
            if (arguments.empty() || arguments.size() > 2) {
                unsupported("subString() with this arity", line, column);
            }
            CGValue start = emit(arguments[0].get());
            if (start.type != CGType::Int) {
                unsupported("non-integer subString() start", line, column);
            }
            llvm::Value* end = llvm::ConstantInt::getSigned(builder_.getInt64Ty(), -1);
            if (arguments.size() == 2) {
                CGValue end_value = emit(arguments[1].get());
                if (end_value.type != CGType::Int) {
                    unsupported("non-integer subString() end", line, column);
                }
                end = end_value.value;
            }
            last_value_ = {builder_.CreateCall(
                               rt_str_substring_, {object.value, start.value, end}, "substrtmp"),
                           CGType::Str};
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
    if (object.type == CGType::Obj) {
        // 类实例字段读（t60）：struct GEP + load，字段类型静态已知
        const CGClass& cls = classes_.at(object.cls);
        const std::string name(expr.name().lexeme());
        auto it = cls.field_index.find(name);
        if (it == cls.field_index.end()) {
            unsupported("undefined property '" + name + "' on class '" + object.cls + "'",
                        expr.name().line(), expr.name().column());
        }
        const CGField& field = cls.fields[it->second];
        llvm::Value* slot =
            builder_.CreateStructGEP(cls.type, object.value, it->second, name);
        last_value_ = {builder_.CreateLoad(llvm_type_of(field.type), slot, name),
                       field.type};
        return;
    }
    unsupported("property access", expr.name().line(), expr.name().column());
}
void CodeGenerator::visitPropertyAssign(const PropertyAssignExpr& expr) {
    // 字段赋值（t60）：求值顺序 object → value（对齐解释器 visitPropertyAssign）；
    // 按字段声明类型对齐（仅 integer→decimal 提升，与四处 coerce 等价）
    CGValue object = emit(expr.object());
    if (object.type != CGType::Obj) {
        unsupported("property assignment on this value type",
                    expr.name().line(), expr.name().column());
    }
    const CGClass& cls = classes_.at(object.cls);
    const std::string name(expr.name().lexeme());
    auto it = cls.field_index.find(name);
    if (it == cls.field_index.end()) {
        unsupported("undefined property '" + name + "' on class '" + object.cls + "'",
                    expr.name().line(), expr.name().column());
    }
    CGValue v = emit(expr.value());
    const CGField& field = cls.fields[it->second];
    llvm::Value* stored = coerce_for_slot(v, field.type, expr.name());
    builder_.CreateStore(
        stored, builder_.CreateStructGEP(cls.type, object.value, it->second, name));
    last_value_ = {stored, field.type}; // 赋值表达式的值 = 所赋的值（与解释器一致）
}
void CodeGenerator::visitNew(const NewExpr& expr) {
    // 类实例化（t60）：collie_rt_obj_new 分配字段块（8 字节 × 字段数上界：
    // struct 各字段 ≤ 8 字节、对齐 ≤ 8，任意目标布局下恒足够），再按声明顺序
    // 求值字段初始值写入 → 求值构造器实参 → 调构造器（三段顺序对齐解释器 visitNew）
    const std::string name(expr.class_name().lexeme());
    size_t line = expr.class_name().line();
    size_t column = expr.class_name().column();
    auto it = classes_.find(name);
    if (it == classes_.end()) {
        unsupported("'new' of unknown class '" + name + "'", line, column);
    }
    const CGClass& cls = it->second;
    const uint64_t size = cls.fields.empty() ? 8 : 8 * cls.fields.size();
    llvm::Value* obj =
        builder_.CreateCall(rt_obj_new_, {builder_.getInt64(size)}, "objnew");
    for (unsigned i = 0; i < cls.fields.size(); ++i) {
        const CGField& field = cls.fields[i];
        CGValue v = emit(field.decl->initializer());
        // 字段初始值按声明类型对齐（解释器 coerce_to_declared 等价静态检查）
        llvm::Value* stored = coerce_for_slot(v, field.type, field.decl->name());
        builder_.CreateStore(stored,
                             builder_.CreateStructGEP(cls.type, obj, i, field.name));
    }
    // 构造器实参在字段初始化之后求值（对齐解释器顺序）
    std::vector<CGValue> args;
    for (const auto& argument : expr.arguments()) {
        args.push_back(emit(argument.get()));
    }
    auto mit = cls.methods.find(name); // 构造器 = 与类名同名成员
    if (mit == cls.methods.end()) {
        if (!args.empty()) {
            // 解释器运行期报错；codegen 静态可判，拒编不错编
            unsupported("constructor arguments for class '" + name +
                            "' without constructor",
                        line, column);
        }
    } else {
        const CGFunction& ctor = mit->second;
        if (args.size() != ctor.param_types.size()) {
            unsupported("constructor arity mismatch for '" + name + "'", line, column);
        }
        std::vector<llvm::Value*> call_args{obj};
        for (size_t i = 0; i < args.size(); ++i) {
            CGValue a = args[i];
            // 实参按形参类型对齐：仅 integer→decimal 提升（coerce 等价）
            if (a.type != ctor.param_types[i]) {
                if (ctor.param_types[i] == CGType::Double && a.type == CGType::Int) {
                    a = {to_double(a), CGType::Double};
                } else {
                    unsupported("constructor argument type mismatch", line, column);
                }
            }
            call_args.push_back(a.value);
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
void CodeGenerator::visitBaseCall(const BaseCallExpr&) { unsupported("'base' call", 0, 0); }
void CodeGenerator::visitBaseMethodCall(const BaseMethodCallExpr&) { unsupported("'base' method call", 0, 0); }

void CodeGenerator::visitVarDecl(const VarDeclStmt& stmt) {
    // 无初始化时解释器绑 none（动态哨兵值），静态降级无对应表示，明确拒编
    if (!stmt.initializer()) {
        unsupported("variable declaration without initializer",
                    stmt.name().line(), stmt.name().column());
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
        if (init.type != CGType::Obj || init.cls != cls_name) {
            unsupported("initializing '" + cls_name + "' variable with incompatible value",
                        stmt.name().line(), stmt.name().column());
        }
        const std::string name(stmt.name().lexeme());
        llvm::AllocaInst* slot = create_entry_alloca(llvm_type_of(CGType::Obj), name);
        builder_.CreateStore(init.value, slot);
        scopes_.back()[name] = {slot, CGType::Obj, CGType::Int, cls_name};
        return;
    }
    CGType type = declared_cgtype(stmt.type());
    CGValue init = emit(stmt.initializer());
    CGType elem = CGType::Int;
    llvm::Value* stored = nullptr;
    if (type == CGType::Arr) {
        // array 槽存不透明 ptr（指针拷贝即引用语义）；声明无元素类型标注，
        // 元素类型取自初始值的同质推断结果（t59）
        if (init.type != CGType::Arr) {
            unsupported("initializing 'array' variable with non-array value",
                        stmt.name().line(), stmt.name().column());
        }
        stored = init.value;
        elem = init.elem;
    } else {
        stored = coerce_for_slot(init, type, stmt.name());
    }
    const std::string name(stmt.name().lexeme());
    llvm::AllocaInst* slot = create_entry_alloca(llvm_type_of(type), name);
    builder_.CreateStore(stored, slot);
    scopes_.back()[name] = {slot, type, elem}; // 同名直接遮蔽（重复声明由语义层拦截）
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

    builder_.SetInsertPoint(then_bb);
    stmt.then_branch()->accept(*this);
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(merge_bb);
    }
    if (else_bb) {
        builder_.SetInsertPoint(else_bb);
        stmt.else_branch()->accept(*this);
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(merge_bb);
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

    builder_.SetInsertPoint(cond_bb);
    CGValue cond = emit(stmt.condition());
    if (cond.type != CGType::Bool) {
        unsupported("non-bool 'while' condition",
                    stmt.while_token().line(), stmt.while_token().column());
    }
    builder_.CreateCondBr(cond.value, body_bb, end_bb);

    builder_.SetInsertPoint(body_bb);
    loops_.push_back({cond_bb, end_bb}); // break/continue 目标（S4 t51）
    stmt.body()->accept(*this);
    loops_.pop_back();
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(cond_bb);
    }
    builder_.SetInsertPoint(end_bb);
}

void CodeGenerator::visitFor(const ForStmt& stmt) {
    // for 的初始化变量作用域限循环内（与解释器 ScopeGuard 对齐）
    scopes_.emplace_back();
    if (stmt.initializer()) {
        stmt.initializer()->accept(*this);
    }
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
    stmt.body()->accept(*this);
    loops_.pop_back();
    if (!builder_.GetInsertBlock()->getTerminator()) {
        builder_.CreateBr(inc_bb ? inc_bb : cond_bb);
    }
    if (inc_bb) {
        builder_.SetInsertPoint(inc_bb);
        emit(stmt.increment()); // 值弃用
        builder_.CreateBr(cond_bb);
    }
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

    builder_.SetInsertPoint(body_bb);
    loops_.push_back({cond_bb, end_bb});
    stmt.body()->accept(*this);
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
    builder_.SetInsertPoint(end_bb);
}

void CodeGenerator::visitSwitch(const SwitchStmt&) { unsupported("'switch'", 0, 0); }

void CodeGenerator::visitFunction(const FunctionStmt& stmt) {
    // 函数体生成（第二遍，S5 t52）：原型已在第一遍建好
    const std::string name(stmt.name().lexeme());
    if (in_function_) {
        unsupported("nested function declaration",
                    stmt.name().line(), stmt.name().column());
    }
    auto it = functions_.find(name);
    if (it == functions_.end()) {
        // 非顶层位置的函数声明（块内等）未进原型表
        unsupported("function declaration outside top level",
                    stmt.name().line(), stmt.name().column());
    }
    const CGFunction& info = it->second;

    // 保存 @main（或外层）生成现场，函数体用独立的变量环境/循环栈；
    // 函数内仅参数与局部变量可见（顶层变量住 @main 栈槽，跨函数不可访问，
    // 引用外层变量会走 identifier 未找到拒编）
    llvm::BasicBlock* saved_bb = builder_.GetInsertBlock();
    auto saved_scopes = std::move(scopes_);
    auto saved_loops = std::move(loops_);
    scopes_.clear();
    scopes_.emplace_back();
    loops_.clear();
    in_function_ = true;
    current_ret_type_ = info.ret_type;

    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", info.fn));
    // 形参落栈槽（与局部变量同机制，可被赋值/遮蔽）
    size_t i = 0;
    for (auto& arg : info.fn->args()) {
        const Parameter& param = stmt.parameters()[i];
        const std::string pname(param.name.lexeme());
        arg.setName(pname);
        llvm::AllocaInst* slot =
            create_entry_alloca(llvm_type_of(info.param_types[i]), pname);
        builder_.CreateStore(&arg, slot);
        scopes_.back()[pname] = {slot, info.param_types[i]};
        ++i;
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

    in_function_ = false;
    current_ret_type_ = CGType::Void;
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
        // 返回值按声明返回类型对齐：仅 integer→decimal 提升（与解释器 t37 一致）
        if (v.type != current_ret_type_) {
            if (current_ret_type_ == CGType::Double && v.type == CGType::Int) {
                v = {to_double(v), CGType::Double};
            } else {
                unsupported("return type mismatch",
                            stmt.keyword().line(), stmt.keyword().column());
            }
        }
        builder_.CreateRet(v.value);
    }
    // return 后同块死代码仍需插入点（与 break/continue 同机制）
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "ret.dead", fn));
}

void CodeGenerator::visitClass(const ClassStmt& stmt) {
    // 方法体生成（第二遍，t60）：struct 布局与方法原型已在注册遍建好
    const std::string name(stmt.name().lexeme());
    if (in_function_) {
        unsupported("class declaration inside function",
                    stmt.name().line(), stmt.name().column());
    }
    auto it = classes_.find(name);
    if (it == classes_.end()) {
        // 非顶层位置的类声明（块内等）未进类表
        unsupported("class declaration outside top level",
                    stmt.name().line(), stmt.name().column());
    }
    for (const auto& member : stmt.members()) {
        if (const auto* method = dynamic_cast<const FunctionStmt*>(member.get())) {
            gen_method_body(it->second, *method);
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
        case TokenType::KW_ARRAY:   return CGType::Arr; // 元素类型由初始值推断（t59）
        case TokenType::KW_NUMBER:
            // number 需整数/小数双表示（缺口 CG5，见 codegen/README.md）
            unsupported("'number' variable (gap CG5: needs tagged int/decimal repr)",
                        type_token.line(), type_token.column());
        default:
            unsupported("variable type '" + std::string(type_token.lexeme()) + "'",
                        type_token.line(), type_token.column());
    }
}

llvm::Type* CodeGenerator::llvm_type_of(CGType type) {
    switch (type) {
        case CGType::Int:    return builder_.getInt64Ty();
        case CGType::Double: return builder_.getDoubleTy();
        case CGType::Bool:   return builder_.getInt1Ty();
        case CGType::Str:    return llvm::PointerType::getUnqual(context_);
        case CGType::Arr:    return llvm::PointerType::getUnqual(context_);
        case CGType::Obj:    return llvm::PointerType::getUnqual(context_);
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

llvm::Value* CodeGenerator::coerce_for_slot(const CGValue& v, CGType slot_type,
                                            const Token& where) {
    if (v.type == slot_type) {
        return v.value;
    }
    // 仅 integer → decimal 隐式提升（与语义层/解释器 coerce_to_declared 一致）
    if (slot_type == CGType::Double && v.type == CGType::Int) {
        return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
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

void CodeGenerator::declare_function(const FunctionStmt& stmt) {
    const std::string name(stmt.name().lexeme());
    if (functions_.count(name) != 0) {
        // 语义层支持同名重载，codegen 第一期仅单签名
        unsupported("function overloading for '" + name + "'",
                    stmt.name().line(), stmt.name().column());
    }
    // none 返回降级 void；其余返回/参数类型限 declared_cgtype 支持面
    const CGType ret = stmt.return_type().type() == TokenType::KW_NONE
                           ? CGType::Void
                           : declared_cgtype(stmt.return_type());
    if (ret == CGType::Arr) {
        // KW_ARRAY 无元素类型标注，签名处无法确定元素表示（t59 范围外）
        unsupported("array return type", stmt.name().line(), stmt.name().column());
    }
    std::vector<CGType> param_types;
    std::vector<llvm::Type*> llvm_params;
    for (const auto& param : stmt.parameters()) {
        CGType t = declared_cgtype(param.type);
        if (t == CGType::Arr) {
            // 同上：形参处无元素类型信息，拒编不错编
            unsupported("array parameter", stmt.name().line(), stmt.name().column());
        }
        param_types.push_back(t);
        llvm_params.push_back(llvm_type_of(t));
    }
    auto* fn_type = llvm::FunctionType::get(llvm_type_of(ret), llvm_params,
                                            /*isVarArg=*/false);
    // 符号名加 "collie." 前缀：用户标识符无 '.'，天然不与 main/printf 等 C 符号冲突
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                      "collie." + name, module_.get());
    functions_[name] = {fn, std::move(param_types), ret};
}

void CodeGenerator::register_class(const ClassStmt& stmt) {
    const std::string name(stmt.name().lexeme());
    if (stmt.has_superclass()) {
        // 继承需字段合并/方法覆写解析，t60 范围外拒编
        unsupported("class inheritance",
                    stmt.name().line(), stmt.name().column());
    }
    if (classes_.count(name) != 0) {
        unsupported("duplicate class '" + name + "'",
                    stmt.name().line(), stmt.name().column());
    }
    CGClass cls;
    cls.stmt = &stmt;

    // 字段按声明顺序布局（下标即 GEP 索引）；类型限 declared_cgtype 支持面
    std::vector<llvm::Type*> field_types;
    for (const auto& member : stmt.members()) {
        const auto* field = dynamic_cast<const VarDeclStmt*>(member.get());
        if (!field) continue;
        const std::string fname(field->name().lexeme());
        if (cls.field_index.count(fname) != 0) {
            unsupported("duplicate field '" + fname + "'",
                        field->name().line(), field->name().column());
        }
        if (!field->initializer()) {
            // 无初值字段解释器落 none，静态布局无对应表示（t60 范围外）
            unsupported("class field without initializer",
                        field->name().line(), field->name().column());
        }
        CGType ftype = declared_cgtype(field->type());
        if (ftype == CGType::Arr) {
            // 数组字段无处标注元素类型（同 t59 参数/返回值拒编理由）
            unsupported("array field",
                        field->name().line(), field->name().column());
        }
        cls.field_index[fname] = static_cast<unsigned>(cls.fields.size());
        cls.fields.push_back({fname, ftype, field});
        field_types.push_back(llvm_type_of(ftype));
    }
    cls.type = llvm::StructType::create(context_, field_types,
                                        "collie.class." + name);

    // 方法（含构造器，键 = 类名）降级为独立函数：this 作隐藏首参 ptr，
    // 符号名 collie.<类名>.<方法名>（用户标识符无 '.'，天然不冲突）
    for (const auto& member : stmt.members()) {
        const auto* method = dynamic_cast<const FunctionStmt*>(member.get());
        if (!method) continue;
        const std::string mname(method->name().lexeme());
        if (cls.methods.count(mname) != 0) {
            // 语义层支持方法重载，codegen 第一期仅单签名（同 declare_function）
            unsupported("method overloading for '" + mname + "'",
                        method->name().line(), method->name().column());
        }
        const CGType ret = method->return_type().type() == TokenType::KW_NONE
                               ? CGType::Void
                               : declared_cgtype(method->return_type());
        if (ret == CGType::Arr) {
            unsupported("array return type",
                        method->name().line(), method->name().column());
        }
        std::vector<CGType> param_types;
        std::vector<llvm::Type*> llvm_params;
        llvm_params.push_back(llvm::PointerType::getUnqual(context_)); // 隐藏 this
        for (const auto& param : method->parameters()) {
            CGType t = declared_cgtype(param.type);
            if (t == CGType::Arr) {
                unsupported("array parameter",
                            method->name().line(), method->name().column());
            }
            param_types.push_back(t);
            llvm_params.push_back(llvm_type_of(t));
        }
        auto* fn_type = llvm::FunctionType::get(llvm_type_of(ret), llvm_params,
                                                /*isVarArg=*/false);
        auto* fn = llvm::Function::Create(fn_type, llvm::Function::InternalLinkage,
                                          "collie." + name + "." + mname,
                                          module_.get());
        cls.methods[mname] = {fn, std::move(param_types), ret};
    }
    classes_[name] = std::move(cls);
}

void CodeGenerator::gen_method_body(const CGClass& cls, const FunctionStmt& stmt) {
    const std::string name(stmt.name().lexeme());
    const CGFunction& info = cls.methods.at(name);

    // 保存生成现场（同 visitFunction）：方法体用独立的变量环境/循环栈
    llvm::BasicBlock* saved_bb = builder_.GetInsertBlock();
    auto saved_scopes = std::move(scopes_);
    auto saved_loops = std::move(loops_);
    scopes_.clear();
    scopes_.emplace_back();
    loops_.clear();
    in_function_ = true;
    current_ret_type_ = info.ret_type;
    current_class_name_ = std::string(cls.stmt->name().lexeme());

    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", info.fn));
    // 首参为隐藏 this（直接持 SSA 值不落栈槽：this 不可被赋值）；
    // 其余形参落栈槽（与 visitFunction 同机制，注意下标偏移 1）
    size_t i = 0;
    for (auto& arg : info.fn->args()) {
        if (i == 0) {
            arg.setName("this");
            current_this_ = &arg;
        } else {
            const Parameter& param = stmt.parameters()[i - 1];
            const std::string pname(param.name.lexeme());
            arg.setName(pname);
            llvm::AllocaInst* slot =
                create_entry_alloca(llvm_type_of(info.param_types[i - 1]), pname);
            builder_.CreateStore(&arg, slot);
            scopes_.back()[pname] = {slot, info.param_types[i - 1]};
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
        if (info.ret_type == CGType::Void) {
            builder_.CreateRetVoid();
        } else if (!reachable_from_entry(tail)) {
            builder_.CreateUnreachable();
        } else {
            throw CodeGenError(
                "codegen: method '" + name + "' may reach end without return",
                stmt.name().line(), stmt.name().column());
        }
    }

    in_function_ = false;
    current_ret_type_ = CGType::Void;
    current_this_ = nullptr;
    current_class_name_.clear();
    scopes_ = std::move(saved_scopes);
    loops_ = std::move(saved_loops);
    builder_.SetInsertPoint(saved_bb);
}

void CodeGenerator::gen_ternary(const TernaryExpr& expr) {
    // 三分支 tribool 形式 a ? x : y : z 属 tribool 后续阶段，拒编
    if (expr.unset_expr() != nullptr) {
        unsupported("three-branch tribool ternary (needs tribool support)",
                    expr.question_token().line(), expr.question_token().column());
    }
    CGValue cond = emit(expr.condition());
    if (cond.type != CGType::Bool) {
        unsupported("non-bool ternary condition",
                    expr.question_token().line(), expr.question_token().column());
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* then_bb = llvm::BasicBlock::Create(context_, "tern.then", fn);
    auto* else_bb = llvm::BasicBlock::Create(context_, "tern.else", fn);
    auto* merge_bb = llvm::BasicBlock::Create(context_, "tern.end", fn);
    builder_.CreateCondBr(cond.value, then_bb, else_bb);

    builder_.SetInsertPoint(then_bb);
    CGValue tv = emit(expr.then_expr());
    llvm::BasicBlock* then_end = builder_.GetInsertBlock();

    builder_.SetInsertPoint(else_bb);
    CGValue ev = emit(expr.else_expr());
    llvm::BasicBlock* else_end = builder_.GetInsertBlock();

    // 两分支类型统一：同型直用；int/double 混型统一提升为 double（与算术混型一致）
    CGType result_type;
    if (tv.type == ev.type && tv.type != CGType::Void) {
        if (tv.type == CGType::Arr && tv.elem != ev.elem) {
            // 同为数组但元素类型不同：合并后无法单一解码，拒编不错编（t59）
            unsupported("ternary branches yield arrays of different element types",
                        expr.question_token().line(), expr.question_token().column());
        }
        if (tv.type == CGType::Obj && tv.cls != ev.cls) {
            // 同为实例但类不同：静态无公共类型可表，拒编不错编（t60）
            unsupported("ternary branches yield instances of different classes",
                        expr.question_token().line(), expr.question_token().column());
        }
        result_type = tv.type;
    } else if ((tv.type == CGType::Int || tv.type == CGType::Double) &&
               (ev.type == CGType::Int || ev.type == CGType::Double)) {
        result_type = CGType::Double;
    } else {
        unsupported("ternary branches have incompatible types",
                    expr.question_token().line(), expr.question_token().column());
    }

    // 各分支块尾把值对齐 result_type 后再跳 merge（提升指令须落在该分支块内）
    builder_.SetInsertPoint(then_end);
    llvm::Value* then_val = (result_type == CGType::Double) ? to_double(tv) : tv.value;
    builder_.CreateBr(merge_bb);
    builder_.SetInsertPoint(else_end);
    llvm::Value* else_val = (result_type == CGType::Double) ? to_double(ev) : ev.value;
    builder_.CreateBr(merge_bb);

    builder_.SetInsertPoint(merge_bb);
    llvm::PHINode* phi = builder_.CreatePHI(llvm_type_of(result_type), 2, "terntmp");
    phi->addIncoming(then_val, then_end);
    phi->addIncoming(else_val, else_end);
    last_value_ = {phi, result_type, tv.elem, tv.cls}; // elem/cls 仅 Arr/Obj 有意义（两分支已校验一致）
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

llvm::Value* CodeGenerator::to_double(const CGValue& v) {
    switch (v.type) {
        case CGType::Double: return v.value;
        case CGType::Int:    return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
        case CGType::Bool:   return builder_.CreateUIToFP(v.value, builder_.getDoubleTy());
        default:             unsupported("numeric conversion of non-numeric value", 0, 0);
    }
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
        case CGType::Bool: {
            // i1 → i32（C 接口边界），垫片返静态串
            llvm::Value* ext = builder_.CreateZExt(v.value, builder_.getInt32Ty());
            return builder_.CreateCall(rt_bool_to_str_, {ext}, "boolstr");
        }
        case CGType::Arr:
            // [1, 2, 3] 格式（对齐 Value::to_string 的 Array 分支，t59）
            return builder_.CreateCall(rt_arr_to_str_, {v.value}, "arrstr");
        case CGType::Obj:
            // 实例转串固定 "<object>"（对齐 Value::to_string Instance 分支，t60）
            return builder_.CreateGlobalString("<object>");
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
        default:             unsupported("array element type", 0, 0);
    }
}

llvm::Value* CodeGenerator::bits_to_elem(llvm::Value* bits, CGType elem) {
    switch (elem) {
        case CGType::Int:    return bits;
        case CGType::Double: return builder_.CreateBitCast(bits, builder_.getDoubleTy(), "elemtmp");
        case CGType::Bool:   return builder_.CreateTrunc(bits, builder_.getInt1Ty(), "elemtmp");
        case CGType::Str:
            return builder_.CreateIntToPtr(bits, llvm::PointerType::getUnqual(context_), "elemtmp");
        default:             unsupported("array element type", 0, 0);
    }
}

int CodeGenerator::arr_kind_of(CGType elem) {
    // 编码与 collie_rt_array.kind 约定一致（collie_rt.c 数组运行时段）
    switch (elem) {
        case CGType::Int:    return 0;
        case CGType::Double: return 1;
        case CGType::Bool:   return 2;
        case CGType::Str:    return 3;
        default:             unsupported("array element type", 0, 0);
    }
}

} // namespace collie
