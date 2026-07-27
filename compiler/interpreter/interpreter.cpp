/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2026-07-25
 * @Description: 树遍历解释器（路线 A）的实现
 */
#include "interpreter.h"

#include <algorithm>
#include <cctype>
#include <limits>
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
            // 特殊数值字面量（词法层把 Infinity/NaN 归为 LITERAL_NUMBER）；
            // 不依赖 std::stod 对 "Infinity"/"NaN" 拼写的平台行为，显式特判
            if (lexeme == "Infinity") {
                result_ = Value::number(std::numeric_limits<double>::infinity());
            } else if (lexeme == "NaN") {
                result_ = Value::number(std::numeric_limits<double>::quiet_NaN());
            } else {
                // 词法器保证 lexeme 是合法数字串
                result_ = Value::number(std::stod(lexeme));
            }
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
    // 按变量声明类型校验/隐式转换（未声明时返回 KW_OBJECT 放行，交给下方报 undefined）
    value = coerce_to_declared(env_.declared_type(std::string(name.lexeme())),
                               value, name.line(), name.column());
    if (!env_.assign(std::string(name.lexeme()), value)) {
        throw RuntimeError("Assignment to undefined variable '" +
                               std::string(name.lexeme()) + "'",
                           name.line(), name.column());
    }
    result_ = value;  // 赋值表达式的值为所赋的值
}

void Interpreter::visitCall(const CallExpr& expr) {
    // 内建函数 print / len / toString / toNumber
    const IdentifierExpr* callee = dynamic_cast<const IdentifierExpr*>(expr.callee());
    if (callee && callee->name().lexeme() == "print") {
        call_builtin_print(expr);
        return;
    }
    if (callee && callee->name().lexeme() == "len") {
        call_builtin_len(expr);
        return;
    }
    if (callee && callee->name().lexeme() == "toString") {
        call_builtin_to_string(expr);
        return;
    }
    if (callee && callee->name().lexeme() == "toNumber") {
        call_builtin_to_number(expr);
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

    // 参数数量检查（语义层已验证，此为解释器兜底保护）
    if (args.size() != fn->parameters().size()) {
        throw RuntimeError("Expected " + std::to_string(fn->parameters().size()) +
                               " arguments but got " + std::to_string(args.size()),
                           expr.paren().line(), expr.paren().column());
    }

    // 创建函数作用域并绑定形参（按形参声明类型校验/隐式转换）
    ScopeGuard guard(env_);
    for (size_t i = 0; i < fn->parameters().size(); ++i) {
        const Parameter& param = fn->parameters()[i];
        Value bound = coerce_to_declared(param.type.type(), args[i],
                                         param.name.line(), param.name.column());
        env_.define(std::string(param.name.lexeme()), bound, false,
                    param.type.type());
    }

    // 执行函数体，捕获 ReturnSignal（返回值按声明返回类型校验/隐式转换）
    try {
        for (const auto& stmt : fn->body()->statements()) {
            execute(stmt.get());
        }
        // 无显式 return —— 返回 none
        result_ = Value::none();
    } catch (const ReturnSignal& ret) {
        result_ = coerce_to_declared(fn->return_type().type(), ret.value,
                                     fn->return_type().line(),
                                     fn->return_type().column());
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

void Interpreter::call_builtin_len(const CallExpr& expr) {
    // len(string)：返回 UTF-8 码点数（而非字节数）；len(array)：返回元素个数。
    const auto& args = expr.arguments();
    if (args.size() != 1) {
        throw RuntimeError("len() expects exactly 1 argument",
                           expr.paren().line(), expr.paren().column());
    }
    Value v = evaluate(args[0].get());
    if (v.is_array()) {
        result_ = Value::number(static_cast<double>(v.as_array().size()));
        return;
    }
    if (!v.is_string()) {
        throw RuntimeError(std::string("len() expects a string or array, got ") + v.kind_name(),
                           expr.paren().line(), expr.paren().column());
    }
    result_ = Value::number(static_cast<double>(utf8_length(v.as_string())));
}

void Interpreter::call_builtin_to_string(const CallExpr& expr) {
    // toString(any)：任意值转字符串（与 print 的文本表示一致）。
    const auto& args = expr.arguments();
    if (args.size() != 1) {
        throw RuntimeError("toString() expects exactly 1 argument",
                           expr.paren().line(), expr.paren().column());
    }
    result_ = Value::str(evaluate(args[0].get()).to_string());
}

void Interpreter::call_builtin_to_number(const CallExpr& expr) {
    // toNumber(string|bool|number)：转数字；无法解析的字符串报运行时错误。
    const auto& args = expr.arguments();
    if (args.size() != 1) {
        throw RuntimeError("toNumber() expects exactly 1 argument",
                           expr.paren().line(), expr.paren().column());
    }
    result_ = to_number_value(evaluate(args[0].get()),
                              expr.paren().line(), expr.paren().column());
}

Value Interpreter::coerce_to_declared(TokenType declared, const Value& value,
                                      size_t line, size_t column) {
    switch (declared) {
        case TokenType::KW_NUMBER:
            if (!value.is_number()) {
                throw RuntimeError(std::string("Type mismatch: cannot assign ") +
                                       value.kind_name() + " to 'number' variable",
                                   line, column);
            }
            return value;
        case TokenType::KW_BOOL:
            if (!value.is_bool()) {
                throw RuntimeError(std::string("Type mismatch: cannot assign ") +
                                       value.kind_name() + " to 'bool' variable",
                                   line, column);
            }
            return value;
        case TokenType::KW_ARRAY:
            if (!value.is_array()) {
                throw RuntimeError(std::string("Type mismatch: cannot assign ") +
                                       value.kind_name() + " to 'array' variable",
                                   line, column);
            }
            return value;
        case TokenType::KW_STRING:
            if (value.is_string()) {
                return value;
            }
            // 语义层允许 number/bool 隐式转 string，运行期在此落地
            if (value.is_number() || value.is_bool()) {
                return Value::str(value.to_string());
            }
            throw RuntimeError(std::string("Type mismatch: cannot assign ") +
                                   value.kind_name() + " to 'string' variable",
                               line, column);
        default:
            // object/类名（IDENTIFIER）等动态类型不校验
            return value;
    }
}

Value Interpreter::to_number_value(const Value& v, size_t line, size_t column) {
    if (v.is_number()) {
        return v;
    }
    if (v.is_bool()) {
        return Value::number(v.as_bool() ? 1.0 : 0.0);
    }
    if (v.is_string()) {
        // 两端去空白后解析；不可解析的字符串按文档返回 NaN（见 04-numeric.md：
        // "infinity".toNumber() == NaN），不再报运行时错误。
        const std::string& raw = v.as_string();
        size_t b = 0, e = raw.size();
        while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
        std::string s = raw.substr(b, e - b);

        // 特殊形式严格大小写匹配："Infinity"/"+Infinity"/"-Infinity"
        if (s == "Infinity" || s == "+Infinity") {
            return Value::number(std::numeric_limits<double>::infinity());
        }
        if (s == "-Infinity") {
            return Value::number(-std::numeric_limits<double>::infinity());
        }
        try {
            size_t pos = 0;
            double n = std::stod(s, &pos);
            // 尾部残留字符视为非法；stod 对 "inf"/"nan" 等拼写宽松，
            // 非有限结果一律视为不可解析（如 "infinity" 应得 NaN）
            if (pos == s.size() && std::isfinite(n)) {
                return Value::number(n);
            }
        } catch (const std::exception&) {
            // fall through: 不可解析 → NaN
        }
        return Value::number(std::numeric_limits<double>::quiet_NaN());
    }
    throw RuntimeError(std::string("toNumber() cannot convert ") + v.kind_name(),
                       line, column);
}

void Interpreter::visitTuple(const TupleExpr& expr) {
    throw RuntimeError("Tuples are not supported yet", expr.paren().line(),
                       expr.paren().column());
}

void Interpreter::visitTupleMember(const TupleMemberExpr& expr) {
    throw RuntimeError("Tuple member access is not supported yet", expr.dot().line(),
                       expr.dot().column());
}

void Interpreter::visitTernary(const TernaryExpr& expr) {
    Value cond = evaluate(expr.condition());
    if (cond.is_truthy()) {
        result_ = evaluate(expr.then_expr());
    } else {
        result_ = evaluate(expr.else_expr());
    }
}

void Interpreter::visitArrayLiteral(const ArrayLiteralExpr& expr) {
    Value::ArrayStorage elements;
    elements.reserve(expr.elements().size());
    for (const auto& element : expr.elements()) {
        elements.push_back(evaluate(element.get()));
    }
    result_ = Value::array(std::move(elements));
}

void Interpreter::visitIndex(const IndexExpr& expr) {
    Value object = evaluate(expr.object());
    Value index = evaluate(expr.index());
    if (object.is_string()) {
        // 字符串按 UTF-8 码点索引（与 len 一致），返回单字符子串
        const std::string& s = object.as_string();
        size_t i = normalize_index(index, utf8_length(s), expr.bracket());
        result_ = Value::str(utf8_char_at(s, i));
        return;
    }
    if (!object.is_array()) {
        throw RuntimeError(std::string("Only arrays and strings can be indexed, got ") +
                               object.kind_name(),
                           expr.bracket().line(), expr.bracket().column());
    }
    size_t i = normalize_index(index, object.as_array().size(), expr.bracket());
    result_ = object.as_array()[i];
}

void Interpreter::visitIndexAssign(const IndexAssignExpr& expr) {
    Value object = evaluate(expr.object());
    if (object.is_string()) {
        // 语义层已拦截静态可知的情况；这里防御 object 动态路径（如数组元素为字符串）
        throw RuntimeError("Strings are immutable, cannot assign to index",
                           expr.bracket().line(), expr.bracket().column());
    }
    if (!object.is_array()) {
        throw RuntimeError(std::string("Only arrays can be index-assigned, got ") +
                               object.kind_name(),
                           expr.bracket().line(), expr.bracket().column());
    }
    Value index = evaluate(expr.index());
    Value value = evaluate(expr.value());
    size_t i = normalize_index(index, object.as_array().size(), expr.bracket());
    // 数组为引用语义：写入共享底层存储，对所有持有者可见。
    object.as_array()[i] = value;
    result_ = value;  // 赋值表达式的值为右侧值，支持链式赋值
}

void Interpreter::visitMethodCall(const MethodCallExpr& expr) {
    Value object = evaluate(expr.object());
    const std::string name(expr.name().lexeme());
    size_t line = expr.name().line();
    size_t column = expr.name().column();

    // 类实例：沿继承链分发用户定义方法（toString 保留为通用内建兜底）
    if (object.is_instance()) {
        const ClassStmt* defining_class = nullptr;
        const FunctionStmt* method =
            find_method(object.as_instance().klass, name, &defining_class);
        if (method) {
            std::vector<Value> args;
            for (const auto& argument : expr.arguments()) {
                args.push_back(evaluate(argument.get()));
            }
            result_ = call_class_method(object, method, defining_class,
                                        args, line, column);
            return;
        }
        if (name == "toString") {
            result_ = Value::str(object.to_string());
            return;
        }
        throw RuntimeError("Undefined method '" + name + "' on object",
                           line, column);
    }

    // 除 subString 外内建方法均为 0 参（语义层已校验，这里防御 object 动态路径）
    if (name != "subString" && !expr.arguments().empty()) {
        throw RuntimeError(name + "() expects no arguments", line, column);
    }

    // 通用方法：与内建函数 toString/toNumber 行为一致
    if (name == "toString") {
        result_ = Value::str(object.to_string());
        return;
    }
    if (name == "toNumber") {
        result_ = to_number_value(object, line, column);
        return;
    }

    // string 专属方法（见设计文档 03-character.md）
    if (name == "trim" || name == "trimLeft" || name == "trimRight" ||
        name == "subString") {
        if (!object.is_string()) {
            throw RuntimeError("Method '" + name + "()' is only supported on strings, got " +
                                   std::string(object.kind_name()),
                               line, column);
        }
        const std::string& s = object.as_string();
        if (name == "subString") {
            // .subString(startIndex[, endIndex = length])，endIndex 传 -1/NaN 时取 length；
            // 区间为 [start, end)，按 UTF-8 码点计数，越界截断、start >= end 时为空串
            if (expr.arguments().empty() || expr.arguments().size() > 2) {
                throw RuntimeError("subString() expects 1 to 2 argument(s)", line, column);
            }
            Value start_value = evaluate(expr.arguments()[0].get());
            if (!start_value.is_number()) {
                throw RuntimeError("subString() indices must be numbers", line, column);
            }
            size_t length = utf8_length(s);
            double end_raw = static_cast<double>(length);
            if (expr.arguments().size() == 2) {
                Value end_value = evaluate(expr.arguments()[1].get());
                if (!end_value.is_number()) {
                    throw RuntimeError("subString() indices must be numbers", line, column);
                }
                end_raw = end_value.as_number();
                if (std::isnan(end_raw) || end_raw == -1.0) {
                    end_raw = static_cast<double>(length);
                }
            }
            double start_raw = start_value.as_number();
            if (std::isnan(start_raw)) { start_raw = 0.0; }
            double len_d = static_cast<double>(length);
            double start_d = std::min(std::max(std::floor(start_raw), 0.0), len_d);
            double end_d = std::min(std::max(std::floor(end_raw), 0.0), len_d);
            if (start_d >= end_d) {
                result_ = Value::str("");
                return;
            }
            size_t from = utf8_byte_offset(s, static_cast<size_t>(start_d));
            size_t to = utf8_byte_offset(s, static_cast<size_t>(end_d));
            result_ = Value::str(s.substr(from, to - from));
            return;
        }
        // trim 系列：空白字符为空格与 Tab 制表符（见 03-character.md）
        auto is_blank = [](char c) { return c == ' ' || c == '\t'; };
        size_t begin = 0;
        size_t end = s.size();
        if (name != "trimRight") {
            while (begin < end && is_blank(s[begin])) { ++begin; }
        }
        if (name != "trimLeft") {
            while (end > begin && is_blank(s[end - 1])) { --end; }
        }
        result_ = Value::str(s.substr(begin, end - begin));
        return;
    }

    // number 专属方法（见设计文档 04-numeric.md）
    if (!object.is_number()) {
        throw RuntimeError("Method '" + name + "()' is only supported on numbers, got " +
                               std::string(object.kind_name()),
                           line, column);
    }
    double a = object.as_number();
    if (name == "abs") {
        result_ = Value::number(std::fabs(a));
    } else if (name == "integerPart") {
        // 向零取整：-123.456.integerPart() == -123
        result_ = Value::number(std::trunc(a));
    } else if (name == "decimalPart") {
        // 保留符号：-123.456.decimalPart() == -0.456
        result_ = Value::number(a - std::trunc(a));
    } else if (name == "isInteger") {
        result_ = Value::boolean(std::isfinite(a) && a == std::floor(a));
    } else if (name == "isDecimal") {
        // 文档规定 Infinity/NaN 的 isInteger/isDecimal 均为 false
        result_ = Value::boolean(std::isfinite(a) && a != std::floor(a));
    } else if (name == "isNaN") {
        result_ = Value::boolean(std::isnan(a));
    } else if (name == "isInfinity") {
        result_ = Value::boolean(std::isinf(a));
    } else if (name == "isFinite") {
        result_ = Value::boolean(std::isfinite(a));
    } else if (name == "isPositive") {
        result_ = Value::boolean(a > 0.0);   // NaN 与 0 均为 false
    } else if (name == "isNegative") {
        result_ = Value::boolean(a < 0.0);
    } else {
        throw RuntimeError("Unknown method '" + name + "'", line, column);
    }
}

void Interpreter::visitProperty(const PropertyExpr& expr) {
    Value object = evaluate(expr.object());
    const std::string name(expr.name().lexeme());
    size_t line = expr.name().line();
    size_t column = expr.name().column();

    // 类实例：读取字段（字段需在类体中声明，未声明即报错）
    if (object.is_instance()) {
        auto& fields = object.as_instance().fields;
        auto it = fields.find(name);
        if (it == fields.end()) {
            throw RuntimeError("Undefined property '" + name + "' on object",
                               line, column);
        }
        result_ = it->second;
        return;
    }

    // 内建属性（见设计文档 03-character.md）：string 按 UTF-8 码点计数
    if (name == "length") {
        if (object.is_string()) {
            result_ = Value::number(
                static_cast<double>(utf8_length(object.as_string())));
            return;
        }
        if (object.is_array()) {
            result_ = Value::number(
                static_cast<double>(object.as_array().size()));
            return;
        }
        throw RuntimeError(
            std::string("Property 'length' is only supported on strings and arrays, got ") +
                object.kind_name(),
            line, column);
    }
    throw RuntimeError("Unknown property '" + name + "'", line, column);
}

size_t Interpreter::normalize_index(const Value& index, size_t size,
                                    const Token& bracket) {
    if (!index.is_number()) {
        throw RuntimeError(std::string("Index must be a number, got ") +
                               index.kind_name(),
                           bracket.line(), bracket.column());
    }
    double raw = index.as_number();
    if (raw != std::floor(raw)) {
        throw RuntimeError("Index must be an integer",
                           bracket.line(), bracket.column());
    }
    long long i = static_cast<long long>(raw);
    // 负索引：-1 表示最后一个元素
    if (i < 0) {
        i += static_cast<long long>(size);
    }
    if (i < 0 || i >= static_cast<long long>(size)) {
        throw RuntimeError("Index " + index.to_string() +
                               " out of range (size " + std::to_string(size) + ")",
                           bracket.line(), bracket.column());
    }
    return static_cast<size_t>(i);
}

namespace {
/// UTF-8 首字节 → 码点字节长度（非法字节按 1 处理，防御性前进）
size_t utf8_char_length(unsigned char c) {
    if ((c & 0x80) == 0)    return 1;  // ASCII
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
}  // namespace

size_t Interpreter::utf8_length(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ) {
        i += utf8_char_length(static_cast<unsigned char>(s[i]));
        ++count;
    }
    return count;
}

std::string Interpreter::utf8_char_at(const std::string& s, size_t index) {
    size_t byte = 0;
    for (size_t seen = 0; byte < s.size(); ++seen) {
        size_t char_len = utf8_char_length(static_cast<unsigned char>(s[byte]));
        if (seen == index) {
            return s.substr(byte, char_len);
        }
        byte += char_len;
    }
    return "";  // 前置条件保证不到达（index < utf8_length(s)）
}

size_t Interpreter::utf8_byte_offset(const std::string& s, size_t index) {
    size_t byte = 0;
    for (size_t seen = 0; byte < s.size() && seen < index; ++seen) {
        byte += utf8_char_length(static_cast<unsigned char>(s[byte]));
    }
    return byte;
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
            // 除零遵循 IEEE 754（与 t31 的 Infinity/NaN 语义衔接，经作者确认）：
            // 1/0 → +Infinity、-1/0 → -Infinity、0/0 → NaN
            return Value::number(a / b);
        case TokenType::OP_MODULO: {
            // 取模除数为 0 同样遵循 IEEE 754：fmod(x, 0) → NaN
            if (b == 0.0) {
                return Value::number(std::numeric_limits<double>::quiet_NaN());
            }
            // 设计文档规定取模为 floor 语义（Python 风格）：结果符号与除数一致，
            // 如 -1 % 5 == 4、-1 % -5 == -1、1 % -5 == -4（见 04-numeric.md）
            double r = std::fmod(a, b);
            if (r != 0.0 && ((r < 0.0) != (b < 0.0))) {
                r += b;
            }
            return Value::number(r);
        }
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
        case Value::Kind::Array: {
            // 数组按元素逐个深度比较
            const auto& l = left.as_array();
            const auto& r = right.as_array();
            if (l.size() != r.size()) return false;
            for (size_t i = 0; i < l.size(); ++i) {
                if (!values_equal(l[i], r[i])) return false;
            }
            return true;
        }
        default: break;
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
    // 按声明类型校验/隐式转换初始值（object/类名等动态类型放行）；
    // 无初始化时绑定 none，不做校验（首次赋值时再检查）。
    Value value = Value::none();
    if (stmt.initializer()) {
        value = coerce_to_declared(stmt.type().type(), evaluate(stmt.initializer()),
                                   stmt.name().line(), stmt.name().column());
    }
    env_.define(std::string(stmt.name().lexeme()), value, stmt.is_const(),
                stmt.type().type());
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

void Interpreter::visitDoWhile(const DoWhileStmt& stmt) {
    do {
        try {
            execute(stmt.body());
        } catch (const BreakSignal&) {
            break;
        } catch (const ContinueSignal&) {
            continue;
        }
    } while (evaluate(stmt.condition()).is_truthy());
}

void Interpreter::visitSwitch(const SwitchStmt& stmt) {
    Value cond = evaluate(stmt.condition());

    // 遍历所有 case 分支，匹配第一个等值的分支
    const SwitchCase* default_case = nullptr;
    for (const auto& sc : stmt.cases()) {
        if (sc.is_default) {
            default_case = &sc;
            continue;
        }
        // 检查是否匹配任一值
        for (const auto& val_expr : sc.values) {
            Value val = evaluate(val_expr.get());
            if (values_equal(cond, val)) {
                execute(sc.body.get());
                return; // 匹配后不继续检查其他分支（无 fallthrough）
            }
        }
    }

    // 无匹配则执行 default
    if (default_case && default_case->body) {
        execute(default_case->body.get());
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
    // 登记类声明（持有 AST 非拥有指针，AST 生命周期覆盖解释执行期）。
    // 字段初始化与构造器执行延迟到 new 时进行。
    classes_[std::string(stmt.name().lexeme())] = &stmt;
}

void Interpreter::visitNew(const NewExpr& expr) {
    const std::string name(expr.class_name().lexeme());
    size_t line = expr.class_name().line();
    size_t column = expr.class_name().column();

    auto it = classes_.find(name);
    if (it == classes_.end()) {
        throw RuntimeError("Undefined class '" + name + "'", line, column);
    }
    const ClassStmt* klass = it->second;

    // 创建实例并初始化字段：沿继承链 base-first 执行（子类同名字段覆盖），
    // 有初始化表达式的求值后按字段声明类型校验/隐式转换，否则为 none
    std::vector<const ClassStmt*> chain;
    for (const ClassStmt* c = klass; c != nullptr; c = superclass_of(c)) {
        chain.push_back(c);
    }
    auto data = std::make_shared<InstanceData>();
    data->klass = klass;
    for (auto rit = chain.rbegin(); rit != chain.rend(); ++rit) {
        for (const auto& member : (*rit)->members()) {
            if (auto* field = dynamic_cast<const VarDeclStmt*>(member.get())) {
                Value init = Value::none();
                if (field->initializer()) {
                    init = coerce_to_declared(field->type().type(),
                                              evaluate(field->initializer()),
                                              field->name().line(),
                                              field->name().column());
                }
                data->fields[std::string(field->name().lexeme())] = init;
            }
        }
    }
    Value instance = Value::instance(std::move(data));

    // 求值构造器实参
    std::vector<Value> args;
    for (const auto& argument : expr.arguments()) {
        args.push_back(evaluate(argument.get()));
    }

    // 构造器为与类名同名的成员函数（不继承，仅在本类命中）；
    // 无构造器时要求 0 实参，且不隐式调用父类构造器
    const FunctionStmt* ctor = find_method(klass, name);
    if (ctor) {
        call_class_method(instance, ctor, klass, args, line, column);
    } else if (!args.empty()) {
        throw RuntimeError("Class '" + name + "' has no constructor but got " +
                               std::to_string(args.size()) + " argument(s)",
                           line, column);
    }

    result_ = instance;
}

void Interpreter::visitThis(const ThisExpr& expr) {
    Value* value = env_.get("this");
    if (!value) {
        throw RuntimeError("'this' can only be used inside a class method",
                           expr.keyword().line(), expr.keyword().column());
    }
    result_ = *value;
}

void Interpreter::visitBaseCall(const BaseCallExpr& expr) {
    size_t line = expr.keyword().line();
    size_t column = expr.keyword().column();

    // base 按“定义当前构造器的类”的父类解析（见 current_class_ 注释）
    if (!current_class_ || !current_class_->has_superclass()) {
        throw RuntimeError(
            "'base' requires the enclosing class to have a superclass",
            line, column);
    }
    const ClassStmt* super = superclass_of(current_class_);

    Value* self = env_.get("this");
    if (!self) {
        throw RuntimeError("'base' can only be used inside a constructor",
                           line, column);
    }
    // 拷贝一份：call_class_method 内 ScopeGuard 压栈可能重分配环境存储，
    // 直接引用 env_ 内部指针会悬空
    Value self_value = *self;

    std::vector<Value> args;
    for (const auto& argument : expr.arguments()) {
        args.push_back(evaluate(argument.get()));
    }

    // 父类构造器与父类名同名（构造器不继承，仅在父类自身命中）
    const std::string super_name(super->name().lexeme());
    const FunctionStmt* ctor = find_method(super, super_name);
    if (!ctor) {
        if (!args.empty()) {
            throw RuntimeError("Class '" + super_name +
                                   "' has no constructor but got " +
                                   std::to_string(args.size()) + " argument(s)",
                               line, column);
        }
        // 父类无构造器且 0 实参：空操作
        result_ = Value::none();
        return;
    }
    call_class_method(self_value, ctor, super, args, line, column);
    result_ = Value::none();
}

void Interpreter::visitPropertyAssign(const PropertyAssignExpr& expr) {
    Value object = evaluate(expr.object());
    const std::string name(expr.name().lexeme());
    size_t line = expr.name().line();
    size_t column = expr.name().column();

    if (!object.is_instance()) {
        throw RuntimeError(std::string("Cannot assign property on ") +
                               object.kind_name(),
                           line, column);
    }

    Value value = evaluate(expr.value());
    auto& fields = object.as_instance().fields;
    auto it = fields.find(name);
    if (it == fields.end()) {
        throw RuntimeError("Undefined property '" + name + "' on object",
                           line, column);
    }
    // 按字段声明类型校验/隐式转换
    if (const VarDeclStmt* field = find_field(object.as_instance().klass, name)) {
        value = coerce_to_declared(field->type().type(), value, line, column);
    }
    it->second = value;
    result_ = value;  // 赋值表达式的值为所赋的值
}

const FunctionStmt* Interpreter::find_method(const ClassStmt* klass,
                                             const std::string& name,
                                             const ClassStmt** defining_class) const {
    // 沿继承链自子向父查找，子类同名方法实现覆写
    for (const ClassStmt* c = klass; c != nullptr; c = superclass_of(c)) {
        for (const auto& member : c->members()) {
            if (auto* fn = dynamic_cast<const FunctionStmt*>(member.get())) {
                if (fn->name().lexeme() == name) {
                    if (defining_class) *defining_class = c;
                    return fn;
                }
            }
        }
    }
    return nullptr;
}

const VarDeclStmt* Interpreter::find_field(const ClassStmt* klass,
                                           const std::string& name) const {
    for (const ClassStmt* c = klass; c != nullptr; c = superclass_of(c)) {
        for (const auto& member : c->members()) {
            if (auto* field = dynamic_cast<const VarDeclStmt*>(member.get())) {
                if (field->name().lexeme() == name) return field;
            }
        }
    }
    return nullptr;
}

const ClassStmt* Interpreter::superclass_of(const ClassStmt* klass) const {
    if (!klass->has_superclass()) return nullptr;
    const std::string super_name(klass->superclass().lexeme());
    auto it = classes_.find(super_name);
    if (it == classes_.end()) {
        // 语义层要求父类先声明，正常不可达；防御未登记的动态路径
        throw RuntimeError("Undefined superclass '" + super_name + "'",
                           klass->superclass().line(),
                           klass->superclass().column());
    }
    return it->second;
}

Value Interpreter::call_class_method(const Value& instance,
                                     const FunctionStmt* method,
                                     const ClassStmt* defining_class,
                                     const std::vector<Value>& args,
                                     size_t line, size_t column) {
    // 元数检查（语义层对 object 动态放行，运行期是唯一门禁）
    if (args.size() != method->parameters().size()) {
        throw RuntimeError(
            std::string(method->name().lexeme()) + "() expects " +
                std::to_string(method->parameters().size()) +
                " argument(s), got " + std::to_string(args.size()),
            line, column);
    }

    // 方法作用域：绑定 this 与形参（按声明类型校验/隐式转换），捕获 ReturnSignal；
    // current_class_ 切换为定义类，供体内 base 按其父类解析（RAII 确保异常路径也还原）
    struct ClassContextGuard {
        const ClassStmt*& slot;
        const ClassStmt* prev;
        ClassContextGuard(const ClassStmt*& s, const ClassStmt* v)
            : slot(s), prev(s) { s = v; }
        ~ClassContextGuard() { slot = prev; }
    } class_guard(current_class_, defining_class);
    ScopeGuard guard(env_);
    env_.define("this", instance);
    for (size_t i = 0; i < method->parameters().size(); ++i) {
        const Parameter& param = method->parameters()[i];
        Value bound = coerce_to_declared(param.type.type(), args[i],
                                         param.name.line(), param.name.column());
        env_.define(std::string(param.name.lexeme()), bound, false,
                    param.type.type());
    }

    try {
        for (const auto& stmt : method->body()->statements()) {
            execute(stmt.get());
        }
        return Value::none();  // 无显式 return
    } catch (const ReturnSignal& ret) {
        // 返回值按声明返回类型校验/隐式转换
        return coerce_to_declared(method->return_type().type(), ret.value,
                                  method->return_type().line(),
                                  method->return_type().column());
    }
}

void Interpreter::visitBreak(const BreakStmt& /*stmt*/) {
    throw BreakSignal{};
}

void Interpreter::visitContinue(const ContinueStmt& /*stmt*/) {
    throw ContinueSignal{};
}

} // namespace collie
