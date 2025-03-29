/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Date: 2025-01-05
 */
#include "test_utils.h"
#include "../parser/parser.h"
#include "../lexer/lexer.h"

namespace collie {
namespace test {

size_t getCurrentMemoryUsage() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
#else
    // Linux/Unix 系统
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss * 1024;  // 转换为字节
    }
#endif
    return 0;  // 如果无法获取内存使用量
}

std::pair<std::vector<std::unique_ptr<Stmt>>, std::vector<Token>>
parse_and_get_tokens(const std::string& source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return {parser.parse_program(), tokens};
}

} // namespace test
} // namespace collie
