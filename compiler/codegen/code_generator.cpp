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
    // collie_rt byte/word 范围与移位量陷阱声明（t69）：越界报错退出
    rt_trap_bit_range_ = module_->getOrInsertFunction(
        "collie_rt_trap_bit_range",
        llvm::FunctionType::get(
            builder_.getVoidTy(),
            {ptr_ty, builder_.getInt64Ty(), builder_.getInt64Ty()}, false));
    rt_trap_shift_count_ = module_->getOrInsertFunction(
        "collie_rt_trap_shift_count",
        llvm::FunctionType::get(builder_.getVoidTy(), false));
    // collie_rt 动态域元素 kind 陷阱声明（t88，缺口 CG9）：bool/str/嵌套数组
    // 经透传后索引读出元素静态类型不可定，陷阱退出不错值
    rt_trap_arr_kind_ = module_->getOrInsertFunction(
        "collie_rt_trap_arr_kind",
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
    classes_.clear();
    in_function_ = false;
    current_this_ = nullptr;
    current_class_name_.clear();
    for (const auto& stmt : statements) {
        // 阶段一：类布局（字段合并需父类先注册——要求父类声明在前，t61）
        if (const auto* class_stmt = dynamic_cast<const ClassStmt*>(stmt.get())) {
            register_class_layout(*class_stmt);
        }
    }
    for (const auto& stmt : statements) {
        // 阶段二：方法单态化原型（签名可引用任意已注册类，t61）
        if (const auto* class_stmt = dynamic_cast<const ClassStmt*>(stmt.get())) {
            register_class_methods(*class_stmt);
        }
    }
    for (const auto& stmt : statements) {
        // 阶段三：函数原型（参数/返回值可为类实例，需全部类先就位，t61）
        if (const auto* fn_stmt = dynamic_cast<const FunctionStmt*>(stmt.get())) {
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

    switch (op.type()) {
        case TokenType::OP_DIVIDE: {
            // '/' 恒产小数（SPEC §4，Python 式 true division）；
            // 除零走 IEEE 754 得 ±Infinity/NaN（t33），fdiv 天然满足
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
            // tuple 相等（t75）：任一侧 Tup 且 ==/!= 时静态展开深比较；
            // Tup × 非 Tup 恒 false（对齐解释器 values_equal kind 不等）；
            // tuple 关系比较落下方 require_numeric 拒编（解释器同样不支持）
            if ((lhs.type == CGType::Tup || rhs.type == CGType::Tup) &&
                (t == TokenType::OP_EQUAL || t == TokenType::OP_NOT_EQUAL)) {
                llvm::Value* eq = lhs.type == CGType::Tup && rhs.type == CGType::Tup
                                      ? gen_tuple_eq(lhs, rhs, op)
                                      : builder_.getInt1(false);
                last_value_ = {t == TokenType::OP_EQUAL
                                   ? eq
                                   : builder_.CreateNot(eq, "cmptmp"),
                               CGType::Bool};
                return;
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
            // 实例（Obj）相等（t82）：解释器 values_equal 无 Instance 分支落
            // default 恒 false（含同一实例 a==a），故 ==/!= 常量折叠——Obj×Obj
            // 与 Obj×非Obj（kind 不等，语义层通常更早拦截，此为双保险）均 false；
            // 关系比较落下方 require_numeric 拒编（解释器同样不支持）
            if ((lhs.type == CGType::Obj || rhs.type == CGType::Obj) &&
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
        unsupported("len() of this value type",
                    expr.paren().line(), expr.paren().column());
    }
    // toNumber 内建（t63）：结果恒为 number（tagged 双表示）；string 解析
    // 下沉 collie_rt，bool/integer/decimal/number 纯 IR 内联转 Num；
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
                // 元数不匹配属重载选择（语义层支持但 codegen 仅单签名）
                unsupported("call arity mismatch (overloads not supported)",
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
            case CGType::Tup:
                // tuple 打印（t68）：静态展开拼接后整串输出（格式对齐
                // Value::to_string Tuple 分支）
                builder_.CreateCall(rt_print_str_, {tuple_to_str(v, expr.paren())});
                break;
            case CGType::Void:
                // none 打印（t81）：常量串 "none"（对齐解释器 Value::to_string
                // None 分支；求值已于收集阶段发生，副作用保序）
                builder_.CreateCall(rt_print_str_,
                                    {builder_.CreateGlobalString("none")});
                break;
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
        unsupported("identifier '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    if (!var->fn_key.empty()) {
        // 嵌套函数绑定（t91）：函数值非一等公民，作值使用拒编不错编
        unsupported("function '" + name + "' used as a value",
                    expr.name().line(), expr.name().column());
    }
    if (var->uninit) {
        // 无初始化声明未经同块赋值（t92）：解释器此值为 none（分支/循环内
        // 赋值流不敏感放行但运行期可能未执行），槽值静态不可知，拒编不错编
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
        if (v.type != CGType::Arr) {
            unsupported("assigning non-array value to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        if (var->elem == CGType::Num) {
            // 动态 elem 槽（t70/t88）：任意元素数组透传（kind 随数组对象自带，
            // print/len/== rt 侧全 kind 覆盖；索引读 kind ≥ 2 落 CG9 陷阱）
        } else if (v.elem != var->elem) {
            // 含 Num 来源写静态槽：元素类型静态不可知，拒编
            unsupported("assigning array with different element type to '" + name + "'",
                        expr.name().line(), expr.name().column());
        } else if (var->elem == CGType::Obj && v.cls != var->cls) {
            // 实例数组同类约束（t100）：元素类不同则后续 arr[i].field/method()
            // 按旧类解码会错值，拒编不错编
            unsupported("assigning array with different element class to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        builder_.CreateStore(v.value, var->slot);
        if (var->uninit && scopes_.size() == var->decl_depth) {
            // 无初始化 array 变量同块赋值后清 uninit 放行后续读（t101，与
            // 通用路径 t92 一致；本分支原早退不经通用路径故此处补清）
            var->uninit = false;
        }
        last_value_ = {v.value, CGType::Arr, var->elem, var->cls};
        return;
    }
    if (var->type == CGType::Obj) {
        // 实例赋值即指针拷贝（引用语义对齐解释器 shared_ptr，t60）；
        // 同一继承树内 upcast/downcast 均放行（t86/t103，解释器不校验类，
        // 成员访问按对象头类 id 动态分派/陷阱不错值）；跨树拒编不错编
        if (v.type != CGType::Obj ||
            (v.cls != var->cls && !is_subclass_of(v.cls, var->cls) &&
             !is_subclass_of(var->cls, v.cls))) {
            unsupported("assigning incompatible value to '" + name + "'",
                        expr.name().line(), expr.name().column());
        }
        builder_.CreateStore(v.value, var->slot);
        if (var->uninit && scopes_.size() == var->decl_depth) {
            // 无初始化类类型变量同块赋值后清 uninit 放行后续读（t101，与
            // 通用路径 t92 一致；本分支原早退不经通用路径故此处补清）
            var->uninit = false;
        }
        last_value_ = {v.value, CGType::Obj, CGType::Int, var->cls};
        return;
    }
    if (var->type == CGType::Tup) {
        // tuple 变量重赋值（t68）：同形状逐槽写（形状不同拒编不错编）
        int idx = store_tuple_var(var->tup, v, expr.name());
        last_value_ = {nullptr, CGType::Tup, CGType::Int, "", idx};
        return;
    }
    llvm::Value* stored = coerce_for_slot(v, var->type, expr.name());
    if (var->bit_max > 0) {
        // byte/word 变量赋值点范围陷阱（t69，对齐解释器 coerce_to_declared）
        stored = check_bit_range(stored, var->bit_max,
                                 var->bit_max == 255 ? "byte" : "word");
    }
    builder_.CreateStore(stored, var->slot);
    if (var->uninit && scopes_.size() == var->decl_depth) {
        // 同块直线区域赋值（t92）：块内顺序执行，后续读运行期必已过此存储，
        // 清 uninit 放行；深层块（分支/循环体）赋值不清——运行期可能未执行，
        // 后续读维持拒编保守安全
        var->uninit = false;
    }
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
            unsupported("tuple element without value",
                        expr.paren().line(), expr.paren().column());
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
    // rt 侧全 kind 递归覆盖；内层经动态域索引读出 kind ≥ 2 落 CG9 陷阱（t88）
    const Token& bracket = expr.bracket();
    std::vector<CGValue> elements;
    elements.reserve(expr.elements().size());
    CGType elem = CGType::Int;
    std::string elem_cls; // 仅 elem==Obj 有意义：元素类名（t100 实例数组，复用 CGValue.cls）
    for (const auto& element : expr.elements()) {
        CGValue v = emit(element.get());
        if (v.type == CGType::Void) {
            unsupported("array element without value", bracket.line(), bracket.column());
        }
        if (elements.empty()) {
            elem = v.type;
            elem_cls = v.cls;
        } else if (v.type != elem) {
            // 数值系互混（t90 扩展含 Num）：统一提升 Double 视图——Num 运行期
            // tag 静态不可判，double 槽承载（rt format_f64 整数值省 .0，
            // print 输出与解释器混合表示一致）
            const auto numlike = [](CGType t) {
                return t == CGType::Int || t == CGType::Double ||
                       t == CGType::Num;
            };
            if (numlike(elem) && numlike(v.type)) {
                elem = CGType::Double;
            } else {
                unsupported("heterogeneous array literal",
                            bracket.line(), bracket.column());
            }
        } else if (elem == CGType::Obj && v.cls != elem_cls) {
            // 实例数组同类约束（t100）：kind 5 单一元素类布局，混合类
            // 实例数组拒编不错编（异类维持后置）
            unsupported("heterogeneous array literal (mixed classes)",
                        bracket.line(), bracket.column());
        }
        elements.push_back(v);
    }
    if (elem == CGType::Num) {
        elem = CGType::Double; // 全 Num 字面量（t90）：同上落 double 视图
    }
    llvm::Value* arr = builder_.CreateCall(
        rt_arr_new_,
        {builder_.getInt64(static_cast<uint64_t>(elements.size())),
         builder_.getInt64(static_cast<uint64_t>(arr_kind_of(elem)))},
        "arrnew");
    for (size_t i = 0; i < elements.size(); ++i) {
        CGValue v = elements[i];
        if (elem == CGType::Double &&
            (v.type == CGType::Int || v.type == CGType::Num)) {
            v = {to_double(v), CGType::Double}; // 同质提升：Int/Num 元素升 double
        }
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
        // 异质 tuple、Num/嵌套(Tup/Arr/Obj)元素、空 tuple 保持拒编——结果类型
        // 静态不可定或数组槽无法承载（elem_to_bits 仅 4 类），拒编不错编
        if (t.elems.empty()) {
            unsupported("non-constant index on empty tuple",
                        expr.bracket().line(), expr.bracket().column());
        }
        const CGType elem = t.elems.front().type;
        if (elem != CGType::Int && elem != CGType::Double &&
            elem != CGType::Bool && elem != CGType::Str) {
            unsupported("non-constant tuple index on this element type",
                        expr.bracket().line(), expr.bracket().column());
        }
        for (const auto& e : t.elems) {
            if (e.type != elem) {
                unsupported("non-constant index on heterogeneous tuple",
                            expr.bracket().line(), expr.bracket().column());
            }
        }
        CGValue index = emit(expr.index());
        if (index.type != CGType::Int) {
            unsupported("non-integer index",
                        expr.bracket().line(), expr.bracket().column());
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
        last_value_ = {bits_to_elem(bits, elem), elem};
        return;
    }
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
        if (object.elem == CGType::Arr) {
            // 嵌套数组内层读（t85/t89）：槽存内层数组 ptr 位模式，还原后
            // elem 记 Num 动态域哨兵——内层 kind 可为任意（t89 放宽），
            // 数值系内层索引读写照常，kind ≥ 2 内层索引读落 CG9 陷阱（t88）；
            // print/len/== 全 kind 天然工作
            llvm::Value* inner = builder_.CreateIntToPtr(
                bits, llvm::PointerType::getUnqual(context_), "inner");
            last_value_ = {inner, CGType::Arr, CGType::Num};
            return;
        }
        if (object.elem == CGType::Num) {
            // 动态域读（t70/t88）：运行时 kind 0/1 即 number tag，bits+kind
            // 直接拼 Num 零转换；kind ≥ 2（bool/str/嵌套数组经透传，t88）
            // 元素静态类型不可定，陷阱退出不错值（缺口 CG9，解释器可行）
            llvm::Value* kind = builder_.CreateCall(
                rt_arr_kind_, {object.value}, "arrkind");
            llvm::Function* fn = builder_.GetInsertBlock()->getParent();
            auto* trap_bb = llvm::BasicBlock::Create(context_, "dynkind.trap", fn);
            auto* cont_bb = llvm::BasicBlock::Create(context_, "dynkind.cont", fn);
            llvm::Value* bad = builder_.CreateICmpUGT(
                kind, builder_.getInt64(1), "dynkind.bad");
            builder_.CreateCondBr(bad, trap_bb, cont_bb);
            builder_.SetInsertPoint(trap_bb);
            builder_.CreateCall(rt_trap_arr_kind_, {kind});
            builder_.CreateUnreachable();
            builder_.SetInsertPoint(cont_bb);
            last_value_ = {make_num(kind, bits), CGType::Num};
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
    if (object.elem == CGType::Arr) {
        // 嵌套数组整槽替换（t85/t89）：外层槽写入新内层数组 ptr 位模式；
        // 值须为数组（任意元素，t89 放宽——kind 随内层对象自带），
        // 非数组值写 kind 4 槽拒编不错编
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
        if (v.type == CGType::Bool || v.type == CGType::Str) {
            builder_.CreateCall(
                rt_arr_set_num_,
                {object.value, index.value,
                 builder_.getInt64(static_cast<uint64_t>(arr_kind_of(v.type))),
                 elem_to_bits(v)});
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
        } else {
            unsupported("array element type mismatch in index assignment",
                        expr.bracket().line(), expr.bracket().column());
        }
    }
    if (object.elem == CGType::Obj && v.type == CGType::Obj &&
        v.cls != object.cls && !is_subclass_of(v.cls, object.cls)) {
        // 实例数组整槽写（t100）：同类或子类 upcast 放行（同 coerce_call_arg 语义），
        // 异类写入后按元素类解码会错值，拒编不错编
        unsupported("array element class mismatch in index assignment",
                    expr.bracket().line(), expr.bracket().column());
    }
    builder_.CreateCall(rt_arr_set_, {object.value, index.value, elem_to_bits(v)});
    last_value_ = v; // 赋值表达式的值为右侧值（与解释器一致，支持链式赋值）
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
            std::vector<llvm::Value*> args{object.value};
            for (size_t i = 0; i < arguments.size(); ++i) {
                CGValue a = emit(arguments[i].get());
                args.push_back(coerce_call_arg(a, info.param_types[i],
                                               info.param_cls[i], line, column));
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
            llvm::Value* result = nullptr;
            if (lone_tree) {
                // 单类树：动态类必为静态类，直调零开销（现状保持）
                result = builder_.CreateCall(info.fn, args);
            } else {
                // 各定义者副本签名须与静态类一致（覆写同签名，语义层校验的
                // 防御；不一致则统一 PHI 不可行，拒编不错编）
                for (const CGClass* sub : defs) {
                    const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
                    if (m.ret_type != info.ret_type || m.ret_cls != info.ret_cls ||
                        m.param_types != info.param_types ||
                        m.param_cls != info.param_cls) {
                        unsupported("overriding method '" + name +
                                        "' with a different signature",
                                    line, column);
                    }
                }
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
        std::vector<llvm::Value*> args{object.value};
        for (size_t i = 0; i < arguments.size(); ++i) {
            CGValue a = emit(arguments[i].get());
            args.push_back(coerce_call_arg(a, dm.param_types[i], dm.param_cls[i],
                                           line, column));
        }
        std::sort(defs.begin(), defs.end(),
                  [](const CGClass* a, const CGClass* b) {
                      return a->id < b->id;
                  });
        // 各定义者副本签名须与首个一致（同 t86 防御：语义层校验的兜底，
        // 不一致则统一 PHI 不可行，拒编不错编）
        for (const CGClass* sub : defs) {
            const CGMethod& m = sub->instances.at(sub->dispatch.at(name));
            if (m.ret_type != dm.ret_type || m.ret_cls != dm.ret_cls ||
                m.param_types != dm.param_types || m.param_cls != dm.param_cls) {
                unsupported("inherited method '" + name +
                                "' with a different signature",
                            line, column);
            }
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
        // 一致，位置前缀缺失同 t83 越界陷阱既定分歧）。异质/非 4 类元素/空 tuple/
        // 无命名字段/非 Str 键保持拒编——结果类型静态不可定或数组槽无法承载
        const long long n = static_cast<long long>(t.elems.size());
        if (n == 0) {
            unsupported("non-constant get() on empty tuple", line, column);
        }
        const CGType elem = t.elems.front().type;
        if (elem != CGType::Int && elem != CGType::Double &&
            elem != CGType::Bool && elem != CGType::Str) {
            unsupported("non-constant tuple get() on this element type", line, column);
        }
        for (const auto& e : t.elems) {
            if (e.type != elem) {
                unsupported("non-constant get() on heterogeneous tuple", line, column);
            }
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
        // values 数组（元素 kind）：逐元素 elem_to_bits 物化
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
        last_value_ = {bits_to_elem(bits, elem), elem};
        return;
    }

    if (name == "toNumber" && expr.arguments().empty()) {
        // toNumber() 方法形式（t63）：与内建 toNumber(x) 同一降级（语义层
        // 已限接收者为 string/bool/数值）
        last_value_ = {to_number_num(object, line, column), CGType::Num};
        return;
    }

    if (object.type == CGType::Tri && expr.arguments().empty() &&
        (name == "isTrue" || name == "isFalse" || name == "isUnset")) {
        // tribool 三态判定方法（t65）：与对应三态常量 icmp 出 bool（对齐解释器）
        const uint8_t code = name == "isTrue" ? 2 : name == "isFalse" ? 0 : 1;
        last_value_ = {builder_.CreateICmpEQ(object.value, builder_.getInt8(code),
                                             "tritest"),
                       CGType::Bool};
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
    if (object.type == CGType::Int || object.type == CGType::Double ||
        object.type == CGType::Num) {
        // number 专属方法（t67）：十方法均 0 参（语义层已校验，此处防御）
        const bool is_num_method =
            name == "abs" || name == "integerPart" || name == "decimalPart" ||
            name == "isInteger" || name == "isDecimal" || name == "isNaN" ||
            name == "isInfinity" || name == "isFinite" ||
            name == "isPositive" || name == "isNegative";
        if (is_num_method) {
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
    if (object.type == CGType::Obj) {
        // 类实例字段读（t60/t102/t103）：同树定义者收集——downcast 放行后
        // 动态类可为整棵继承树上任意类（旁支可经共同祖先中转写入槽）；
        // 单类树维持直 GEP + load 零开销，多类树读对象头类 id switch 到
        // 定义者字段槽（各定义者按自身 field_index 下标 GEP），default
        // （动态类实例无此字段）走运行期陷阱不错值——消息对齐解释器
        // "Undefined property 'X' on object"
        const CGClass& cls = classes_.at(object.cls);
        const std::string name(expr.name().lexeme());
        std::vector<const CGClass*> defs;
        bool lone_tree = true;
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
        if (defs.empty()) {
            unsupported("undefined property '" + name + "' on class '" + object.cls +
                            "'",
                        expr.name().line(), expr.name().column());
        }
        if (lone_tree) {
            // 单类树：动态类必为静态类，直 GEP + load 零开销（现状保持）
            auto it = cls.field_index.find(name);
            const CGField& field = cls.fields[it->second];
            llvm::Value* slot =
                builder_.CreateStructGEP(cls.type, object.value, it->second + 1, name);
            // Arr 字段 elem 记 Num 哨兵（t71：CGField 无元素类型伴随，读出
            // 即动态域，同 t70 形参机制）；Obj 字段带类名（t72）
            last_value_ = {builder_.CreateLoad(llvm_type_of(field.type), slot, name),
                           field.type,
                           field.type == CGType::Arr ? CGType::Num : CGType::Int,
                           field.cls};
            return;
        }
        const CGField& ref = defs[0]->fields.at(defs[0]->field_index.at(name));
        std::sort(defs.begin(), defs.end(),
                  [](const CGClass* a, const CGClass* b) {
                      return a->id < b->id;
                  });
        // 各定义者字段类型须与首个一致（同 t86 防御：不一致则统一 PHI
        // 与后续使用类型不可定，拒编不错编）
        for (const CGClass* sub : defs) {
            const CGField& f = sub->fields.at(sub->field_index.at(name));
            if (f.type != ref.type || f.cls != ref.cls || f.bit_max != ref.bit_max) {
                unsupported("inherited field '" + name + "' with a different type",
                            expr.name().line(), expr.name().column());
            }
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
        last_value_ = {phi, ref.type,
                       ref.type == CGType::Arr ? CGType::Num : CGType::Int,
                       ref.cls};
        return;
    }
    if (object.type == CGType::Tup) {
        // tuple 属性（t68）：length 常量折叠（优先于同名字段，对齐解释器
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
    if (object.type != CGType::Obj) {
        unsupported("property assignment on this value type",
                    expr.name().line(), expr.name().column());
    }
    const CGClass& cls = classes_.at(object.cls);
    const std::string name(expr.name().lexeme());
    // 同树定义者收集（t60/t102/t103，与读路径一致）：downcast 放行后动态
    // 类可为整棵继承树上任意类；单类树维持直 GEP 存零开销，多类树各
    // arm 按定义者布局存储，default（动态类实例无此字段）走运行期陷阱
    // 不错值——消息对齐解释器 "Undefined property 'X' on object"
    std::vector<const CGClass*> defs;
    bool lone_tree = true;
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
    if (defs.empty()) {
        unsupported("undefined property '" + name + "' on class '" + object.cls + "'",
                    expr.name().line(), expr.name().column());
    }
    if (lone_tree) {
        // 单类树：动态类必为静态类，直 GEP + store 零开销（现状保持）
        auto it = cls.field_index.find(name);
        CGValue v = emit(expr.value());
        const CGField& field = cls.fields[it->second];
        llvm::Value* stored = coerce_for_slot(v, field.type, expr.name(), field.cls);
        if (field.bit_max > 0) {
            // byte/word 字段赋值点范围陷阱（t87，对齐解释器 coerce_to_declared）
            stored = check_bit_range(stored, field.bit_max,
                                     field.bit_max == 255 ? "byte" : "word");
        }
        builder_.CreateStore(
            stored, builder_.CreateStructGEP(cls.type, object.value, it->second + 1, name));
        // 赋值表达式的值 = 所赋的值（与解释器一致）；Arr 字段带 Num 哨兵（t71），
        // Obj 字段带类名（t72）
        last_value_ = {stored, field.type,
                       field.type == CGType::Arr ? CGType::Num : CGType::Int,
                       field.cls};
        return;
    }
    // 多类树：字段类型一致防御后值在 switch 前求值/coerce（求值顺序
    // object → value 不变，bit 陷阱仍在赋值点前），各 arm 按定义者布局存储
    const CGField& ref = defs[0]->fields.at(defs[0]->field_index.at(name));
    std::sort(defs.begin(), defs.end(),
              [](const CGClass* a, const CGClass* b) {
                  return a->id < b->id;
              });
    for (const CGClass* sub : defs) {
        const CGField& f = sub->fields.at(sub->field_index.at(name));
        if (f.type != ref.type || f.cls != ref.cls || f.bit_max != ref.bit_max) {
            unsupported("inherited field '" + name + "' with a different type",
                        expr.name().line(), expr.name().column());
        }
    }
    CGValue v = emit(expr.value());
    llvm::Value* stored = coerce_for_slot(v, ref.type, expr.name(), ref.cls);
    if (ref.bit_max > 0) {
        stored = check_bit_range(stored, ref.bit_max,
                                 ref.bit_max == 255 ? "byte" : "word");
    }
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
            stored, builder_.CreateStructGEP(sub->type, object.value, idx, name));
        incoming.emplace_back(builder_.GetInsertBlock(), stored);
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
    // 赋值表达式的值 = 所赋的值（与解释器一致）；Arr 字段带 Num 哨兵（t71），
    // Obj 字段带类名（t72）
    last_value_ = {phi, ref.type,
                   ref.type == CGType::Arr ? CGType::Num : CGType::Int,
                   ref.cls};
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
    uint64_t size = 8; // i64 类 id 头部（t86）
    for (const CGField& field : cls.fields) {
        size += field.type == CGType::Num ? 16 : 8;
    }
    llvm::Value* obj =
        builder_.CreateCall(rt_obj_new_, {builder_.getInt64(size)}, "objnew");
    // 写类 id 头部（struct 元素 0，t86）：upcast 后方法调用点按 id 动态分派
    builder_.CreateStore(builder_.getInt64(cls.id),
                         builder_.CreateStructGEP(cls.type, obj, 0, "clsid"));
    for (unsigned i = 0; i < cls.fields.size(); ++i) {
        const CGField& field = cls.fields[i];
        CGValue v = emit(field.decl->initializer());
        // 字段初始值按声明类型对齐（解释器 coerce_to_declared 等价静态检查）
        llvm::Value* stored = coerce_for_slot(v, field.type, field.decl->name(),
                                              field.cls);
        if (field.bit_max > 0) {
            // byte/word 字段初始化范围陷阱（t87，对齐解释器 coerce_to_declared）
            stored = check_bit_range(stored, field.bit_max,
                                     field.bit_max == 255 ? "byte" : "word");
        }
        builder_.CreateStore(stored,
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
    // 零初始化全局槽/函数内 alloca 不预存），CGVar 记 uninit + 声明深度——
    // uninit 期间读拒编（语义层流不敏感放行的分支内赋值后读，解释器运行期
    // 仍是 none，零初始化槽会错值，拒编不错编），同块赋值后清；
    // 其余类型（array/Tuple/类）维持拒编
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
            var.decl_depth = scopes_.size();
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
            var.decl_depth = scopes_.size();
            scopes_.back()[name] = var;
            return;
        }
        if (tt == TokenType::KW_ARRAY) {
            // 无初始化 array 变量（t101）：elem 无从静态推断，记 Num 动态域
            // 哨兵（t70/t88）——后续赋值走 visitAssign Arr 动态域透传（任意元素
            // 数组指针直存）、读出 kind 随对象自带、kind ≥ 2 落 CG9 陷阱；
            // uninit 期读拒编（同块赋值后清，t92）
            const std::string name(stmt.name().lexeme());
            CGVar var;
            var.slot = create_var_slot(llvm_type_of(CGType::Arr), name);
            var.type = CGType::Arr;
            var.elem = CGType::Num;
            var.uninit = true;
            var.decl_depth = scopes_.size();
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
            var.decl_depth = scopes_.size();
            scopes_.back()[name] = var;
            return;
        }
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
        scopes_.back()[name] = {slot, CGType::Obj, CGType::Int, cls_name};
        return;
    }
    // byte/word 变量（t69）：i64 承载零类型扩散，仅声明/赋值点插范围陷阱
    //（对齐解释器 coerce_to_declared 只在赋值点校验、表达式域无截断）；
    // 初始值须 Int（Num 整数态静态不可判、Double 拒编不错编）
    if (stmt.type().type() == TokenType::KW_BYTE ||
        stmt.type().type() == TokenType::KW_WORD) {
        const bool is_byte = stmt.type().type() == TokenType::KW_BYTE;
        const long long max_val = is_byte ? 255 : 65535;
        CGValue bit_init = emit(stmt.initializer());
        if (bit_init.type != CGType::Int) {
            unsupported(std::string("initializing '") + (is_byte ? "byte" : "word") +
                            "' variable with non-integer value",
                        stmt.name().line(), stmt.name().column());
        }
        llvm::Value* checked = check_bit_range(bit_init.value, max_val,
                                               is_byte ? "byte" : "word");
        const std::string bit_name(stmt.name().lexeme());
        llvm::Value* slot = create_var_slot(builder_.getInt64Ty(), bit_name);
        builder_.CreateStore(checked, slot);
        scopes_.back()[bit_name] = {slot, CGType::Int, CGType::Int, "", -1, max_val};
        return;
    }
    CGType type = declared_cgtype(stmt.type());
    CGValue init = emit(stmt.initializer());
    CGType elem = CGType::Int;
    std::string elem_cls; // 仅 type==Arr && elem==Obj 有意义：元素类名（t100）
    llvm::Value* stored = nullptr;
    if (type == CGType::Tup) {
        // Tuple 变量（t68）：解构为逐元素独立槽（无单一 slot，形状入
        // tuple_vars_）；声明无元素类型标注，形状取自初始值
        if (init.type != CGType::Tup) {
            unsupported("initializing 'Tuple' variable with non-tuple value",
                        stmt.name().line(), stmt.name().column());
        }
        const std::string name(stmt.name().lexeme());
        int idx = create_tuple_var(tuple_values_[init.tup], name);
        scopes_.back()[name] = {nullptr, CGType::Tup, CGType::Int, "", idx};
        return;
    }
    if (type == CGType::Arr) {
        // array 槽存不透明 ptr（指针拷贝即引用语义）；声明无元素类型标注，
        // 元素类型取自初始值的同质推断结果（t59）
        if (init.type != CGType::Arr) {
            unsupported("initializing 'array' variable with non-array value",
                        stmt.name().line(), stmt.name().column());
        }
        stored = init.value;
        elem = init.elem;
        elem_cls = init.cls; // Obj 元素数组：拷贝元素类名（t100）
    } else {
        stored = coerce_for_slot(init, type, stmt.name());
    }
    const std::string name(stmt.name().lexeme());
    llvm::Value* slot = create_var_slot(llvm_type_of(type), name);
    builder_.CreateStore(stored, slot);
    scopes_.back()[name] = {slot, type, elem, elem_cls}; // 同名直接遮蔽（重复声明由语义层拦截）
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

void CodeGenerator::visitSwitch(const SwitchStmt& stmt) {
    const Token& tok = stmt.switch_token();
    // 条件只求值一次；级联比较块链（gen_multi_match 的语句版，无结果 PHI），
    // 候选按 case 序/值序惰性求值、首命中即执行 body 后结束（无 fallthrough），
    // default 位置无关最后兜底，均对齐解释器 visitSwitch
    CGValue cond = emit(stmt.condition());
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* end_bb = llvm::BasicBlock::Create(context_, "switch.end", fn);

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

    // 各 body（BlockStmt 自带作用域）执行后跳 end；body 内 break/continue
    // 维持绑定外层循环（解释器 switch 不捕获 BreakSignal，loop 栈不动）
    for (const auto& p : bodies) {
        builder_.SetInsertPoint(p.body_bb);
        if (p.body != nullptr) {
            p.body->accept(*this);
        }
        if (!builder_.GetInsertBlock()->getTerminator()) {
            builder_.CreateBr(end_bb);
        }
    }
    builder_.SetInsertPoint(end_bb);
}

void CodeGenerator::visitFunction(const FunctionStmt& stmt) {
    // 函数体生成（第二遍，S5 t52）：原型已在第一遍建好；
    // 嵌套函数（t91）按改编键查表，未登记的嵌套位置（类方法体内）维持拒编
    const std::string name(stmt.name().lexeme());
    auto nested_it = nested_fns_.find(&stmt);
    const bool is_nested = nested_it != nested_fns_.end();
    if (!is_nested && in_function_) {
        unsupported("nested function declaration",
                    stmt.name().line(), stmt.name().column());
    }
    const std::string& key = is_nested ? nested_it->second : name;
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
    const CGType saved_ret_type = current_ret_type_;
    const std::string saved_ret_cls = current_ret_cls_;
    const long long saved_ret_bit_max = current_ret_bit_max_;
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
    current_ret_type_ = info.ret_type;
    current_ret_cls_ = info.ret_cls;
    current_ret_bit_max_ = info.ret_bit_max;

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
        // Arr 形参 elem 记 Num 哨兵（t70：签名处元素类型不可知，动态域路径）
        CGVar binding{slot, info.param_types[i],
                      info.param_types[i] == CGType::Arr ? CGType::Num
                                                         : CGType::Int,
                      info.param_cls[i]};
        // byte/word 形参（t98）：置 bit_max，体内重赋走既有赋值点 check_bit_range
        const TokenType ptt = param.type.type();
        if (ptt == TokenType::KW_BYTE || ptt == TokenType::KW_WORD) {
            binding.bit_max = ptt == TokenType::KW_BYTE ? 255 : 65535;
        }
        scopes_.back()[pname] = binding;
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

    in_function_ = saved_in_function;
    current_ret_type_ = saved_ret_type;
    current_ret_cls_ = saved_ret_cls;
    current_ret_bit_max_ = saved_ret_bit_max;
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
            } else if (current_ret_type_ == CGType::Num &&
                       (v.type == CGType::Int || v.type == CGType::Double)) {
                // integer/decimal → number 加宽（t62）：保持原表示打 tag
                v = {to_num(v), CGType::Num};
            } else if (current_ret_type_ == CGType::Tri && v.type == CGType::Bool) {
                // bool → tribool 单向加宽（t65，与语义层一致）
                v = {to_tri(v), CGType::Tri};
            } else {
                unsupported("return type mismatch",
                            stmt.keyword().line(), stmt.keyword().column());
            }
        } else if (v.type == CGType::Obj && v.cls != current_ret_cls_ &&
                   !is_subclass_of(v.cls, current_ret_cls_) &&
                   !is_subclass_of(current_ret_cls_, v.cls)) {
            // 同一继承树内 upcast/downcast 放行（t86/t103）；跨树拒编不错编
            unsupported("returning instance of class '" + v.cls +
                            "' where '" + current_ret_cls_ + "' is declared",
                        stmt.keyword().line(), stmt.keyword().column());
        }
        // Arr 返回值任意元素透传（t88 解除 t70 数值系守卫：kind 随数组对象
        // 自带，print/len/== rt 侧全 kind 覆盖；索引读 kind ≥ 2 落 CG9 陷阱）
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
    CGClass& cls = it->second;
    for (const CGClass* c = &cls; c != nullptr;
         c = c->super.empty() ? nullptr : &classes_.at(c->super)) {
        const std::string dname(c->stmt->name().lexeme());
        for (const auto& member : c->stmt->members()) {
            if (const auto* method = dynamic_cast<const FunctionStmt*>(member.get())) {
                gen_method_body(cls, cls.instances.at(
                    dname + "." + std::string(method->name().lexeme())));
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
        // tuple 进函数签名拒编（t68）：静态展开的形状跨函数边界不可知
        unsupported("tuple in function signature",
                    type_token.line(), type_token.column());
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

llvm::Value* CodeGenerator::coerce_call_arg(const CGValue& a, CGType want,
                                            const std::string& want_cls,
                                            size_t line, size_t column) {
    // 实参对齐形参类型：仅 integer→decimal 提升；Obj 允许同一继承树内
    // upcast/downcast（t86/t103，指针原样传递，调用点按对象头类 id 动态
    // 分派/陷阱保语义），跨树拒编不错编
    if (a.type == want) {
        if (want == CGType::Obj && a.cls != want_cls &&
            !is_subclass_of(a.cls, want_cls) &&
            !is_subclass_of(want_cls, a.cls)) {
            unsupported("passing instance of class '" + a.cls +
                            "' where '" + want_cls + "' is expected",
                        line, column);
        }
        // Arr 实参任意元素透传（t88 解除 t70 数值系守卫：kind 随数组对象
        // 自带，print/len/== rt 侧全 kind 覆盖；索引读 kind ≥ 2 落 CG9 陷阱）
        return a.value;
    }
    if (want == CGType::Double && a.type == CGType::Int) {
        return to_double(a);
    }
    // integer/decimal → number 加宽（t62）：保持原表示打 tag；反向窄化
    // （number→integer/decimal）静态无法判 tag，落入下方拒编
    if (want == CGType::Num &&
        (a.type == CGType::Int || a.type == CGType::Double)) {
        return to_num(a);
    }
    // bool → tribool 单向加宽（t65，与语义层一致）
    if (want == CGType::Tri && a.type == CGType::Bool) {
        return to_tri(a);
    }
    unsupported("argument type mismatch", line, column);
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
        case CGType::Bool:   return builder_.getInt1Ty();
        case CGType::Tri:    return builder_.getInt8Ty(); // 三态编码（t65）
        case CGType::Str:    return llvm::PointerType::getUnqual(context_);
        case CGType::Arr:    return llvm::PointerType::getUnqual(context_);
        case CGType::Obj:    return llvm::PointerType::getUnqual(context_);
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
                                            const std::string& slot_cls) {
    if (v.type == slot_type) {
        // Arr 字段槽任意元素透传（t88 解除 t71 数值系守卫：字段槽 elem 恒
        // Num 哨兵，kind 随数组对象自带——print/len/== rt 侧全 kind 覆盖，
        // 索引读 kind ≥ 2 落 CG9 陷阱；Arr 目标仅字段路径可达）
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
    // 仅 integer → decimal 隐式提升（与语义层/解释器 coerce_to_declared 一致）
    if (slot_type == CGType::Double && v.type == CGType::Int) {
        return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
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

void CodeGenerator::declare_function(const FunctionStmt& stmt,
                                     const std::string& prefix) {
    const std::string name(stmt.name().lexeme());
    // 嵌套函数注册键改编为 prefix.name（t91）：用户标识符无 '.'，
    // 与顶层名/其他外层的同名嵌套天然不冲突
    const std::string key = prefix.empty() ? name : prefix + "." + name;
    if (functions_.count(key) != 0) {
        // 语义层支持同名重载，codegen 第一期仅单签名（同外层同名嵌套同此拒编）
        unsupported("function overloading for '" + key + "'",
                    stmt.name().line(), stmt.name().column());
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
    // 父类字段的 GEP 索引在子类 struct 中不变，父类方法副本可直接复用）；
    // struct 元素 0 恒为 i64 类 id 头部（t86），字段 GEP 下标 = 逻辑下标 + 1
    std::vector<llvm::Type*> field_types;
    field_types.push_back(builder_.getInt64Ty()); // 类 id 头部
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
        for (const CGField& field : cls.fields) {
            field_types.push_back(llvm_type_of(field.type));
        }
    }

    // 自身字段追加（下标即 GEP 索引）；类型限 declared_cgtype 支持面
    for (const auto& member : stmt.members()) {
        const auto* field = dynamic_cast<const VarDeclStmt*>(member.get());
        if (!field) continue;
        const std::string fname(field->name().lexeme());
        if (cls.field_index.count(fname) != 0) {
            // 本类重名或与父链字段同名（遮蔽）：解释器 base-first 覆写值，
            // 静态布局下类型/初值歧义无法单槽承载，拒编不错编（t61 范围外）
            unsupported("duplicate or shadowing field '" + fname + "'",
                        field->name().line(), field->name().column());
        }
        if (!field->initializer()) {
            // 无初值字段解释器落 none，静态布局无对应表示（t60 范围外）
            unsupported("class field without initializer",
                        field->name().line(), field->name().column());
        }
        CGType ftype;
        std::string fcls;
        long long fbit_max = 0;
        if (field->type().type() == TokenType::IDENTIFIER) {
            // 类实例字段（t72）：IDENTIFIER 视为类名，须已注册（声明在前，
            // 同父类/签名要求）；自引用字段本类尚未入表自然落此拒编——
            // 解释器侧自引用 + 字段必有初始值 = 无限递归 new，拒编不错编
            fcls = std::string(field->type().lexeme());
            if (classes_.count(fcls) == 0) {
                unsupported("field type '" + fcls + "'",
                            field->name().line(), field->name().column());
            }
            ftype = CGType::Obj;
        } else if (field->type().type() == TokenType::KW_BYTE ||
                   field->type().type() == TokenType::KW_WORD) {
            // byte/word 字段（t87）：i64 槽承载 + 记 bit_max，初始化/赋值
            // 两触点插 check_bit_range 陷阱（对齐解释器 coerce_to_declared
            // 赋值点校验）；读出恒 Int，表达式域无截断（同 t69 变量语义）
            ftype = CGType::Int;
            fbit_max = field->type().type() == TokenType::KW_BYTE ? 255 : 65535;
        } else {
            ftype = declared_cgtype(field->type());
        }
        // array 字段放行（t71）：字段槽即 opaque ptr；无处标注元素类型，
        // 读出即动态域（visitProperty 置 Num 哨兵，t70 机制复用），
        // 写入守卫在 coerce_for_slot（elem 限数值系，保 kind ∈ {0,1}）
        // number 字段放行（t74）：StructType 按 llvm_type_of 逐字段拼装，
        // {i64,i64} 自动占位；malloc 上界在 visitNew 按字段类型累计（Num 16）
        cls.field_index[fname] = static_cast<unsigned>(cls.fields.size());
        cls.fields.push_back({fname, ftype, field, fcls, fbit_max});
        field_types.push_back(llvm_type_of(ftype));
    }
    cls.type = llvm::StructType::create(context_, field_types,
                                        "collie.class." + name);
    classes_[name] = std::move(cls);
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
            // 本类内同名重复（语义层支持方法重载，codegen 第一期仅单签名）
            unsupported("method overloading for '" + mname + "'",
                        method->name().line(), method->name().column());
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
    scopes_.clear();
    scopes_.emplace_back();
    if (!saved_scopes.empty()) {
        scopes_.back() = saved_scopes.front();
    }
    scopes_.emplace_back(); // 参数层（可遮蔽全局）
    loops_.clear();
    in_function_ = true;
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

    in_function_ = false;
    current_ret_type_ = CGType::Void;
    current_ret_cls_.clear();
    current_ret_bit_max_ = 0;
    current_this_ = nullptr;
    current_class_name_.clear();
    current_defining_class_.clear();
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
    auto eval_arm = [&](llvm::BasicBlock* bb, const Expr* e) {
        builder_.SetInsertPoint(bb);
        Arm arm;
        arm.value = emit(e);
        arm.end = builder_.GetInsertBlock();
        arms.push_back(std::move(arm));
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

    // 分支类型统一：同型直用（Arr elem / Obj cls 一致性校验）；数值混型任一
    // Num 统一 Num 否则提升 Double（与算术混型一致）；tribool/bool 混型
    // 统一 tribool（单向加宽，t65）
    auto is_numeric = [](CGType t) {
        return t == CGType::Int || t == CGType::Double || t == CGType::Num;
    };
    CGType result_type = arms.front().value.type;
    std::string result_cls = arms.front().value.cls;
    CGType result_elem = arms.front().value.elem;
    if (result_type == CGType::Void) {
        unsupported("ternary branch has no value",
                    expr.question_token().line(), expr.question_token().column());
    }
    if (result_type == CGType::Tup) {
        // tuple 虚值（value=nullptr）不进标量 PHI：全支同为 tuple 且形状一致
        // 时静态展开逐元素 PHI 合流（t95），否则拒编不错编
        for (const auto& arm : arms) {
            if (arm.value.type != CGType::Tup) {
                unsupported("ternary branches have incompatible types",
                            expr.question_token().line(),
                            expr.question_token().column());
            }
        }
        auto* tup_merge_bb = llvm::BasicBlock::Create(context_, "tern.end", fn);
        std::vector<int> tups;
        std::vector<llvm::BasicBlock*> arm_ends;
        for (const auto& arm : arms) {
            tups.push_back(arm.value.tup);
            arm_ends.push_back(arm.end);
        }
        const int idx = merge_tuple_arms(tups, arm_ends, tup_merge_bb,
                                         "ternary branches",
                                         expr.question_token().line(),
                                         expr.question_token().column());
        last_value_ = {nullptr, CGType::Tup, CGType::Int, "", idx};
        return;
    }
    for (size_t i = 1; i < arms.size(); ++i) {
        const CGValue& v = arms[i].value;
        if (v.type == result_type) {
            if (v.type == CGType::Arr && v.elem != result_elem) {
                // 同为数组但元素类型不同：统一 elem=Num 动态域哨兵（t94）——
                // 数组值同为不透明 ptr，PHI 无关 elem；kind 随数组对象运行期
                // 自带，print/len/== rt 侧全 kind 覆盖，索引读 kind ≥ 2 落
                // 既有 CG9 陷阱不错值
                result_elem = CGType::Num;
            }
            if (v.type == CGType::Obj && v.cls != result_cls) {
                // 类不同：统一到最近公共祖先（t93）——Obj 的 LLVM 表示同为
                // 指针，PHI 无关 cls；t86 对象头类 id + 动态分派保证合流值
                // 按运行期真实类解析方法；无公共祖先维持拒编不错编
                const std::string nca = nearest_common_ancestor(result_cls, v.cls);
                if (nca.empty()) {
                    unsupported("ternary branches yield instances of different classes",
                                expr.question_token().line(),
                                expr.question_token().column());
                }
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
        unsupported("ternary branches have incompatible types",
                    expr.question_token().line(), expr.question_token().column());
    }

    // 各分支块尾把值对齐 result_type 后再跳 merge（提升指令须落在该分支块内）
    auto* merge_bb = llvm::BasicBlock::Create(context_, "tern.end", fn);
    for (auto& arm : arms) {
        builder_.SetInsertPoint(arm.end);
        arm.aligned = result_type == CGType::Double ? to_double(arm.value)
                    : result_type == CGType::Num    ? to_num(arm.value)
                    : result_type == CGType::Tri    ? to_tri(arm.value)
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
    // 复用 visitBinary == 的四路降级（语义层已保证候选与目标可 == 比较）
    if (target.type == CGType::Str && cand.type == CGType::Str) {
        llvm::Value* c =
            builder_.CreateCall(rt_strcmp_, {target.value, cand.value}, "strcmptmp");
        return builder_.CreateICmpEQ(c, builder_.getInt32(0), "matcheq");
    }
    if (target.type == CGType::Tri || cand.type == CGType::Tri) {
        // tribool 三态判等（t65）：双方限 tribool/bool（bool 加宽三态后 icmp）
        if ((target.type != CGType::Tri && target.type != CGType::Bool) ||
            (cand.type != CGType::Tri && cand.type != CGType::Bool)) {
            unsupported("'==?' comparison of tribool with this value type",
                        op.line(), op.column());
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
    // tuple 目标/候选（t78）：双 Tup 走 t75 静态展开深比较（含 Arr 元素
    // 已于 t79 下沉 rt_arr_eq 深比较）；Tup × 非 Tup 恒 false（对齐解释器
    // values_equal kind 不等，语义层通常更早拦截，此为防御性双保险）
    if (target.type == CGType::Tup || cand.type == CGType::Tup) {
        return target.type == CGType::Tup && cand.type == CGType::Tup
                   ? gen_tuple_eq(target, cand, op)
                   : builder_.getInt1(false);
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
    // object 等候选比较范围外，拒编不错编
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
        builder_.SetInsertPoint(body_bb);
        Arm arm;
        arm.value = emit(branch.result.get());
        arm.end = builder_.GetInsertBlock();
        arms.push_back(std::move(arm));
        builder_.SetInsertPoint(chain_bb);
    }
    // 全部未命中落到默认分支（比较链末端即默认块）；tribool 穷尽形式
    // 无默认（t65）：i8 值域严格 {0,1,2} 且候选已穷尽三态，链尾静态不可达
    if (expr.default_expr() != nullptr) {
        Arm def;
        def.value = emit(expr.default_expr());
        def.end = builder_.GetInsertBlock();
        arms.push_back(std::move(def));
    } else {
        builder_.CreateUnreachable();
    }

    // 结果类型统一：沿用 gen_ternary 规则扩展到 N+1 支（同型直用含 Arr elem/
    // Obj cls 一致性校验；数值混型任一 Num 统一 Num 否则 Double）
    auto is_numeric = [](CGType t) {
        return t == CGType::Int || t == CGType::Double || t == CGType::Num;
    };
    CGType result_type = arms.front().value.type;
    std::string result_cls = arms.front().value.cls;
    CGType result_elem = arms.front().value.elem;
    if (result_type == CGType::Void) {
        unsupported("'==?' branch result has no value", op.line(), op.column());
    }
    if (result_type == CGType::Tup) {
        // tuple 虚值（value=nullptr）不进标量 PHI：全支同为 tuple 且形状一致
        // 时静态展开逐元素 PHI 合流（t95，同 gen_ternary），否则拒编不错编
        for (const auto& arm : arms) {
            if (arm.value.type != CGType::Tup) {
                unsupported("'==?' branches have incompatible types",
                            op.line(), op.column());
            }
        }
        auto* tup_merge_bb = llvm::BasicBlock::Create(context_, "match.end", fn);
        std::vector<int> tups;
        std::vector<llvm::BasicBlock*> arm_ends;
        for (const auto& arm : arms) {
            tups.push_back(arm.value.tup);
            arm_ends.push_back(arm.end);
        }
        const int idx = merge_tuple_arms(tups, arm_ends, tup_merge_bb,
                                         "'==?' branches", op.line(), op.column());
        last_value_ = {nullptr, CGType::Tup, CGType::Int, "", idx};
        return;
    }
    for (size_t i = 1; i < arms.size(); ++i) {
        const CGValue& v = arms[i].value;
        if (v.type == result_type) {
            if (v.type == CGType::Arr && v.elem != result_elem) {
                // elem 不同：统一 Num 动态域哨兵（t94，同 gen_ternary）
                result_elem = CGType::Num;
            }
            if (v.type == CGType::Obj && v.cls != result_cls) {
                // 类不同：统一到最近公共祖先（t93，同 gen_ternary），
                // 无公共祖先维持拒编不错编
                const std::string nca = nearest_common_ancestor(result_cls, v.cls);
                if (nca.empty()) {
                    unsupported("'==?' branches yield instances of different classes",
                                op.line(), op.column());
                }
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
            // tribool/bool 混型统一 tribool（单向加宽，t65）
            result_type = CGType::Tri;
            continue;
        }
        unsupported("'==?' branches have incompatible types", op.line(), op.column());
    }

    // 各分支结束块尾对齐 result_type 后跳 merge（转换指令须落在该分支块内）
    auto* merge_bb = llvm::BasicBlock::Create(context_, "match.end", fn);
    for (auto& arm : arms) {
        builder_.SetInsertPoint(arm.end);
        arm.aligned = result_type == CGType::Double ? to_double(arm.value)
                    : result_type == CGType::Num    ? to_num(arm.value)
                    : result_type == CGType::Tri    ? to_tri(arm.value)
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
        default:
            unsupported("toNumber() of this value type", line, column);
    }
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
        case CGType::Tup:
            // tuple 转串（t68）：静态展开拼接（拉通 toString/插值/'+' 拼接）
            return tuple_to_str(v, where);
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
        default:             unsupported("array element type", 0, 0);
    }
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
        case CGType::Arr:    return 4; // 嵌套数组：槽存内层数组 ptr 位模式（t85）
        case CGType::Obj:    return 5; // 实例数组：槽存实例 ptr 位模式（t100）
        default:             unsupported("array element type", 0, 0);
    }
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
            if (e.type != CGType::Obj || e.cls != var.cls) {
                unsupported("assigning tuple with incompatible instance element to '" +
                                std::string(where.lexeme()) + "'",
                            where.line(), where.column());
            }
            builder_.CreateStore(e.value, var.slot);
            stored.elems.push_back(e);
            continue;
        }
        llvm::Value* sv = coerce_for_slot(e, var.type, where);
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
