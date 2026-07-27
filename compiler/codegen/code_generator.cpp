/**
 * @file code_generator.cpp
 * @brief AST → LLVM IR 代码生成器实现（M6 t49/t50/t51，S1–S4 子集）
 *
 * 降级规则见 compiler/codegen/README.md 第四节；语义依据 compiler/SPEC.md。
 */
#include "code_generator.h"

#include <cerrno>
#include <cstdlib>

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

namespace collie {

CodeGenerator::CodeGenerator() : builder_(context_) {}

void CodeGenerator::generate(const std::vector<std::unique_ptr<Stmt>>& statements,
                             const std::string& module_name) {
    module_ = std::make_unique<llvm::Module>(module_name, context_);
    // 显式标记宿主 target triple，免得 clang 编 .ll 时报 override-module 警告
    module_->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));

    // printf 声明：i32 (ptr, ...) 变参；print 统一降级为一次 printf 调用
    auto* printf_type = llvm::FunctionType::get(
        builder_.getInt32Ty(), {llvm::PointerType::getUnqual(context_)},
        /*isVarArg=*/true);
    printf_fn_ = module_->getOrInsertFunction("printf", printf_type);

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
            llvm::Value* rem = builder_.CreateSRem(lhs.value, rhs.value, "remtmp");
            llvm::Value* zero = builder_.getInt64(0);
            llvm::Value* nonzero = builder_.CreateICmpNE(rem, zero);
            llvm::Value* rem_neg = builder_.CreateICmpSLT(rem, zero);
            llvm::Value* rhs_neg = builder_.CreateICmpSLT(rhs.value, zero);
            llvm::Value* sign_diff = builder_.CreateICmpNE(rem_neg, rhs_neg);
            llvm::Value* need_fix = builder_.CreateAnd(nonzero, sign_diff);
            llvm::Value* fixed = builder_.CreateAdd(rem, rhs.value, "remfix");
            last_value_ = {builder_.CreateSelect(need_fix, fixed, rem, "floormod"),
                           CGType::Int};
            return;
        }
        case TokenType::OP_PLUS:
        case TokenType::OP_MINUS:
        case TokenType::OP_MULTIPLY: {
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
                // i64 域（妥协点 CG1：溢出回绕暂容忍，不 nsw 陷阱）
                llvm::Value* v = op.type() == TokenType::OP_PLUS
                                     ? builder_.CreateAdd(lhs.value, rhs.value)
                               : op.type() == TokenType::OP_MINUS
                                     ? builder_.CreateSub(lhs.value, rhs.value)
                                     : builder_.CreateMul(lhs.value, rhs.value);
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
        last_value_ = {builder_.CreateSub(builder_.getInt64(0), v.value, "negtmp"),
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
    unsupported("function call", expr.paren().line(), expr.paren().column());
}

void CodeGenerator::gen_print(const CallExpr& expr) {
    // print(a, b, ...)：与解释器 call_builtin_print 对齐——空格分隔 + 末尾换行；
    // 编译期按参数 CGType 拼一条 printf 格式串，一次调用完成
    std::string format;
    std::vector<llvm::Value*> args;
    args.push_back(nullptr); // 占位：格式串放最前

    const auto& arguments = expr.arguments();
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) format += ' ';
        CGValue v = emit(arguments[i].get());
        switch (v.type) {
            case CGType::Str:
                format += "%s";
                args.push_back(v.value);
                break;
            case CGType::Int:
                format += "%lld";
                args.push_back(v.value);
                break;
            case CGType::Double:
                // %g 与解释器 ostringstream 默认格式同为 6 位有效数字；
                // ±Infinity/NaN 的拼写差异属缺口 CG2（collie_rt 垫片统一接管后消除）
                format += "%g";
                args.push_back(v.value);
                break;
            case CGType::Bool: {
                format += "%s";
                llvm::Value* true_str = builder_.CreateGlobalString("true");
                llvm::Value* false_str = builder_.CreateGlobalString("false");
                args.push_back(builder_.CreateSelect(v.value, true_str, false_str));
                break;
            }
        }
    }
    format += '\n';
    args[0] = builder_.CreateGlobalString(format);
    builder_.CreateCall(printf_fn_, args);
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
                   var->type};
}

void CodeGenerator::visitAssign(const AssignExpr& expr) {
    const std::string name(expr.name().lexeme());
    CGVar* var = lookup_var(name);
    if (!var) {
        unsupported("assignment to undeclared '" + name + "'",
                    expr.name().line(), expr.name().column());
    }
    CGValue v = emit(expr.value());
    llvm::Value* stored = coerce_for_slot(v, var->type, expr.name());
    builder_.CreateStore(stored, var->slot);
    // 赋值表达式的值 = 存入后的值（与解释器一致）
    last_value_ = {stored, var->type};
}

void CodeGenerator::visitTuple(const TupleExpr&) { unsupported("tuple", 0, 0); }
void CodeGenerator::visitTernary(const TernaryExpr& expr) { gen_ternary(expr); }
void CodeGenerator::visitMultiMatch(const MultiMatchExpr&) { unsupported("'==?'", 0, 0); }
void CodeGenerator::visitArrayLiteral(const ArrayLiteralExpr&) { unsupported("array literal", 0, 0); }
void CodeGenerator::visitIndex(const IndexExpr&) { unsupported("indexing", 0, 0); }
void CodeGenerator::visitIndexAssign(const IndexAssignExpr&) { unsupported("index assignment", 0, 0); }
void CodeGenerator::visitMethodCall(const MethodCallExpr&) { unsupported("method call", 0, 0); }
void CodeGenerator::visitProperty(const PropertyExpr&) { unsupported("property access", 0, 0); }
void CodeGenerator::visitPropertyAssign(const PropertyAssignExpr&) { unsupported("property assignment", 0, 0); }
void CodeGenerator::visitNew(const NewExpr&) { unsupported("'new'", 0, 0); }
void CodeGenerator::visitThis(const ThisExpr&) { unsupported("'this'", 0, 0); }
void CodeGenerator::visitBaseCall(const BaseCallExpr&) { unsupported("'base' call", 0, 0); }
void CodeGenerator::visitBaseMethodCall(const BaseMethodCallExpr&) { unsupported("'base' method call", 0, 0); }

void CodeGenerator::visitVarDecl(const VarDeclStmt& stmt) {
    // 无初始化时解释器绑 none（动态哨兵值），静态降级无对应表示，明确拒编
    if (!stmt.initializer()) {
        unsupported("variable declaration without initializer",
                    stmt.name().line(), stmt.name().column());
    }
    CGType type = declared_cgtype(stmt.type());
    CGValue init = emit(stmt.initializer());
    llvm::Value* stored = coerce_for_slot(init, type, stmt.name());
    const std::string name(stmt.name().lexeme());
    llvm::AllocaInst* slot = create_entry_alloca(llvm_type_of(type), name);
    builder_.CreateStore(stored, slot);
    scopes_.back()[name] = {slot, type}; // 同名直接遮蔽（重复声明由语义层拦截）
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
void CodeGenerator::visitFunction(const FunctionStmt&) { unsupported("function declaration", 0, 0); }
void CodeGenerator::visitReturn(const ReturnStmt&) { unsupported("'return'", 0, 0); }
void CodeGenerator::visitClass(const ClassStmt&) { unsupported("'class'", 0, 0); }

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
    if (tv.type == ev.type) {
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
    last_value_ = {phi, result_type};
}

llvm::Value* CodeGenerator::to_double(const CGValue& v) {
    switch (v.type) {
        case CGType::Double: return v.value;
        case CGType::Int:    return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
        case CGType::Bool:   return builder_.CreateUIToFP(v.value, builder_.getDoubleTy());
        default:             unsupported("numeric conversion of non-numeric value", 0, 0);
    }
}

} // namespace collie
