/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2025-01-05
 * @Description: 语义分析器的实现，负责类型检查和语义错误检测
 */
#include "semantic_analyzer.h"
#include <algorithm>
#include <cassert>
#include <sstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../utils/token_utils.h"
#include "symbol_table.h"
#include "../parser/ast.h"
#include "../lexer/token.h"

namespace collie {

// 前向声明
class Expr;
class Stmt;
struct Symbol;
class SymbolTable;

// -----------------------------------------------------------------------------
// 公共接口实现
// -----------------------------------------------------------------------------

void SemanticAnalyzer::analyze(const std::vector<std::unique_ptr<Stmt>>& statements) {
    // 清理之前的状态
    reset_state();

    // 分析每个顶层语句
    for (const auto& stmt : statements) {
        try {
            stmt->accept(*this);
        } catch (const SemanticError& error) {
            record_error(error);
            if (!in_panic_mode_) {
                enter_panic_mode();
                synchronize();
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 错误处理相关方法
// -----------------------------------------------------------------------------

void SemanticAnalyzer::record_error(const SemanticError& error) {
    errors_.push_back(error);
}

void SemanticAnalyzer::enter_panic_mode() {
    in_panic_mode_ = true;
}

void SemanticAnalyzer::exit_panic_mode() {
    in_panic_mode_ = false;
}

void SemanticAnalyzer::synchronize() {
    // 找到下一个安全点
    while (in_panic_mode_) {
        // 若没有可用的 token 序列（例如未调用 set_tokens），或已到达序列末尾，
        // 则无法继续同步，直接退出恢复模式，避免死循环。
        if (tokens_.empty() || current_token_index_ >= tokens_.size() - 1) {
            exit_panic_mode();
            return;
        }

        // 在以下位置同步：
        // 1. 语句结束（分号）
        // 2. 函数定义开始
        // 3. 变量声明开始
        // 4. 块语句开始/结束
        // 5. 控制流语句开始

        const Token& current = current_token();
        switch (current.type()) {
            case TokenType::DELIMITER_SEMICOLON:
            case TokenType::DELIMITER_RBRACE:
                exit_panic_mode();
                return;

            case TokenType::KW_FUNCTION:
            case TokenType::KW_CLASS:
            case TokenType::KW_IF:
            case TokenType::KW_WHILE:
            case TokenType::KW_FOR:
            case TokenType::KW_RETURN:
            case TokenType::KW_BREAK:
            case TokenType::KW_CONTINUE:
                if (previous_token().type() == TokenType::DELIMITER_SEMICOLON ||
                    previous_token().type() == TokenType::DELIMITER_RBRACE) {
                    exit_panic_mode();
                    return;
                }
                break;

            case TokenType::KW_NUMBER:
            case TokenType::KW_STRING:
            case TokenType::KW_BOOL:
            case TokenType::KW_CHAR:
            case TokenType::KW_BYTE:
            case TokenType::KW_WORD:
                if (peek_next().type() == TokenType::IDENTIFIER) {
                    exit_panic_mode();
                    return;
                }
                break;
        }

        advance_token();
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
        case TokenType::KW_TRUE:
        case TokenType::KW_FALSE:
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
    try {
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
                    if (!is_string_concatenable(left_type) || !is_string_concatenable(right_type)) {
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
                if (!is_numeric_type(left_type) || !is_numeric_type(right_type)) {
                    throw SemanticError("Numeric operands expected for arithmetic operation",
                        expr.op().line(), expr.op().column());
                }
                current_type_ = common_type(left_type, right_type);
                break;

            case TokenType::OP_EQUAL:
            case TokenType::OP_NOT_EQUAL:
                // 相等性比较
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
                // 关系比较
                if (!is_ordered_type(left_type) || !is_ordered_type(right_type) ||
                    !is_compatible_type(left_type, right_type)) {
                    throw SemanticError("Invalid operands for comparison",
                        expr.op().line(), expr.op().column());
                }
                current_type_ = TokenType::KW_BOOL;
                break;

            case TokenType::OP_AND:
            case TokenType::OP_OR:
                // 逻辑运算
                if (left_type != TokenType::KW_BOOL || right_type != TokenType::KW_BOOL) {
                    throw SemanticError("Boolean operands expected for logical operation",
                        expr.op().line(), expr.op().column());
                }
                current_type_ = TokenType::KW_BOOL;
                break;

            case TokenType::OP_BIT_AND:
            case TokenType::OP_BIT_OR:
            case TokenType::OP_BIT_XOR:
                // 位运算
                if (!is_bit_type(left_type) || !is_bit_type(right_type) ||
                    !is_compatible_type(left_type, right_type)) {
                    throw SemanticError("Bit operands expected for bitwise operation",
                        expr.op().line(), expr.op().column());
                }
                current_type_ = common_type(left_type, right_type);
                break;

            case TokenType::OP_BIT_LSHIFT:
            case TokenType::OP_BIT_RSHIFT:
                // 位移运算
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
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

// 语句访问方法实现
void SemanticAnalyzer::visitVarDecl(const VarDeclStmt& stmt) {
    try {
        std::string name(stmt.name().lexeme());

        // 检查变量是否已在当前作用域中定义
        if (symbols_.is_defined_in_current_scope(name)) {
            throw SemanticError(
                std::string("Variable '") + name + "' is already defined in this scope",
                stmt.name().line(), stmt.name().column()
            );
        }

        // 检查类型是否是有效的类型标识符
        TokenType type_token = stmt.type().type();
        if (type_token == TokenType::IDENTIFIER) {
            // 如果是标识符，尝试将其解析为类型
            type_token = get_identifier_type(stmt.type().lexeme());
        }

        // 检查是否是有效的类型
        if (!is_valid_type(type_token)) {
            throw SemanticError(
                std::string("Invalid type '") + std::string(stmt.type().lexeme()) + "'",
                stmt.type().line(), stmt.type().column()
            );
        }

        // const 变量必须有初始化表达式
        if (stmt.is_const() && !stmt.initializer()) {
            throw SemanticError(
                std::string("Constant variable '") + name + "' must be initialized",
                stmt.name().line(), stmt.name().column()
            );
        }

        // 创建新的符号
        Symbol symbol{
            SymbolKind::VARIABLE,
            Token(type_token, stmt.type().lexeme(), stmt.type().line(), stmt.type().column()),
            stmt.name(),
            symbols_.current_scope_level(),
            false,  // 初始时未初始化
            stmt.is_const()
        };

        // 如果有初始化表达式，分析它
        if (stmt.initializer()) {
            stmt.initializer()->accept(*this);
            TokenType init_type = current_type_;

            // 检查类型兼容性
            if (!is_compatible_type(type_token, init_type)) {
                throw SemanticError(
                    std::string("Cannot initialize variable of type '") +
                    token_type_to_string(type_token) +
                    "' with value of type '" +
                    token_type_to_string(init_type) + "'",
                    stmt.name().line(), stmt.name().column()
                );
            }

            // 标记变量已初始化
            symbol.is_initialized = true;
        }

        // 将符号添加到符号表
        symbols_.define(symbol);

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitBlock(const BlockStmt& stmt) {
    try {
        // 进入新的作用域
        symbols_.begin_scope();

        // 分析块中的每个语句
        for (const auto& s : stmt.statements()) {
            s->accept(*this);
        }

        // 退出作用域
        symbols_.end_scope();

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitIf(const IfStmt& stmt) {
    try {
        // 检查条件表达式
        stmt.condition()->accept(*this);
        if (current_type_ != TokenType::KW_BOOL) {
            throw SemanticError("If condition must be a boolean expression",
                stmt.if_token().line(), stmt.if_token().column());
        }

        // 记录当前的返回值状态
        bool had_return = has_return_;
        bool then_returns = false;
        bool else_returns = false;

        // 分析 then 分支
        symbols_.begin_scope();
        stmt.then_branch()->accept(*this);
        then_returns = has_return_;
        symbols_.end_scope();

        // 分析 else 分支（如果存在）
        if (stmt.else_branch()) {
            has_return_ = had_return;  // 重置返回值状态
            symbols_.begin_scope();
            stmt.else_branch()->accept(*this);
            else_returns = has_return_;
            symbols_.end_scope();
        }

        // 只有当两个分支都有返回值时，整个 if 语句才有返回值
        has_return_ = had_return || (then_returns && else_returns);

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitWhile(const WhileStmt& stmt) {
    try {
        // 检查条件表达式
        stmt.condition()->accept(*this);
        if (current_type_ != TokenType::KW_BOOL) {
            throw SemanticError("While condition must be a boolean expression",
                stmt.while_token().line(), stmt.while_token().column());
        }

        // 记录当前的返回值状态
        bool had_return = has_return_;

        // 进入循环体
        symbols_.begin_scope();
        loop_depth_++;
        stmt.body()->accept(*this);
        loop_depth_--;
        symbols_.end_scope();

        // 循环可能不执行，所以不能保证有返回值
        has_return_ = had_return;
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitFor(const ForStmt& stmt) {
    try {
        symbols_.begin_scope();

        // 检查初始化语句
        if (stmt.initializer()) {
            stmt.initializer()->accept(*this);
        }

        // 检查条件表达式
        if (stmt.condition()) {
            stmt.condition()->accept(*this);
            if (current_type_ != TokenType::KW_BOOL) {
                throw SemanticError("For condition must be a boolean expression",
                    stmt.for_token().line(), stmt.for_token().column());
            }
        }

        // 检查增量表达式
        if (stmt.increment()) {
            stmt.increment()->accept(*this);
        }

        // 进入循环体
        loop_depth_++;
        stmt.body()->accept(*this);
        loop_depth_--;

        symbols_.end_scope();
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitDoWhile(const DoWhileStmt& stmt) {
    try {
        // 进入循环体
        symbols_.begin_scope();
        loop_depth_++;
        stmt.body()->accept(*this);
        loop_depth_--;
        symbols_.end_scope();

        // 检查条件表达式
        stmt.condition()->accept(*this);
        if (current_type_ != TokenType::KW_BOOL) {
            throw SemanticError("Do-while condition must be a boolean expression",
                stmt.do_token().line(), stmt.do_token().column());
        }
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitSwitch(const SwitchStmt& stmt) {
    try {
        // 检查 switch 条件表达式
        stmt.condition()->accept(*this);
        // switch 条件可以是任意类型（number/string/bool），不需要限制

        // 检查每个 case 分支
        for (const auto& sc : stmt.cases()) {
            // 检查 case 值表达式
            for (const auto& val : sc.values) {
                val->accept(*this);
            }
            // 检查 case 执行体
            if (sc.body) {
                symbols_.begin_scope();
                sc.body->accept(*this);
                symbols_.end_scope();
            }
        }
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitFunction(const FunctionStmt& stmt) {
    try {
        // 检查函数是否已在当前作用域中定义
        std::string name(stmt.name().lexeme());

        // 获取所有同名函数
        std::vector<Symbol*> overloads = symbols_.resolve_overloads(name);

        // 检查是否有完全相同的函数签名
        for (const auto* overload : overloads) {
            if (is_same_signature(*overload, stmt)) {
                throw SemanticError("Function '" + name + "' with same signature is already defined",
                    stmt.name().line(), stmt.name().column());
            }
        }

        // 创建函数符号
        Symbol function{
            SymbolKind::FUNCTION,
            stmt.return_type(),
            stmt.name(),
            symbols_.current_scope_level(),
            true,  // 函数定义时就认为是已初始化的
            false, // 不是常量
            {}     // 参数列表先为空
        };

        // 先收集参数列表（检查重名），填充函数符号的 parameters
        std::vector<Symbol> param_symbols;
        for (const auto& param : stmt.parameters()) {
            // 检查参数名是否重复（在已收集的参数中查找）
            for (const auto& prev : param_symbols) {
                if (prev.name.lexeme() == param.name.lexeme()) {
                    throw SemanticError("Duplicate parameter name '" +
                        std::string(param.name.lexeme()) + "'",
                        param.name.line(), param.name.column());
                }
            }

            // 创建参数符号
            Symbol param_symbol{
                SymbolKind::PARAMETER,
                param.type,
                param.name,
                symbols_.current_scope_level() + 1,  // 将在函数作用域内
                true  // 参数总是已初始化的
            };

            function.parameters.push_back(param_symbol);
            param_symbols.push_back(param_symbol);
        }

        // 在当前作用域定义函数（此时已有完整参数列表）——允许递归调用
        symbols_.define(function);

        // 保存当前函数以供 return 语句检查
        Symbol* previous_function = current_function_;
        current_function_ = &function;

        // 进入函数作用域
        symbols_.begin_scope();

        // 重置返回值标记
        has_return_ = false;

        // 将参数符号注册到函数作用域
        for (const auto& ps : param_symbols) {
            symbols_.define(ps);
        }

        // 分析函数体
        stmt.body()->accept(*this);

        // 检查是否所有路径都有返回值
        if (stmt.return_type().type() != TokenType::KW_NONE && !has_return_) {
            throw SemanticError("Function '" + name + "' must return a value in all code paths",
                stmt.name().line(), stmt.name().column());
        }

        // 退出函数作用域
        symbols_.end_scope();

        // 恢复之前的函数上下文
        current_function_ = previous_function;

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitCall(const CallExpr& expr) {
    try {
        // 内建函数 print：接受任意个任意类型的实参，返回 none。
        // 内建函数 len/toString/toNumber：单参，返回固定类型。
        // 在此提前处理，避免把内建函数当作未声明的变量/函数而报错。
        // TODO(semantic): 未来以正式的内建函数符号表 + 变参签名替代此特判。
        if (const IdentifierExpr* builtin =
                dynamic_cast<const IdentifierExpr*>(expr.callee())) {
            if (builtin->name().lexeme() == "print") {
                for (const auto& arg : expr.arguments()) {
                    arg->accept(*this);
                }
                current_type_ = TokenType::KW_NONE;
                return;
            }
            if (builtin->name().lexeme() == "len" ||
                builtin->name().lexeme() == "toString" ||
                builtin->name().lexeme() == "toNumber") {
                if (expr.arguments().size() != 1) {
                    throw SemanticError(
                        std::string(builtin->name().lexeme()) + "() expects exactly 1 argument",
                        builtin->name().line(), builtin->name().column());
                }
                expr.arguments()[0]->accept(*this);
                TokenType arg_type = current_type_;
                if (builtin->name().lexeme() == "len") {
                    // len 仅接受 string
                    if (arg_type != TokenType::KW_STRING) {
                        throw SemanticError("len() expects a string argument",
                            builtin->name().line(), builtin->name().column());
                    }
                    current_type_ = TokenType::KW_NUMBER;
                } else if (builtin->name().lexeme() == "toString") {
                    current_type_ = TokenType::KW_STRING;
                } else {
                    current_type_ = TokenType::KW_NUMBER;
                }
                return;
            }
        }

        // 分析被调用的表达式
        expr.callee()->accept(*this);

        // 收集参数类型
        std::vector<TokenType> arg_types;
        for (const auto& arg : expr.arguments()) {
            arg->accept(*this);
            arg_types.push_back(current_type_);
        }

        // 获取函数名
        const IdentifierExpr* callee = dynamic_cast<const IdentifierExpr*>(expr.callee());
        if (!callee) {
            throw SemanticError("Invalid function call target",
                expr.paren().line(), expr.paren().column());
        }

        std::string func_name(callee->name().lexeme());
        auto overloads = symbols_.resolve_overloads(func_name);

        if (overloads.empty()) {
            throw SemanticError("Undefined function '" + func_name + "'",
                callee->name().line(), callee->name().column());
        }

        // 查找最匹配的重载函数
        Symbol* best_match = find_best_overload(overloads, arg_types, expr.paren());
        if (!best_match) {
            std::string message = "No matching overload for function '" + func_name + "'";
            throw SemanticError(message, expr.paren().line(), expr.paren().column());
        }

        // 设置返回值类型
        current_type_ = best_match->type.type();

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

// 辅助方法：检查函数签名是否相同
bool SemanticAnalyzer::is_same_signature(const Symbol& func1, const FunctionStmt& func2) {
    // 检查参数数量
    if (func1.parameters.size() != func2.parameters().size()) {
        return false;
    }

    // 检查每个参数的类型
    for (size_t i = 0; i < func1.parameters.size(); ++i) {
        if (func1.parameters[i].type.type() != func2.parameters()[i].type.type()) {
            return false;
        }
    }

    // 检查返回类型
    return func1.type.type() == func2.return_type().type();
}

// 辅助方法：查找最匹配的重载函数
Symbol* SemanticAnalyzer::find_best_overload(
    const std::vector<Symbol*>& overloads,
    const std::vector<TokenType>& arg_types,
    const Token& error_location) {

    Symbol* best_match = nullptr;
    int best_score = -1;

    for (Symbol* overload : overloads) {
        int score = calculate_overload_score(*overload, arg_types);
        if (score > best_score) {
            best_score = score;
            best_match = overload;
        }
    }

    return best_match;
}

int SemanticAnalyzer::calculate_overload_score(
    const Symbol& func,
    const std::vector<TokenType>& arg_types) {

    if (func.parameters.size() != arg_types.size()) {
        return -1;
    }

    int score = 0;
    for (size_t i = 0; i < arg_types.size(); ++i) {
        TokenType param_type = func.parameters[i].type.type();
        TokenType arg_type = arg_types[i];

        if (param_type == arg_type) {
            score += 2;  // 完全匹配
        } else if (can_implicit_convert(arg_type, param_type)) {
            score += 1;  // 需要隐式转换
        } else {
            return -1;  // 不兼容
        }
    }

    return score;
}

void SemanticAnalyzer::visitAssign(const AssignExpr& expr) {
    try {
        // 先检查变量是否存在
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

        // 分析赋值表达式
        expr.value()->accept(*this);
        TokenType value_type = current_type_;

        // 检查类型兼容性
        if (!is_compatible_type(symbol->type.type(), value_type)) {
            throw SemanticError("Cannot assign value of type '" +
                token_type_to_string(value_type) + "' to variable of type '" +
                token_type_to_string(symbol->type.type()) + "'",
                expr.name().line(), expr.name().column());
        }

        // 标记变量已初始化和被修改
        symbol->is_initialized = true;
        symbol->is_modified = true;

        // 赋值表达式的类型是被赋值的变量的类型
        current_type_ = symbol->type.type();

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitUnary(const UnaryExpr& expr) {
    try {
        // 分析操作数
        expr.operand()->accept(*this);
        TokenType operand_type = current_type_;

        switch (expr.op().type()) {
            case TokenType::OP_MINUS:
                if (!is_numeric_type(operand_type)) {
                    throw SemanticError("Numeric operand expected for unary minus",
                        expr.op().line(), expr.op().column());
                }
                current_type_ = operand_type;
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
                throw SemanticError("Unknown unary operator",
                    expr.op().line(), expr.op().column());
        }
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitExpression(const ExpressionStmt& stmt) {
    try {
        stmt.expression()->accept(*this);
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

// 添加辅助方法
bool SemanticAnalyzer::is_bit_type(TokenType type) const {
    switch (type) {
        case TokenType::KW_BYTE:
        case TokenType::KW_WORD:
            return true;
        default:
            return false;
    }
}

// ... 其他方法的实现 ...

// 辅助方法实现
TokenType SemanticAnalyzer::check_type(const Expr& expr) {
    expr.accept(*this);
    return current_type_;
}

bool SemanticAnalyzer::is_numeric_type(TokenType type) const {
    switch (type) {
        case TokenType::KW_NUMBER:
        case TokenType::KW_BYTE:
        case TokenType::KW_WORD:
            return true;
        default:
            return false;
    }
}

bool SemanticAnalyzer::is_numeric_convertible(TokenType type) const {
    return is_numeric_type(type) || type == TokenType::KW_CHAR;
}

bool SemanticAnalyzer::is_string_convertible(TokenType type) const {
    switch (type) {
        case TokenType::KW_STRING:
        case TokenType::KW_CHAR:
        case TokenType::KW_CHARACTER:
        case TokenType::KW_NUMBER:
        case TokenType::KW_BOOL:
        case TokenType::KW_BYTE:
        case TokenType::KW_WORD:
            return true;
        default:
            return false;
    }
}

bool SemanticAnalyzer::is_compatible_type(TokenType expected, TokenType actual) const {
    // 相同类型总是兼容的
    if (expected == actual) return true;

    // 检查数值类型的兼容性
    if (is_numeric_convertible(expected) && is_numeric_convertible(actual)) {
        // 允许从小类型到大类型的隐式转换
        if (expected == TokenType::KW_NUMBER) {
            return true;  // 任何数值类型都可以转换为 number
        }
        if (expected == TokenType::KW_WORD && actual == TokenType::KW_BYTE) {
            return true;
        }
    }

    // 检查字符类型的兼容性
    if (expected == TokenType::KW_CHARACTER && actual == TokenType::KW_CHAR) {
        return true;
    }
    if (expected == TokenType::KW_STRING) {
        return is_string_convertible(actual);
    }

    return false;
}

void SemanticAnalyzer::ensure_boolean(const Expr& expr, const std::string& context) {
    TokenType type = check_type(expr);

    size_t line = 0, column = 0;

    // 获取表达式的位置信息
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

    if (type != TokenType::KW_BOOL) {
        std::string message = "Boolean expression expected in " + context;
        throw SemanticError(message, line, column);
    }
}

void SemanticAnalyzer::visitReturn(const ReturnStmt& stmt) {
    try {
        // 检查是否在函数内部
        if (!current_function_) {
            throw SemanticError("Cannot return from global scope",
                stmt.keyword().line(), stmt.keyword().column());
        }

        // 检查返回值类型
        if (stmt.value()) {
            stmt.value()->accept(*this);
            TokenType return_type = current_function_->type.type();
            TokenType value_type = current_type_;

            if (!is_compatible_type(return_type, value_type)) {
                throw SemanticError("Cannot return value of type '" +
                    token_type_to_string(value_type) + "' from function with return type '" +
                    token_type_to_string(return_type) + "'",
                    stmt.keyword().line(), stmt.keyword().column());
            }
        } else if (current_function_->type.type() != TokenType::KW_NONE) {
            throw SemanticError("Function with return type '" +
                token_type_to_string(current_function_->type.type()) +
                "' must return a value",
                stmt.keyword().line(), stmt.keyword().column());
        }

        // 标记此路径已有返回值
        has_return_ = true;
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
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
    // 相同类型直接返回
    if (t1 == t2) return t1;

    // 数值类型的共同类型
    if (is_numeric_convertible(t1) && is_numeric_convertible(t2)) {
        // 返回最大的类型
        if (t1 == TokenType::KW_NUMBER || t2 == TokenType::KW_NUMBER)
            return TokenType::KW_NUMBER;
        if (t1 == TokenType::KW_WORD || t2 == TokenType::KW_WORD)
            return TokenType::KW_WORD;
        return TokenType::KW_BYTE;
    }

    // 字符类型的共同类型
    if ((t1 == TokenType::KW_CHAR || t1 == TokenType::KW_CHARACTER) &&
        (t2 == TokenType::KW_CHAR || t2 == TokenType::KW_CHARACTER)) {
        return TokenType::KW_CHARACTER;
    }

    // 如果有一个是字符串类型，且另一个可以转换为字符串
    if ((t1 == TokenType::KW_STRING && is_string_convertible(t2)) ||
        (t2 == TokenType::KW_STRING && is_string_convertible(t1))) {
        return TokenType::KW_STRING;
    }

    // 无法找到共同类型
    return TokenType::INVALID;
}

void SemanticAnalyzer::visitBreak(const BreakStmt& stmt) {
    with_error_handling([&]() {
        if (!in_loop()) {
            throw SemanticError(
                "Cannot use 'break' outside of a loop. Break statement must be inside a loop",
                stmt.keyword().line(), stmt.keyword().column());
        }
    });
}

void SemanticAnalyzer::visitContinue(const ContinueStmt& stmt) {
    with_error_handling([&]() {
        if (!in_loop()) {
            throw SemanticError(
                "Cannot use 'continue' outside of a loop. Continue statement must be inside a loop",
                stmt.keyword().line(), stmt.keyword().column());
        }
    });
}

// 添加错误恢复的辅助方法
bool SemanticAnalyzer::is_synchronization_point() const {
    // 判断是否是可以安全恢复的位置
    TokenType type = current_token().type();
    switch (type) {
        case TokenType::KW_FUNCTION:
        case TokenType::KW_CLASS:
        case TokenType::KW_NUMBER:
        case TokenType::KW_STRING:
        case TokenType::KW_BOOL:
        case TokenType::KW_CHAR:
        case TokenType::KW_BYTE:
        case TokenType::KW_WORD:
        case TokenType::KW_DWORD:
        case TokenType::DELIMITER_SEMICOLON:
        case TokenType::DELIMITER_RBRACE:
            return true;
        default:
            return false;
    }
}

// 添加 token 访问辅助方法
const Token& SemanticAnalyzer::current_token() const {
    if (tokens_.empty()) {
        static Token empty_token;  // 返回一个静态的空 token
        return empty_token;
    }
    if (current_token_index_ >= tokens_.size()) {
        return tokens_.back();  // 返回 EOF token
    }
    return tokens_[current_token_index_];
}

const Token& SemanticAnalyzer::previous_token() const {
    if (tokens_.empty()) {
        static Token empty_token;  // 返回一个静态的空 token
        return empty_token;
    }
    if (current_token_index_ == 0) {
        return tokens_[0];
    }
    return tokens_[current_token_index_ - 1];
}

const Token& SemanticAnalyzer::peek_next() const {
    if (tokens_.empty()) {
        static Token empty_token;  // 返回一个静态的空 token
        return empty_token;
    }
    if (current_token_index_ + 1 >= tokens_.size()) {
        return tokens_.back();  // 返回 EOF token
    }
    return tokens_[current_token_index_ + 1];
}

void SemanticAnalyzer::advance_token() {
    if (!tokens_.empty() && current_token_index_ < tokens_.size() - 1) {
        ++current_token_index_;
    }
}

void SemanticAnalyzer::visitTuple(const TupleExpr& expr) {
    try {
        std::vector<TokenType> element_types;

        // 分析每个元素的类型
        for (const auto& element : expr.elements()) {
            element->accept(*this);
            element_types.push_back(current_type_);
        }

        // 设置当前类型为元组类型
        current_type_ = TokenType::KW_TUPLE;
        tuple_element_types_ = element_types;  // 存储元组元素类型

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitTupleMember(const TupleMemberExpr& expr) {
    try {
        // 分析元组表达式
        expr.tuple()->accept(*this);

        // 确保是元组类型
        if (current_type_ != TokenType::KW_TUPLE) {
            throw SemanticError("Cannot access member of non-tuple type",
                expr.dot().line(), expr.dot().column());
        }

        // 检查索引是否有效
        if (expr.index() >= tuple_element_types_.size()) {
            throw SemanticError("Tuple index out of range",
                expr.dot().line(), expr.dot().column());
        }

        // 设置当前类型为元组成员的类型
        current_type_ = tuple_element_types_[expr.index()];

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitTernary(const TernaryExpr& expr) {
    try {
        // 分析条件表达式
        expr.condition()->accept(*this);
        TokenType cond_type = current_type_;

        // 条件必须是布尔型
        if (cond_type != TokenType::KW_BOOL) {
            throw SemanticError("Ternary condition must be a boolean expression",
                expr.question_token().line(), expr.question_token().column());
        }

        // 分析 then 和 else 分支
        expr.then_expr()->accept(*this);
        TokenType then_type = current_type_;

        expr.else_expr()->accept(*this);
        TokenType else_type = current_type_;

        // 两个分支类型必须兼容（简化：相同或可隐式转换）
        if (then_type != else_type && !is_compatible_type(then_type, else_type)
            && !is_compatible_type(else_type, then_type)) {
            throw SemanticError("Ternary branches must have compatible types",
                expr.question_token().line(), expr.question_token().column());
        }

        // 结果类型取 then 分支类型
        current_type_ = then_type;

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitTupleType(const TupleType& type) {
    try {
        std::vector<TokenType> element_types;

        // 分析每个元素类型
        for (const auto& element_type : type.element_types()) {
            element_type->accept(*this);
            element_types.push_back(current_type_);
        }

        // 存储元组元素类型
        current_type_ = TokenType::KW_TUPLE;
        tuple_element_types_ = element_types;

    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitBasicType(const BasicType& type) {
    try {
        // 基本类型直接设置为对应的类型
        current_type_ = type.name().type();
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitArrayType(const ArrayType& type) {
    try {
        // 分析元素类型
        type.element_type().accept(*this);
        TokenType element_type = current_type_;

        // 设置当前类型为数组类型
        current_type_ = TokenType::KW_ARRAY;
        // 存储数组元素类型
        array_element_type_ = element_type;
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

void SemanticAnalyzer::visitClass(const ClassStmt& stmt) {
    // TODO: 实现类的语义分析
    // 1. 检查类名是否已经被使用
    // 2. 分析类成员
    for (const auto& member : stmt.members()) {
        member->accept(*this);
    }
}

// -----------------------------------------------------------------------------
// 私有辅助方法
// -----------------------------------------------------------------------------

void SemanticAnalyzer::reset_state() {
    errors_.clear();
    in_panic_mode_ = false;
    current_token_index_ = 0;
    current_type_ = TokenType::INVALID;
    current_function_ = nullptr;
    has_return_ = false;
    loop_depth_ = 0;
    tuple_element_types_.clear();
    array_element_type_ = TokenType::INVALID;
}

template<typename Func>
void SemanticAnalyzer::with_error_handling(Func&& func) {
    try {
        func();
    } catch (const SemanticError& error) {
        record_error(error);
        if (!in_panic_mode_) {
            enter_panic_mode();
            synchronize();
        }
    }
}

bool SemanticAnalyzer::is_valid_type(TokenType type) const {
    switch (type) {
        case TokenType::KW_NUMBER:
        case TokenType::KW_STRING:
        case TokenType::KW_BOOL:
        case TokenType::KW_CHAR:
        case TokenType::KW_CHARACTER:
        case TokenType::KW_BYTE:
        case TokenType::KW_WORD:
        case TokenType::KW_DWORD:
        case TokenType::KW_NONE:
        case TokenType::KW_OBJECT:
        case TokenType::KW_INTEGER:
        case TokenType::KW_DECIMAL:
        case TokenType::KW_TRIBOOL:
        case TokenType::KW_BIT:
            return true;
        default:
            return false;
    }
}

} // namespace collie
