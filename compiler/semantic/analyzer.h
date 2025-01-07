class SemanticAnalyzer : public StmtVisitor {
public:
    // ... 其他方法 ...

    /**
     * @brief 访问 break 语句
     * @param stmt break 语句节点
     * @throw SemanticError 当在循环外使用 break 时
     */
    void visitBreak(const BreakStmt& stmt) override;

    /**
     * @brief 访问 continue 语句
     * @param stmt continue 语句节点
     * @throw SemanticError 当在循环外使用 continue 时
     */
    void visitContinue(const ContinueStmt& stmt) override;

private:
    // ... 其他成员 ...
    size_t loop_depth_ = 0;  ///< 当前循环嵌套深度
    bool in_loop() const { return loop_depth_ > 0; }
};