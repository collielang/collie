/**
 * @file code_generator.cpp
 * @brief AST → LLVM IR 代码生成器实现（M6 t49，S1/S2 最小子集）
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
        case TokenType::LITERAL_BOOL:
            last_value_ = {builder_.getInt1(lexeme == "true"), CGType::Bool};
            return;
        default:
            unsupported("literal '" + lexeme + "'", tok.line(), tok.column());
    }
}

void CodeGenerator::visitBinary(const BinaryExpr& expr) {
    const Token& op = expr.op();
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
        default:
            unsupported("binary operator '" + std::string(op.lexeme()) + "'",
                        op.line(), op.column());
    }
}

void CodeGenerator::visitUnary(const UnaryExpr& expr) {
    const Token& op = expr.op();
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

// ---------------- 语句 ----------------

void CodeGenerator::visitExpression(const ExpressionStmt& stmt) {
    emit(stmt.expression()); // 值弃用（print 等以副作用为主）
}

// ---------------- 范围外节点：显式报错，绝不静默错编 ----------------

void CodeGenerator::visitIdentifier(const IdentifierExpr& expr) {
    unsupported("identifier '" + std::string(expr.name().lexeme()) + "'",
                expr.name().line(), expr.name().column());
}
void CodeGenerator::visitAssign(const AssignExpr&) { unsupported("assignment", 0, 0); }
void CodeGenerator::visitTuple(const TupleExpr&) { unsupported("tuple", 0, 0); }
void CodeGenerator::visitTernary(const TernaryExpr&) { unsupported("ternary", 0, 0); }
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

void CodeGenerator::visitVarDecl(const VarDeclStmt&) { unsupported("variable declaration", 0, 0); }
void CodeGenerator::visitBlock(const BlockStmt&) { unsupported("block statement", 0, 0); }
void CodeGenerator::visitIf(const IfStmt&) { unsupported("'if'", 0, 0); }
void CodeGenerator::visitWhile(const WhileStmt&) { unsupported("'while'", 0, 0); }
void CodeGenerator::visitFor(const ForStmt&) { unsupported("'for'", 0, 0); }
void CodeGenerator::visitDoWhile(const DoWhileStmt&) { unsupported("'do-while'", 0, 0); }
void CodeGenerator::visitSwitch(const SwitchStmt&) { unsupported("'switch'", 0, 0); }
void CodeGenerator::visitFunction(const FunctionStmt&) { unsupported("function declaration", 0, 0); }
void CodeGenerator::visitReturn(const ReturnStmt&) { unsupported("'return'", 0, 0); }
void CodeGenerator::visitClass(const ClassStmt&) { unsupported("'class'", 0, 0); }
void CodeGenerator::visitBreak(const BreakStmt&) { unsupported("'break'", 0, 0); }
void CodeGenerator::visitContinue(const ContinueStmt&) { unsupported("'continue'", 0, 0); }

// ---------------- 辅助 ----------------

llvm::Value* CodeGenerator::to_double(const CGValue& v) {
    switch (v.type) {
        case CGType::Double: return v.value;
        case CGType::Int:    return builder_.CreateSIToFP(v.value, builder_.getDoubleTy());
        case CGType::Bool:   return builder_.CreateUIToFP(v.value, builder_.getDoubleTy());
        default:             unsupported("numeric conversion of non-numeric value", 0, 0);
    }
}

} // namespace collie
