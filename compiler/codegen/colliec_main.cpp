/**
 * @file colliec_main.cpp
 * @brief Collie 本地编译器驱动（M6 t49，S1/S2 最小子集）
 *
 * 流水线：读源码 → Lexer → Parser（+语法门禁）→ SemanticAnalyzer（+语义门禁）
 *        → CodeGenerator 生成 LLVM IR → 写 .ll → 调 LLVM 自带 clang 编成本地二进制。
 *
 * 用法：colliec [--emit-llvm] [-o <output>] <source.collie>
 *   --emit-llvm  只生成 <base>.ll，不链接
 *   -o <output>  指定输出路径（默认与源文件同名换后缀）
 */
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "code_generator.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../semantic/semantic_analyzer.h"

#ifndef COLLIE_LLVM_BIN
#define COLLIE_LLVM_BIN ""
#endif

namespace {

/// @brief 读取源文件（二进制，去 UTF-8 BOM）；失败返回 false
bool read_source(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

/// @brief 去掉路径的扩展名（用于派生 .ll / .exe 输出名）
std::string strip_extension(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path;
    }
    return path.substr(0, dot);
}

} // namespace

int main(int argc, char* argv[]) {
    bool emit_llvm_only = false;
    std::string filename;
    std::string output;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--emit-llvm") {
            emit_llvm_only = true;
        } else if (arg == "-o" && i + 1 < argc) {
            output = argv[++i];
        } else if (filename.empty()) {
            filename = arg;
        }
    }

    if (filename.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " [--emit-llvm] [-o <output>] <source.collie>" << std::endl;
        return 1;
    }

    std::string source;
    if (!read_source(filename, source)) {
        std::cerr << "Error: cannot open source file: " << filename << std::endl;
        return 1;
    }

    // 词法
    std::vector<collie::Token> tokens;
    try {
        collie::Lexer lexer(source);
        tokens = lexer.tokenize();
    } catch (const std::exception& e) {
        std::cerr << "Error during tokenization: " << e.what() << std::endl;
        return 1;
    }

    // 语法（错误恢复 + 门禁）
    collie::Parser parser(tokens);
    std::vector<std::unique_ptr<collie::Stmt>> stmts;
    try {
        stmts = parser.parse_program();
    } catch (const std::exception& e) {
        std::cerr << "Error during parsing: " << e.what() << std::endl;
        return 1;
    }
    if (!parser.get_errors().empty()) {
        std::cerr << "Found " << parser.get_errors().size() << " syntax error(s)."
                  << std::endl;
        return 1;
    }
    if (stmts.empty()) {
        std::cerr << "Error: empty program" << std::endl;
        return 1;
    }

    // 语义（门禁）
    collie::SemanticAnalyzer analyzer;
    try {
        analyzer.analyze(stmts);
    } catch (const std::exception& e) {
        std::cerr << "Error during semantic analysis: " << e.what() << std::endl;
        return 1;
    }
    if (analyzer.has_errors()) {
        const auto& errors = analyzer.get_errors();
        std::cerr << "Found " << errors.size() << " semantic error(s):" << std::endl;
        for (const auto& err : errors) std::cerr << "  " << err.what() << std::endl;
        return 1;
    }

    // 代码生成
    collie::CodeGenerator codegen;
    try {
        codegen.generate(stmts, strip_extension(filename));
    } catch (const collie::CodeGenError& e) {
        std::cerr << "Codegen error";
        if (e.line() > 0) std::cerr << " at line " << e.line() << ", column " << e.column();
        std::cerr << ": " << e.what() << std::endl;
        return 1;
    }

    // 写 .ll
    const std::string ll_path = strip_extension(output.empty() ? filename : output) + ".ll";
    {
        std::ofstream ll_file(ll_path, std::ios::binary);
        if (!ll_file) {
            std::cerr << "Error: cannot write IR file: " << ll_path << std::endl;
            return 1;
        }
        ll_file << codegen.emit_ir();
    }

    if (emit_llvm_only) {
        std::cout << ll_path << std::endl;
        return 0;
    }

    // 调用 LLVM 自带 clang 把 .ll 编成本地二进制
    const std::string exe_path =
        output.empty() ? strip_extension(filename) + ".exe" : output;
    const std::string clang_bin = std::string(COLLIE_LLVM_BIN) + "/clang.exe";

    // Windows 下 std::system 需把含空格路径的整条命令再套一层引号；
    // -Wno-override-module：模块 triple 与 clang 宿主 triple 仅差 MSVC 版本后缀，告警无意义
    std::ostringstream cmd;
    cmd << "\"\"" << clang_bin << "\" -Wno-override-module \"" << ll_path
        << "\" -o \"" << exe_path << "\"\"";
    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        std::cerr << "Error: linking failed (clang exit code " << rc << "). "
                  << "IR written to " << ll_path << std::endl;
        return 1;
    }
    std::cout << exe_path << std::endl;
    return 0;
}
