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
#include <codecvt>
#include <locale>
#include <Windows.h>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic_analyzer.h"
#include "utils/token_utils.h"

void flush_output() {
    std::cout.flush();
    std::cerr.flush();
}

int main(int argc, char* argv[]) {
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

        // 以二进制模式打开文件并设置 UTF-8 编码
        std::wifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << std::endl;
            flush_output();
            return 1;
        }

        try {
            file.imbue(std::locale(file.getloc(),
                new std::codecvt_utf8<wchar_t, 0x10ffff, std::consume_header>));
        } catch (const std::exception& e) {
            std::cerr << "Error setting file encoding: " << e.what() << std::endl;
            flush_output();
            return 1;
        }

        std::wstringstream wss;
        wss << file.rdbuf();
        if (file.fail()) {
            std::cerr << "Error reading file content" << std::endl;
            flush_output();
            return 1;
        }
        file.close();

        // 将宽字符串转换为 UTF-8 编码的字符串
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::string source;
        try {
            source = converter.to_bytes(wss.str());
        } catch (const std::exception& e) {
            std::cerr << "Error converting file content to UTF-8: " << e.what() << std::endl;
            flush_output();
            return 1;
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
            std::cout << "  Type: " << static_cast<int>(token.type())
                     << " (" << token_type_to_string(token.type()) << ")"
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