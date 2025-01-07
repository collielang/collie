/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "semantic_analyzer.h"

namespace collie {

void SemanticAnalyzer::analyze(const std::vector<std::unique_ptr<Stmt>>& statements) {
    // 分析每个顶层语句
    for (const auto& stmt : statements) {
        stmt->accept(*this);
    }
}

// 表达式访问方法实现
void SemanticAnalyzer::visitLiteral(const LiteralExpr& expr) {
    // 根据字面量的token类型设置当前类型
    switch (expr.token().type()) {
        case TokenType::LITERAL_NUMBER:
            current_type_ = TokenType::KW_NUMBER;
            break;
        case TokenType::LITERAL_STRING:
            current_type_ = TokenType::KW_STRING;
            break;
        case TokenType::LITERAL_CHAR:
        case TokenType::LITERAL_CHARACTER:
            current_type_ = TokenType::KW_CHAR;
            break;
        case TokenType::LITERAL_BOOL:
            current_type_ = TokenType::KW_BOOL;
            break;
        default: {
            std::string message = "Invalid literal type";
            throw SemanticError(message, expr.token().line(), expr.token().column());
        }
    }
}

void SemanticAnalyzer::visitIdentifier(const IdentifierExpr& expr) {
    std::string name(expr.name().lexeme());
    Symbol* symbol = symbols_.resolve(name);
    if (!symbol) {
        std::string message = std::string("Undefined variable '") + name + "'";
        throw SemanticError(message, expr.name().line(), expr.name().column());
    }

    if (symbol->kind == SymbolKind::VARIABLE && !symbol->is_initialized) {
        std::string message = std::string("Variable '") + name +
                            "' is used before initialization";
        throw SemanticError(message, expr.name().line(), expr.name().column());
    }

    current_type_ = symbol->type.type();
}

void SemanticAnalyzer::visitBinary(const BinaryExpr& expr) {
    // 分析左右操作数
    expr.left()->accept(*this);
    TokenType left_type = current_type_;

    expr.right()->accept(*this);
    TokenType right_type = current_type_;

    // 检查操作符类型兼容性
    switch (expr.op().type()) {
        case TokenType::OP_PLUS:
            // 字符串连接
            if (left_type == TokenType::KW_STRING || right_type == TokenType::KW_STRING) {
                if (!is_string_convertible(left_type) || !is_string_convertible(right_type)) {
                    throw SemanticError("Invalid operands for string concatenation",
                        expr.op().line(), expr.op().column());
                }
                current_type_ = TokenType::KW_STRING;
                return;
            }
            [[fallthrough]];
        case TokenType::OP_MINUS:
        case TokenType::OP_MULTIPLY:
        case TokenType::OP_DIVIDE:
        case TokenType::OP_MODULO:
            // 数值运算
            if (!is_numeric_convertible(left_type) || !is_numeric_convertible(right_type)) {
                throw SemanticError("Numeric operands expected for arithmetic operation",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = common_type(left_type, right_type);
            break;

        // 比较运算符
        case TokenType::OP_EQUAL:
        case TokenType::OP_NOT_EQUAL:
            if (!is_comparable_type(left_type, right_type)) {
                throw SemanticError("Incomparable operand types",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = TokenType::KW_BOOL;
            break;

        case TokenType::OP_GREATER:
        case TokenType::OP_GREATER_EQ:
        case TokenType::OP_LESS:
        case TokenType::OP_LESS_EQ:
            if (!is_ordered_type(left_type) || !is_ordered_type(right_type) ||
                !is_compatible_type(left_type, right_type)) {
                throw SemanticError("Invalid operands for comparison",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = TokenType::KW_BOOL;
            break;

        // 逻辑运算符
        case TokenType::OP_AND:
        case TokenType::OP_OR:
            if (left_type != TokenType::KW_BOOL || right_type != TokenType::KW_BOOL) {
                throw SemanticError("Boolean operands expected for logical operation",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = TokenType::KW_BOOL;
            break;

        // 位运算符
        case TokenType::OP_BIT_AND:
        case TokenType::OP_BIT_OR:
        case TokenType::OP_BIT_XOR:
            if (!is_bit_type(left_type) || !is_bit_type(right_type) ||
                !is_compatible_type(left_type, right_type)) {
                throw SemanticError("Bit operands expected for bitwise operation",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = left_type;  // 保持位运算的原始类型
            break;

        case TokenType::OP_BIT_LSHIFT:
        case TokenType::OP_BIT_RSHIFT:
            if (!is_bit_type(left_type)) {
                throw SemanticError("Left operand must be a bit type",
                    expr.op().line(), expr.op().column());
            }
            if (!is_numeric_type(right_type)) {
                throw SemanticError("Right operand must be a number",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = left_type;
            break;

        default:
            throw SemanticError("Unknown binary operator",
                expr.op().line(), expr.op().column());
    }
}

// 语句访问方法实现
void SemanticAnalyzer::visitVarDecl(const VarDeclStmt& stmt) {
    std::string name(stmt.name().lexeme());

    // 检查变量是否已在当前作用域中定义
    if (symbols_.is_defined_in_current_scope(name)) {
        throw SemanticError("Variable '" + name + "' is already defined",
            stmt.name().line(), stmt.name().column());
    }

    // 检查是否是常量声明
    bool is_const = stmt.is_const();

    // 常量必须有初始化表达式
    if (is_const && !stmt.initializer()) {
        throw SemanticError("Constant '" + name + "' must be initialized",
            stmt.name().line(), stmt.name().column());
    }

    // 如果有初始化表达式，检查类型匹配
    if (stmt.initializer()) {
        stmt.initializer()->accept(*this);
        if (!is_compatible_type(stmt.type().type(), current_type_)) {
            throw SemanticError("Cannot initialize variable of type '" +
                std::string(stmt.type().lexeme()) + "' with expression of type '" +
                std::to_string(static_cast<int>(current_type_)) + "'",
                stmt.name().line(), stmt.name().column());
        }
    }

    // 创建并添加符号
    Symbol symbol{
        SymbolKind::VARIABLE,
        stmt.type(),
        stmt.name(),
        symbols_.current_scope_level(),
        stmt.initializer() != nullptr,  // 如果有初始化表达式则标记为已初始化
        is_const
    };
    symbols_.define(symbol);
}

void SemanticAnalyzer::visitBlock(const BlockStmt& stmt) {
    symbols_.begin_scope();

    for (const auto& statement : stmt.statements()) {
        statement->accept(*this);
    }

    symbols_.end_scope();
}

void SemanticAnalyzer::visitIf(const IfStmt& stmt) {
    // 检查条件表达式
    stmt.condition()->accept(*this);
    if (current_type_ != TokenType::KW_BOOL) {
        throw SemanticError("If condition must be a boolean expression",
            stmt.if_token().line(), stmt.if_token().column());
    }

    // 分析 then 分支
    symbols_.begin_scope();
    stmt.then_branch()->accept(*this);
    symbols_.end_scope();

    // 分析 else 分支（如果存在）
    if (stmt.else_branch()) {
        symbols_.begin_scope();
        stmt.else_branch()->accept(*this);
        symbols_.end_scope();
    }
}

void SemanticAnalyzer::visitWhile(const WhileStmt& stmt) {
    // 检查条件表达式
    stmt.condition()->accept(*this);
    if (current_type_ != TokenType::KW_BOOL) {
        throw SemanticError("While condition must be a boolean expression",
            stmt.while_token().line(), stmt.while_token().column());
    }

    // 分析循环体
    symbols_.begin_scope();
    stmt.body()->accept(*this);
    symbols_.end_scope();
}

void SemanticAnalyzer::visitFor(const ForStmt& stmt) {
    symbols_.begin_scope();

    // 分析初始化语句（如果存在）
    if (stmt.initializer()) {
        stmt.initializer()->accept(*this);
    }

    // 检查条件表达式（如果存在）
    if (stmt.condition()) {
        stmt.condition()->accept(*this);
        if (current_type_ != TokenType::KW_BOOL) {
            throw SemanticError("For condition must be a boolean expression",
                stmt.for_token().line(), stmt.for_token().column());
        }
    }

    // 分析增量表达式（如果存在）
    if (stmt.increment()) {
        stmt.increment()->accept(*this);
    }

    // 分析循环体
    stmt.body()->accept(*this);

    symbols_.end_scope();
}

void SemanticAnalyzer::visitFunction(const FunctionStmt& stmt) {
    // 检查函数是否已在当前作用域中定义
    std::string name(stmt.name().lexeme());
    if (symbols_.is_defined_in_current_scope(name)) {
        throw SemanticError("Function '" + name + "' is already defined",
            stmt.name().line(), stmt.name().column());
    }

    // 创建函数符号
    Symbol function{
        SymbolKind::FUNCTION,
        stmt.return_type(),
        stmt.name(),
        symbols_.current_scope_level(),
        true  // 函数定义时就认为是已初始化的
    };

    // 保存当前函数上下文
    Symbol* previous_function = current_function_;
    current_function_ = &function;

    // 进入函数作用域
    symbols_.begin_scope();

    // 处理参数
    for (const auto& param : stmt.parameters()) {
        if (symbols_.is_defined_in_current_scope(std::string(param.name.lexeme()))) {
            throw SemanticError("Duplicate parameter name '" +
                std::string(param.name.lexeme()) + "'",
                param.name.line(), param.name.column());
        }

        Symbol parameter{
            SymbolKind::PARAMETER,
            param.type,
            param.name,
            symbols_.current_scope_level(),
            true  // 参数在函数调用时会被初始化
        };

        symbols_.define(parameter);
        function.parameters.push_back(parameter);
    }

    // 分析函数体
    stmt.body()->accept(*this);

    // 退出函数作用域
    symbols_.end_scope();

    // 恢复之前的函数上下文
    current_function_ = previous_function;

    // 在当前作用域中定义函数
    symbols_.define(function);
}

void SemanticAnalyzer::visitCall(const CallExpr& expr) {
    // 先获取被调用的表达式
    const IdentifierExpr* callee = dynamic_cast<const IdentifierExpr*>(expr.callee().get());
    if (!callee) {
        std::string message = "Invalid function call target";
        throw SemanticError(message, expr.paren().line(), expr.paren().column());
    }

    // 分析被调用的表达式
    expr.callee()->accept(*this);

    std::string name(callee->name().lexeme());
    Symbol* function = symbols_.resolve(std::string(callee->name().lexeme()));
    if (!function || function->kind != SymbolKind::FUNCTION) {
        std::string message = std::string("'") + name + "' is not a function";
        throw SemanticError(message, callee->name().line(), callee->name().column());
    }

    // 检查参数数量
    if (expr.arguments().size() != function->parameters.size()) {
        std::string message = std::string("Expected ") +
                            std::to_string(function->parameters.size()) +
                            " arguments but got " +
                            std::to_string(expr.arguments().size());
        throw SemanticError(message, expr.paren().line(), expr.paren().column());
    }

    // 检查参数类型
    for (size_t i = 0; i < expr.arguments().size(); ++i) {
        expr.arguments()[i]->accept(*this);
        if (!is_compatible_type(function->parameters[i].type.type(), current_type_)) {
            std::string message = std::string("Invalid argument type for parameter '") +
                                std::string(function->parameters[i].name.lexeme()) + "'";
            throw SemanticError(message, expr.paren().line(), expr.paren().column());
        }
    }

    // 设置返回类型
    current_type_ = function->type.type();
}

void SemanticAnalyzer::visitAssign(const AssignExpr& expr) {
    std::string name(expr.name().lexeme());
    Symbol* symbol = symbols_.resolve(name);
    if (!symbol) {
        throw SemanticError("Undefined variable '" + name + "'",
            expr.name().line(), expr.name().column());
    }

    // 检查是否是常量
    if (symbol->is_const) {
        throw SemanticError("Cannot assign to constant '" + name + "'",
            expr.name().line(), expr.name().column());
    }

    if (symbol->kind != SymbolKind::VARIABLE && symbol->kind != SymbolKind::PARAMETER) {
        throw SemanticError("Cannot assign to '" + name + "'",
            expr.name().line(), expr.name().column());
    }

    expr.value()->accept(*this);
    if (!is_compatible_type(symbol->type.type(), current_type_)) {
        throw SemanticError("Cannot assign value of type '" +
            std::to_string(static_cast<int>(current_type_)) +
            "' to variable of type '" +
            std::to_string(static_cast<int>(symbol->type.type())) + "'",
            expr.name().line(), expr.name().column());
    }

    // 标记变量为已初始化和已修改
    symbol->is_initialized = true;
    symbol->is_modified = true;
    current_type_ = symbol->type.type();
}

void SemanticAnalyzer::visitUnary(const UnaryExpr& expr) {
    // 分析一元表达式的操作数
    expr.operand()->accept(*this);
    TokenType operand_type = current_type_;

    // 检查操作符类型兼容性
    switch (expr.op().type()) {
        case TokenType::OP_MINUS:
            if (!is_numeric_type(operand_type)) {
                throw SemanticError("Numeric operand expected for unary minus",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = TokenType::KW_NUMBER;
            break;

        case TokenType::OP_NOT:
            if (operand_type != TokenType::KW_BOOL) {
                throw SemanticError("Boolean operand expected for logical not",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = TokenType::KW_BOOL;
            break;

        case TokenType::OP_BIT_NOT:
            if (!is_bit_type(operand_type)) {
                throw SemanticError("Bit operand expected for bitwise not",
                    expr.op().line(), expr.op().column());
            }
            current_type_ = operand_type;
            break;

        default:
            throw SemanticError("Invalid unary operator",
                expr.op().line(), expr.op().column());
    }
}

void SemanticAnalyzer::visitExpression(const ExpressionStmt& stmt) {
    stmt.expression()->accept(*this);
}

// 添加辅助方法
bool SemanticAnalyzer::is_bit_type(TokenType type) const {
    return type == TokenType::KW_BIT ||
           type == TokenType::KW_BYTE ||
           type == TokenType::KW_WORD ||
           type == TokenType::KW_DWORD;
}

// ... 其他方法的实现 ...

// 辅助方法实现
TokenType SemanticAnalyzer::check_type(const Expr& expr) {
    expr.accept(*this);
    return current_type_;
}

bool SemanticAnalyzer::is_numeric_type(TokenType type) const {
    return type == TokenType::KW_NUMBER;
}

bool SemanticAnalyzer::is_compatible_type(TokenType left, TokenType right) const {
    if (left == right) return true;

    // 特殊情况：数字类型的兼容性
    if (is_numeric_type(left) && is_numeric_type(right)) {
        return true;
    }

    return false;
}

void SemanticAnalyzer::ensure_boolean(const Expr& expr, const std::string& context) {
    TokenType type = check_type(expr);
    if (type != TokenType::KW_BOOL) {
        // 获取表达式的位置信息
        size_t line = 0;
        size_t column = 0;

        // 根据不同的表达式类型获取位置信息
        if (auto* literal = dynamic_cast<const LiteralExpr*>(&expr)) {
            line = literal->token().line();
            column = literal->token().column();
        } else if (auto* identifier = dynamic_cast<const IdentifierExpr*>(&expr)) {
            line = identifier->name().line();
            column = identifier->name().column();
        } else if (auto* binary = dynamic_cast<const BinaryExpr*>(&expr)) {
            line = binary->op().line();
            column = binary->op().column();
        } else if (auto* unary = dynamic_cast<const UnaryExpr*>(&expr)) {
            line = unary->op().line();
            column = unary->op().column();
        }

        std::string message = "Boolean expression expected in " + context;
        throw SemanticError(message, line, column);
    }
}

void SemanticAnalyzer::visitReturn(const ReturnStmt& stmt) {
    // 检查是否在函数内部
    if (!current_function_) {
        throw SemanticError("Return statement outside function",
            stmt.keyword().line(), stmt.keyword().column());
    }

    // 检查返回值
    if (stmt.value()) {
        // 有返回值，检查类型匹配
        stmt.value()->accept(*this);
        if (!is_compatible_type(current_function_->type.type(), current_type_)) {
            std::string message = "Cannot return value of type '" +
                std::to_string(static_cast<int>(current_type_)) +
                "' from function with return type '" +
                std::to_string(static_cast<int>(current_function_->type.type())) + "'";
            throw SemanticError(message, stmt.keyword().line(), stmt.keyword().column());
        }
    } else {
        // 无返回值，检查函数是否声明为 none 返回类型
        if (current_function_->type.type() != TokenType::KW_NONE) {
            std::string message = "Function with return type '" +
                std::to_string(static_cast<int>(current_function_->type.type())) +
                "' must return a value";
            throw SemanticError(message, stmt.keyword().line(), stmt.keyword().column());
        }
    }
}

// 添加新的辅助方法
bool SemanticAnalyzer::is_string_concatenable(TokenType type) const {
    return type == TokenType::KW_STRING ||
           type == TokenType::KW_CHAR ||
           type == TokenType::KW_CHARACTER ||
           type == TokenType::KW_NUMBER ||
           type == TokenType::KW_BOOL;
}

bool SemanticAnalyzer::is_ordered_type(TokenType type) const {
    return is_numeric_type(type) ||
           type == TokenType::KW_CHAR ||
           type == TokenType::KW_CHARACTER ||
           type == TokenType::KW_STRING;
}

bool SemanticAnalyzer::is_comparable_type(TokenType left, TokenType right) const {
    // 相同类型总是可比较的
    if (left == right) return true;

    // 数值类型之间可以比较
    if (is_numeric_type(left) && is_numeric_type(right)) return true;

    // 字符类型之间可以比较
    if ((left == TokenType::KW_CHAR || left == TokenType::KW_CHARACTER) &&
        (right == TokenType::KW_CHAR || right == TokenType::KW_CHARACTER)) {
        return true;
    }

    return false;
}

bool SemanticAnalyzer::can_implicit_convert(TokenType from, TokenType to) const {
    // 相同类型可以转换
    if (from == to) return true;

    // 数值类型转换规则
    if (is_numeric_convertible(from) && is_numeric_convertible(to)) {
        // 允许从小类型到大类型的隐式转换
        if (from == TokenType::KW_BYTE && to == TokenType::KW_NUMBER) return true;
        if (from == TokenType::KW_WORD && to == TokenType::KW_NUMBER) return true;
    }

    // 字符类型转换规则
    if (from == TokenType::KW_CHAR && to == TokenType::KW_CHARACTER) return true;
    if (from == TokenType::KW_CHAR && to == TokenType::KW_STRING) return true;
    if (from == TokenType::KW_CHARACTER && to == TokenType::KW_STRING) return true;

    // 任何类型都可以转换为字符串
    if (to == TokenType::KW_STRING && is_string_convertible(from)) return true;

    return false;
}

TokenType SemanticAnalyzer::common_type(TokenType t1, TokenType t2) const {
    // 如果类型相同，直接返回
    if (t1 == t2) return t1;

    // 如果都是数值类型，返回最大的类型
    if (is_numeric_convertible(t1) && is_numeric_convertible(t2)) {
        if (t1 == TokenType::KW_NUMBER || t2 == TokenType::KW_NUMBER)
            return TokenType::KW_NUMBER;
        if (t1 == TokenType::KW_WORD || t2 == TokenType::KW_WORD)
            return TokenType::KW_WORD;
        return TokenType::KW_BYTE;
    }

    // 如果有一个是字符串类型，且另一个可以转换为字符串
    if ((t1 == TokenType::KW_STRING && is_string_convertible(t2)) ||
        (t2 == TokenType::KW_STRING && is_string_convertible(t1))) {
        return TokenType::KW_STRING;
    }

    // 如果都是字符类型，返回最大的类型
    if ((t1 == TokenType::KW_CHAR || t1 == TokenType::KW_CHARACTER) &&
        (t2 == TokenType::KW_CHAR || t2 == TokenType::KW_CHARACTER)) {
        return TokenType::KW_CHARACTER;
    }

    // 无法找到共同类型
    return TokenType::INVALID;
}

bool SemanticAnalyzer::is_numeric_convertible(TokenType type) const {
    return type == TokenType::KW_NUMBER ||
           type == TokenType::KW_BYTE ||
           type == TokenType::KW_WORD;
}

bool SemanticAnalyzer::is_string_convertible(TokenType type) const {
    return type == TokenType::KW_STRING ||
           type == TokenType::KW_CHAR ||
           type == TokenType::KW_CHARACTER ||
           type == TokenType::KW_NUMBER ||
           type == TokenType::KW_BOOL ||
           type == TokenType::KW_BYTE ||
           type == TokenType::KW_WORD;
}

} // namespace collie