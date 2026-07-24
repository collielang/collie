/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2024-01-07
 * @Description: Collie 编程语言编译器主程序入口
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#ifdef _WIN32
#include <Windows.h>
#endif
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic_analyzer.h"
#include "utils/token_utils.h"
#include "utils/version_info.h"

void flush_output() {
    std::cout.flush();
    std::cerr.flush();
}

int main(int argc, char* argv[]) {
    // 打印版本信息 & 环境信息
    std::cout << collie::utils::get_version_info();
    std::cout << std::endl;
    std::cout << collie::utils::get_environment_info();
    std::cout << std::endl;

    // 检查命令行参数
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " example.collie" << std::endl;
        return 1;
    }

    try {
        // 设置控制台输出为 UTF-8 编码
        #ifdef _WIN32
        if (!SetConsoleOutputCP(CP_UTF8)) {
            std::cerr << "Warning: Failed to set console output to UTF-8" << std::endl;
            flush_output();
        }
        #endif

        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " <source_file>" << std::endl;
            flush_output();
            return 1;
        }

        // 读取源文件
        std::string filename = argv[1];
        std::cout << "Reading file: " << filename << std::endl;
        flush_output();

        // 以二进制方式读取源文件，直接按 UTF-8 字节流处理（跨平台、无需编码转换）
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << std::endl;
            flush_output();
            return 1;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        if (file.bad()) {
            std::cerr << "Error reading file content" << std::endl;
            flush_output();
            return 1;
        }
        file.close();

        std::string source = ss.str();

        // 跳过可能存在的 UTF-8 BOM
        if (source.size() >= 3 &&
            static_cast<unsigned char>(source[0]) == 0xEF &&
            static_cast<unsigned char>(source[1]) == 0xBB &&
            static_cast<unsigned char>(source[2]) == 0xBF) {
            source.erase(0, 3);
        }

        std::cout << std::endl;
        flush_output();

        std::string equalSigns(20, '=');
        std::cout << "Source code:" << std::endl;
        std::cout << equalSigns << " START OF FILE " << equalSigns << std::endl;
        std::cout << source << std::endl;
        std::cout << equalSigns << "  END OF FILE  " << equalSigns << std::endl;
        std::cout << std::endl;
        flush_output();

        // 词法分析
        std::cout << "Starting lexical analysis..." << std::endl;
        flush_output();

        collie::Lexer lexer(source);
        std::cout << "Created lexer object..." << std::endl;
        flush_output();

        std::cout << "Starting tokenization..." << std::endl;
        flush_output();

        std::vector<collie::Token> tokens;
        try {
            tokens = lexer.tokenize();
        } catch (const std::exception& e) {
            std::cerr << "Error during tokenization: " << e.what() << std::endl;
            flush_output();
            return 1;
        }

        std::cout << "Tokenization completed. Token count: " << tokens.size() << std::endl;
        flush_output();

        std::cout << "Tokens:" << std::endl;
        for (const auto& token : tokens) {
            std::cout << "  Type: " << token_type_to_string(token.type())
                     << " (" << static_cast<int>(token.type()) << ")"
                     << ", Lexeme: '" << token.lexeme()
                     << "', Line: " << token.line()
                     << ", Column: " << token.column() << std::endl;
            flush_output();
        }
        std::cout << "Lexical analysis completed." << std::endl;
        std::cout << std::endl;
        flush_output();

        // 语法分析
        std::cout << "Starting syntax analysis..." << std::endl;
        flush_output();

        collie::Parser parser(tokens);
        std::vector<std::unique_ptr<collie::Stmt>> stmts;
        try {
            stmts = parser.parse_program();
            if (stmts.empty()) {
                std::cerr << "Error: Parser returned empty AST" << std::endl;
                flush_output();
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during parsing: " << e.what() << std::endl;
            flush_output();
            return 1;
        }

        std::cout << "Syntax analysis completed." << std::endl;
        std::cout << std::endl;
        flush_output();

        // 语义分析
        std::cout << "Starting semantic analysis..." << std::endl;
        flush_output();

        collie::SemanticAnalyzer analyzer;
        try {
            analyzer.analyze(stmts);
        } catch (const std::exception& e) {
            std::cerr << "Error during semantic analysis: " << e.what() << std::endl;
            flush_output();
            return 1;
        }

        std::cout << "Semantic analysis completed." << std::endl;
        std::cout << std::endl;
        flush_output();

        // 检查语义错误：有错误时逐条上报并以非零退出码结束，
        // 不再静默打印 "Compilation successful!"
        if (analyzer.has_errors()) {
            const auto& errors = analyzer.get_errors();
            std::cerr << "Found " << errors.size()
                      << (errors.size() == 1 ? " semantic error:" : " semantic errors:")
                      << std::endl;
            for (const auto& err : errors) {
                std::cerr << "  " << err.what() << std::endl;
            }
            flush_output();
            return 1;
        }

        std::cout << "Compilation successful!" << std::endl;
        flush_output();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << std::endl;
        flush_output();
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred during compilation." << std::endl;
        flush_output();
        return 1;
    }
}
