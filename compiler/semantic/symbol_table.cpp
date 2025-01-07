/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "symbol_table.h"

namespace collie {

void Scope::define(const Symbol& symbol) {
    std::string name(symbol.name.lexeme());
    symbols_.insert_or_assign(name, symbol);
}

Symbol* Scope::resolve(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return &(it->second);
    }
    return nullptr;
}

bool Scope::is_defined(const std::string& name) const {
    return symbols_.find(name) != symbols_.end();
}

void SymbolTable::begin_scope() {
    scopes_.emplace_back(scopes_.size());
}

void SymbolTable::end_scope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void SymbolTable::define(const Symbol& symbol) {
    if (!scopes_.empty()) {
        scopes_.back().define(symbol);
    }
}

Symbol* SymbolTable::resolve(const std::string& name) {
    // 从内层作用域向外层查找
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        if (Symbol* symbol = it->resolve(name)) {
            return symbol;
        }
    }
    return nullptr;
}

bool SymbolTable::is_defined_in_current_scope(const std::string& name) const {
    if (!scopes_.empty()) {
        return scopes_.back().is_defined(name);
    }
    return false;
}

} // namespace collie