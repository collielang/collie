void SemanticAnalyzer::visitBreak(const BreakStmt& stmt) {
    if (!in_loop()) {
        throw SemanticError("Cannot use 'break' outside of a loop.",
            stmt.keyword().line(), stmt.keyword().column());
    }
}

void SemanticAnalyzer::visitContinue(const ContinueStmt& stmt) {
    if (!in_loop()) {
        throw SemanticError("Cannot use 'continue' outside of a loop.",
            stmt.keyword().line(), stmt.keyword().column());
    }
}

void SemanticAnalyzer::visitWhile(const WhileStmt& stmt) {
    // 检查条件表达式
    stmt.condition().accept(*this);

    // 进入循环体
    loop_depth_++;
    stmt.body().accept(*this);
    loop_depth_--;
}

void SemanticAnalyzer::visitFor(const ForStmt& stmt) {
    // 检查初始化语句
    if (stmt.initializer()) {
        stmt.initializer()->accept(*this);
    }

    // 检查条件表达式
    if (stmt.condition()) {
        stmt.condition()->accept(*this);
    }

    // 检查增量表达式
    if (stmt.increment()) {
        stmt.increment()->accept(*this);
    }

    // 进入循环体
    loop_depth_++;
    stmt.body().accept(*this);
    loop_depth_--;
}