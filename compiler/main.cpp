/*
 * @Author: Zhang Bokai <zbrook@126.com>
 * @Date: 2024-01-07
 * @Description: Collie 编程语言编译器主程序入口
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <ostream>
#include <streambuf>
#ifdef _WIN32
#include <Windows.h>
#endif
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic_analyzer.h"
#include "interpreter/interpreter.h"
#include "utils/token_utils.h"
#include "utils/version_info.h"

namespace {
// 丢弃一切写入的空缓冲区：非 verbose 模式下用于静默编译流水线的诊断信息，
// 保证标准输出只包含被解释程序自身的 print 输出。
class NullBuffer : public std::streambuf {
public:
    int overflow(int c) override { return c; }
};
}  // namespace

void flush_output() {
    std::cout.flush();
    std::cerr.flush();
}

int main(int argc, char* argv[]) {
    // 命令行：collie [-v|--verbose] <source_file>
    // 默认安静模式：标准输出仅包含程序的 print 输出；诊断信息仅在 verbose 下打印。
    bool verbose = false;
    std::string filename;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (filename.empty()) {
            filename = arg;
        }
    }

    if (filename.empty()) {
        std::cerr << "Usage: " << argv[0] << " [-v|--verbose] <source_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " example.collie" << std::endl;
        return 1;
    }

    // 诊断信息输出流：verbose -> 标准输出；否则丢弃。
    NullBuffer null_buffer;
    std::ostream null_stream(&null_buffer);
    std::ostream& diag = verbose ? std::cout : null_stream;

    try {
        // 设置控制台输出为 UTF-8 编码
        #ifdef _WIN32
        if (!SetConsoleOutputCP(CP_UTF8)) {
            std::cerr << "Warning: Failed to set console output to UTF-8" << std::endl;
            flush_output();
        }
        #endif

        // 版本 & 环境信息（仅 verbose）
        diag << collie::utils::get_version_info() << std::endl;
        diag << collie::utils::get_environment_info() << std::endl;

        // 以二进制方式读取源文件，直接按 UTF-8 字节流处理（跨平台、无需编码转换）
        diag << "Reading file: " << filename << std::endl;
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

        std::string equalSigns(20, '=');
        diag << std::endl;
        diag << "Source code:" << std::endl;
        diag << equalSigns << " START OF FILE " << equalSigns << std::endl;
        diag << source << std::endl;
        diag << equalSigns << "  END OF FILE  " << equalSigns << std::endl;
        diag << std::endl;

        // 词法分析
        diag << "Starting lexical analysis..." << std::endl;
        collie::Lexer lexer(source);
        std::vector<collie::Token> tokens;
        try {
            tokens = lexer.tokenize();
        } catch (const std::exception& e) {
            std::cerr << "Error during tokenization: " << e.what() << std::endl;
            flush_output();
            return 1;
        }

        diag << "Tokenization completed. Token count: " << tokens.size() << std::endl;
        if (verbose) {
            diag << "Tokens:" << std::endl;
            for (const auto& token : tokens) {
                diag << "  Type: " << token_type_to_string(token.type())
                     << " (" << static_cast<int>(token.type()) << ")"
                     << ", Lexeme: '" << token.lexeme()
                     << "', Line: " << token.line()
                     << ", Column: " << token.column() << std::endl;
            }
        }
        diag << "Lexical analysis completed." << std::endl;
        diag << std::endl;

        // 语法分析
        diag << "Starting syntax analysis..." << std::endl;
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

        diag << "Syntax analysis completed." << std::endl;
        diag << std::endl;

        // 检查语法错误：parse_program 采用错误恢复（记录并跳过出错语句后继续），
        // 不会抛出异常，只会返回一份部分 AST。若存在语法错误，即使解析出部分
        // 有效语句也不应继续执行，否则会“报完 Parse error 仍运行正确的那部分”。
        if (!parser.get_errors().empty()) {
            const auto& syntax_errors = parser.get_errors();
            std::cerr << "Found " << syntax_errors.size()
                      << (syntax_errors.size() == 1 ? " syntax error." : " syntax errors.")
                      << std::endl;
            flush_output();
            return 1;
        }

        // 语义分析
        diag << "Starting semantic analysis..." << std::endl;
        collie::SemanticAnalyzer analyzer;
        try {
            analyzer.analyze(stmts);
        } catch (const std::exception& e) {
            std::cerr << "Error during semantic analysis: " << e.what() << std::endl;
            flush_output();
            return 1;
        }

        diag << "Semantic analysis completed." << std::endl;
        diag << std::endl;

        // 检查语义错误：有错误时逐条上报并以非零退出码结束
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

        // 解释执行：程序的 print 输出写入标准输出（与诊断信息分离）
        diag << "Running program..." << std::endl;
        try {
            collie::Interpreter interpreter(std::cout);
            interpreter.interpret(stmts);
        } catch (const collie::RuntimeError& e) {
            std::cout.flush();
            std::cerr << "Runtime error at line " << e.line()
                      << ", column " << e.column() << ": " << e.what() << std::endl;
            flush_output();
            return 1;
        }

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
