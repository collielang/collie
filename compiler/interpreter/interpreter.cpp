/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2026-07-25
 * @Description: 树遍历解释器（路线 A）的实现
 */
#include "interpreter.h"

#include <string>
#include <utility>

namespace collie {

namespace {
// 循环控制流通过异常在解释器内部传递，不对外暴露。
struct BreakSignal {};
struct ContinueSignal {};

/// @brief 函数返回控制流信号，携带返回值。
struct ReturnSignal {
    Value value;
};
}  // namespace

// -----------------------------------------------------------------------------
// 顶层入口
// -----------------------------------------------------------------------------
void Interpreter::interpret(const std::vector<std::unique_ptr<Stmt>>& statements) {
    for (const auto& stmt : statements) {
        execute(stmt.get());
    }
}

Value Interpreter::evaluate(const Expr* expr) {
    expr->accept(*this);
    return result_;
}

void Interpreter::execute(const Stmt* stmt) {
    stmt->accept(*this);
}

void Interpreter::execute_block(const BlockStmt& block) {
    ScopeGuard guard(env_);
    for (const auto& stmt : block.statements()) {
        execute(stmt.get());
    }
}

// -----------------------------------------------------------------------------
// 表达式
// -----------------------------------------------------------------------------
void Interpreter::visitLiteral(const LiteralExpr& expr) {
    const Token& tok = expr.token();
    std::string lexeme(tok.lexeme());
    switch (tok.type()) {
        case TokenType::LITERAL_NUMBER:
            // 词法器保证 lexeme 是合法数字串
            result_ = Value::number(std::stod(lexeme));
            break;
        case TokenType::LITERAL_STRING:
        case TokenType::LITERAL_CHAR:
        case TokenType::LITERAL_CHARACTER:
            // 词法器已完成转义解码，lexeme 即为字符串内容
            result_ = Value::str(std::move(lexeme));
            break;
        case TokenType::KW_TRUE:
            result_ = Value::boolean(true);
            break;
        case TokenType::KW_FALSE:
            result_ = Value::boolean(false);
            break;
        case TokenType::LITERAL_BOOL:
            result_ = Value::boolean(lexeme == "true");
            break;
        case TokenType::KW_NULL:
        case TokenType::KW_NONE:
            result_ = Value::none();
            break;
        default:
            throw RuntimeError("Unsupported literal", tok.line(), tok.column());
    }
}

void Interpreter::visitIdentifier(const IdentifierExpr& expr) {
    const Token& name = expr.name();
    Value* v = env_.get(std::string(name.lexeme()));
    if (!v) {
        // 语义分析通过后一般不会到这里，作为解释器的兜底保护
        throw RuntimeError("Undefined variable '" + std::string(name.lexeme()) + "'",
                           name.line(), name.column());
    }
    result_ = *v;
}

void Interpreter::visitBinary(const BinaryExpr& expr) {
    const Token& op = expr.op();

    // 逻辑运算需要短路求值：先算左侧，必要时才算右侧。
    if (op.type() == TokenType::OP_AND) {
        Value left = evaluate(expr.left());
        if (!left.is_truthy()) {
            result_ = Value::boolean(false);
            return;
        }
        result_ = Value::boolean(evaluate(expr.right()).is_truthy());
        return;
    }
    if (op.type() == TokenType::OP_OR) {
        Value left = evaluate(expr.left());
        if (left.is_truthy()) {
            result_ = Value::boolean(true);
            return;
        }
        result_ = Value::boolean(evaluate(expr.right()).is_truthy());
        return;
    }

    Value left = evaluate(expr.left());
    Value right = evaluate(expr.right());
    result_ = eval_binary(op, left, right);
}

void Interpreter::visitUnary(const UnaryExpr& expr) {
    const Token& op = expr.op();
    Value operand = evaluate(expr.operand());
    switch (op.type()) {
        case TokenType::OP_MINUS:
            if (!operand.is_number()) {
                throw RuntimeError("Unary '-' requires a number", op.line(), op.column());
            }
            result_ = Value::number(-operand.as_number());
            break;
        case TokenType::OP_NOT:
            result_ = Value::boolean(!operand.is_truthy());
            break;
        default:
            throw RuntimeError("Unsupported unary operator", op.line(), op.column());
    }
}

void Interpreter::visitAssign(const AssignExpr& expr) {
    const Token& name = expr.name();
    // const 保护：禁止对常量重新赋值
    if (env_.is_const(std::string(name.lexeme()))) {
        throw RuntimeError("Cannot assign to constant '" +
                               std::string(name.lexeme()) + "'",
                           name.line(), name.column());
    }
    Value value = evaluate(expr.value());
    if (!env_.assign(std::string(name.lexeme()), value)) {
        throw RuntimeError("Assignment to undefined variable '" +
                               std::string(name.lexeme()) + "'",
                           name.line(), name.column());
    }
    result_ = value;  // 赋值表达式的值为所赋的值
}

void Interpreter::visitCall(const CallExpr& expr) {
    // 内建函数 print
    const IdentifierExpr* callee = dynamic_cast<const IdentifierExpr*>(expr.callee());
    if (callee && callee->name().lexeme() == "print") {
        call_builtin_print(expr);
        return;
    }

    // 用户自定义函数调用
    Value callee_val = evaluate(expr.callee());
    if (!callee_val.is_function()) {
        std::string name = callee ? std::string(callee->name().lexeme()) : "<expr>";
        throw RuntimeError("'" + name + "' is not a function",
                           expr.paren().line(), expr.paren().column());
    }

    const FunctionStmt* fn = callee_val.as_function();

    // 求值实参
    std::vector<Value> args;
    for (const auto& arg : expr.arguments()) {
        args.push_back(evaluate(arg.get()));
    }

    // 参数数量检查（语义层已验证，此为解释器兆底保护）
    if (args.size() != fn->parameters().size()) {
        throw RuntimeError("Expected " + std::to_string(fn->parameters().size()) +
                               " arguments but got " + std::to_string(args.size()),
                           expr.paren().line(), expr.paren().column());
    }

    // 创建函数作用域并绑定形参
    ScopeGuard guard(env_);
    for (size_t i = 0; i < fn->parameters().size(); ++i) {
        env_.define(std::string(fn->parameters()[i].name.lexeme()), args[i]);
    }

    // 执行函数体，捕获 ReturnSignal
    try {
        for (const auto& stmt : fn->body()->statements()) {
            execute(stmt.get());
        }
        // 无显式 return —— 返回 none
        result_ = Value::none();
    } catch (const ReturnSignal& ret) {
        result_ = ret.value;
    }
}

void Interpreter::call_builtin_print(const CallExpr& expr) {
    // print(a, b, ...)：各参数以单个空格分隔，末尾换行。
    const auto& args = expr.arguments();
    std::string line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) line += ' ';
        line += evaluate(args[i].get()).to_string();
    }
    out_ << line << '\n';
    result_ = Value::none();
}

void Interpreter::visitTuple(const TupleExpr& expr) {
    throw RuntimeError("Tuples are not supported yet", expr.paren().line(),
                       expr.paren().column());
}

void Interpreter::visitTupleMember(const TupleMemberExpr& expr) {
    throw RuntimeError("Tuple member access is not supported yet", expr.dot().line(),
                       expr.dot().column());
}

// -----------------------------------------------------------------------------
// 二元运算
// -----------------------------------------------------------------------------
Value Interpreter::eval_binary(const Token& op, const Value& left, const Value& right) {
    switch (op.type()) {
        case TokenType::OP_PLUS:
        case TokenType::OP_MINUS:
        case TokenType::OP_MULTIPLY:
        case TokenType::OP_DIVIDE:
        case TokenType::OP_MODULO:
            return eval_arithmetic(op, left, right);

        case TokenType::OP_EQUAL:
            return Value::boolean(values_equal(left, right));
        case TokenType::OP_NOT_EQUAL:
            return Value::boolean(!values_equal(left, right));

        case TokenType::OP_GREATER:
        case TokenType::OP_LESS:
        case TokenType::OP_GREATER_EQ:
        case TokenType::OP_LESS_EQ:
            return eval_comparison(op, left, right);

        default:
            throw RuntimeError("Unsupported binary operator", op.line(), op.column());
    }
}

Value Interpreter::eval_arithmetic(const Token& op, const Value& left, const Value& right) {
    // '+' 在任一侧为字符串时表示拼接
    if (op.type() == TokenType::OP_PLUS &&
        (left.is_string() || right.is_string())) {
        return Value::str(left.to_string() + right.to_string());
    }

    if (!left.is_number() || !right.is_number()) {
        throw RuntimeError("Arithmetic operands must be numbers", op.line(), op.column());
    }

    double a = left.as_number();
    double b = right.as_number();
    switch (op.type()) {
        case TokenType::OP_PLUS:     return Value::number(a + b);
        case TokenType::OP_MINUS:    return Value::number(a - b);
        case TokenType::OP_MULTIPLY: return Value::number(a * b);
        case TokenType::OP_DIVIDE:
            if (b == 0.0) {
                throw RuntimeError("Division by zero", op.line(), op.column());
            }
            return Value::number(a / b);
        case TokenType::OP_MODULO:
            if (b == 0.0) {
                throw RuntimeError("Modulo by zero", op.line(), op.column());
            }
            // TODO(interpreter): 目前用 fmod 处理，未来区分整型取模语义
            return Value::number(std::fmod(a, b));
        default:
            throw RuntimeError("Unsupported arithmetic operator", op.line(), op.column());
    }
}

Value Interpreter::eval_comparison(const Token& op, const Value& left, const Value& right) {
    // 数字比较
    if (left.is_number() && right.is_number()) {
        double a = left.as_number();
        double b = right.as_number();
        switch (op.type()) {
            case TokenType::OP_GREATER:    return Value::boolean(a > b);
            case TokenType::OP_LESS:       return Value::boolean(a < b);
            case TokenType::OP_GREATER_EQ: return Value::boolean(a >= b);
            case TokenType::OP_LESS_EQ:    return Value::boolean(a <= b);
            default: break;
        }
    }
    // 字符串按字典序比较
    if (left.is_string() && right.is_string()) {
        const std::string& a = left.as_string();
        const std::string& b = right.as_string();
        switch (op.type()) {
            case TokenType::OP_GREATER:    return Value::boolean(a > b);
            case TokenType::OP_LESS:       return Value::boolean(a < b);
            case TokenType::OP_GREATER_EQ: return Value::boolean(a >= b);
            case TokenType::OP_LESS_EQ:    return Value::boolean(a <= b);
            default: break;
        }
    }
    throw RuntimeError("Comparison operands must be both numbers or both strings",
                       op.line(), op.column());
}

bool Interpreter::values_equal(const Value& left, const Value& right) {
    if (left.kind() != right.kind()) return false;
    switch (left.kind()) {
        case Value::Kind::None:   return true;
        case Value::Kind::Bool:   return left.as_bool() == right.as_bool();
        case Value::Kind::Number: return left.as_number() == right.as_number();
        case Value::Kind::String: return left.as_string() == right.as_string();
    }
    return false;
}

// -----------------------------------------------------------------------------
// 语句
// -----------------------------------------------------------------------------
void Interpreter::visitExpression(const ExpressionStmt& stmt) {
    evaluate(stmt.expression());
}

void Interpreter::visitVarDecl(const VarDeclStmt& stmt) {
    // TODO(interpreter): 依据声明类型做类型检查/隐式转换，目前按动态类型直接绑定初始值。
    Value value = stmt.initializer() ? evaluate(stmt.initializer()) : Value::none();
    env_.define(std::string(stmt.name().lexeme()), value, stmt.is_const());
}

void Interpreter::visitBlock(const BlockStmt& stmt) {
    execute_block(stmt);
}

void Interpreter::visitIf(const IfStmt& stmt) {
    if (evaluate(stmt.condition()).is_truthy()) {
        execute(stmt.then_branch());
    } else if (stmt.else_branch()) {
        execute(stmt.else_branch());
    }
}

void Interpreter::visitWhile(const WhileStmt& stmt) {
    while (evaluate(stmt.condition()).is_truthy()) {
        try {
            execute(stmt.body());
        } catch (const BreakSignal&) {
            break;
        } catch (const ContinueSignal&) {
            continue;
        }
    }
}

void Interpreter::visitFor(const ForStmt& stmt) {
    // for 的初始化变量作用域限定在循环内部
    ScopeGuard guard(env_);
    if (stmt.initializer()) {
        execute(stmt.initializer());
    }
    while (stmt.condition() == nullptr || evaluate(stmt.condition()).is_truthy()) {
        try {
            execute(stmt.body());
        } catch (const BreakSignal&) {
            break;
        } catch (const ContinueSignal&) {
            // 落到下方执行 increment 后继续
        }
        if (stmt.increment()) {
            evaluate(stmt.increment());
        }
    }
}

void Interpreter::visitFunction(const FunctionStmt& stmt) {
    // 将函数声明登记到当前作用域（与变量同层存储）。
    // 函数值持有 FunctionStmt 的非拥有指针（AST 生命周期覆盖解释执行期）。
    env_.define(std::string(stmt.name().lexeme()), Value::function(&stmt));
}

void Interpreter::visitReturn(const ReturnStmt& stmt) {
    // 求值 return 表达式（若无表达式则返回 none），通过内部信号传播回 visitCall。
    Value val = stmt.value() ? evaluate(stmt.value()) : Value::none();
    throw ReturnSignal{val};
}

void Interpreter::visitClass(const ClassStmt& stmt) {
    throw RuntimeError("Classes are not supported yet", stmt.name().line(),
                       stmt.name().column());
}

void Interpreter::visitBreak(const BreakStmt& /*stmt*/) {
    throw BreakSignal{};
}

void Interpreter::visitContinue(const ContinueStmt& /*stmt*/) {
    throw ContinueSignal{};
}

} // namespace collie
