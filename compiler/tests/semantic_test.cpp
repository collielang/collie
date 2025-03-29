TEST(SemanticAnalyzerTest, BreakContinueStatements) {
    // 测试在循环内使用 break
    {
        std::string source = R"(
            while (true) {
                if (x > 10) break;
            }
        )";
        Lexer lexer(source);
        Parser parser(lexer);
        auto stmt = parser.parse();

        SemanticAnalyzer analyzer;
        EXPECT_NO_THROW(analyzer.analyze(stmt.get()));
    }

    // 测试在循环外使用 break（应该报错）
    {
        std::string source = "break;";
        Lexer lexer(source);
        Parser parser(lexer);
        auto stmt = parser.parse();

        SemanticAnalyzer analyzer;
        EXPECT_THROW(analyzer.analyze(stmt.get()), SemanticError);
    }

    // 测试在循环外使用 continue（应该报错）
    {
        std::string source = "continue;";
        Lexer lexer(source);
        Parser parser(lexer);
        auto stmt = parser.parse();

        SemanticAnalyzer analyzer;
        EXPECT_THROW(analyzer.analyze(stmt.get()), SemanticError);
    }
}
