/**
 * @file collie_rt.c
 * @brief Collie 运行时垫片库（M6 t53，纯 C 静态库）
 *
 * 目的：codegen 的 print 降级不再直连 printf 拼格式串，而是逐参调用本库接口，
 * 使编译产物的输出与树遍历解释器（compiler/interpreter/value.h Value::to_string）
 * 逐字节一致。首版覆盖 print 的标量格式化（缺口 CG2 的 print 部分）。
 *
 * 纯 C 实现：clang 编链 .ll 时默认只带 C 运行时，不自动带 C++ 标准库；
 * 用纯 C 避免 libc++/libstdc++ 依赖问题。Release /MT 静态 CRT 与主工程对齐。
 *
 * 接口约定（供 codegen 声明并调用）：
 *   void collie_rt_print_str(const char* s);   // 原样输出
 *   void collie_rt_print_i64(long long v);      // integer（i64 表示）
 *   void collie_rt_print_f64(double v);         // decimal，四步格式化对齐解释器
 *   void collie_rt_print_bool(int v);           // 非 0 → true，0 → false
 *   void collie_rt_print_sep(void);             // 参数间分隔：单个空格
 *   void collie_rt_print_newline(void);         // 一行结束：换行
 *
 * 字符串运行时（t54，拼接 + toString 降级用）：
 *   const char* collie_rt_concat(const char* a, const char* b);  // malloc 新串
 *   const char* collie_rt_i64_to_str(long long v);               // malloc 新串
 *   const char* collie_rt_f64_to_str(double v);                  // malloc 新串，四步格式
 *   const char* collie_rt_bool_to_str(int v);                    // 静态串，勿 free
 *   注：malloc 产物不 free（缺口 CG6：短生命周期编译产物暂容忍泄漏）
 *
 * 字符串比较（t55，六种比较运算降级用）：
 *   int collie_rt_strcmp(const char* a, const char* b);  // strcmp 语义（<0/0/>0）
 *
 * 字符串 length / 索引（t56，UTF-8 码点，对齐解释器 utf8_length/utf8_char_at）：
 *   long long collie_rt_str_len(const char* s);                    // 码点数
 *   const char* collie_rt_str_index(const char* s, long long i);   // malloc 单码点子串；
 *     负索引 -1 为最后一个码点；越界 stderr 报错后 exit(1)（对齐解释器 RuntimeError）
 *
 * 字符串方法 trim / subString（t57，对齐解释器 visitMethodCall string 分支）：
 *   const char* collie_rt_str_trim(const char* s, int mode);  // malloc 新串；
 *     只剥空格与 Tab，mode 0=两端/1=左/2=右
 *   const char* collie_rt_str_substring(const char* s, long long start, long long end);
 *     // malloc 新串；UTF-8 码点区间 [start,end)，end==-1 取 length，越界 clamp
 *
 * 整数溢出陷阱（t58，缺口 CG1）：
 *   void collie_rt_trap_int_overflow(void);  // stderr 报错后 exit(1)；
 *     codegen 的 i64 加/减/乘/一元负号经 s*.with.overflow 检查，溢出即调此陷阱，
 *     把静默回绕错值变为显式运行期报错（解释器 BigInt 任意精度无溢出）
 *
 * decimal 格式化四步（移植 Value::to_string 的 Number 小数分支）：
 *   1) NaN                → "NaN"
 *   2) +Inf / -Inf        → "+Infinity" / "-Infinity"
 *   3) 整值且 |v| < 1e15  → 按 long long 打印，无小数点
 *   4) 其余               → "%g"（C++ ostringstream 默认与 printf %g 同为 6 位有效数字）
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* decimal 四步格式化写入 buf（print_f64 与 f64_to_str 共享，保两路径输出一致） */
static void collie_rt_format_f64(char* buf, size_t size, double v) {
    if (isnan(v)) {
        snprintf(buf, size, "NaN");
    } else if (isinf(v)) {
        snprintf(buf, size, v > 0 ? "+Infinity" : "-Infinity");
    } else if (v == floor(v) && fabs(v) < 1e15) {
        /* 整值且绝对值 < 1e15：按整数打印（避免 %g 的 3e+06 科学计数） */
        snprintf(buf, size, "%lld", (long long)v);
    } else {
        snprintf(buf, size, "%g", v);
    }
}

/* malloc 失败直接终止（编译产物无恢复路径，与解释器 bad_alloc 行为等效） */
static char* collie_rt_alloc(size_t n) {
    char* p = (char*)malloc(n);
    if (!p) {
        fputs("collie_rt: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

void collie_rt_print_str(const char* s) {
    fputs(s, stdout);
}

void collie_rt_print_i64(long long v) {
    printf("%lld", v);
}

void collie_rt_print_f64(double v) {
    char buf[64];
    collie_rt_format_f64(buf, sizeof buf, v);
    fputs(buf, stdout);
}

void collie_rt_print_bool(int v) {
    fputs(v ? "true" : "false", stdout);
}

void collie_rt_print_sep(void) {
    fputc(' ', stdout);
}

void collie_rt_print_newline(void) {
    fputc('\n', stdout);
}

/* ---- 字符串运行时（t54）---- */

const char* collie_rt_concat(const char* a, const char* b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* out = collie_rt_alloc(la + lb + 1);
    memcpy(out, a, la);
    memcpy(out + la, b, lb + 1); /* 含结尾 '\0' */
    return out;
}

const char* collie_rt_i64_to_str(long long v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", v);
    size_t n = strlen(buf) + 1;
    char* out = collie_rt_alloc(n);
    memcpy(out, buf, n);
    return out;
}

const char* collie_rt_f64_to_str(double v) {
    char buf[64];
    collie_rt_format_f64(buf, sizeof buf, v);
    size_t n = strlen(buf) + 1;
    char* out = collie_rt_alloc(n);
    memcpy(out, buf, n);
    return out;
}

const char* collie_rt_bool_to_str(int v) {
    return v ? "true" : "false"; /* 静态串，调用方勿 free */
}

/* ---- 字符串比较（t55）---- */

/* 逐字节字典序（unsigned char 域），与解释器 std::string 比较语义一致；
 * codegen 拿返回值与 0 做 icmp 实现六种比较，只用符号不依赖幅度 */
int collie_rt_strcmp(const char* a, const char* b) {
    return strcmp(a, b);
}

/* ---- 字符串 length / 索引（t56，UTF-8 码点）---- */

/* UTF-8 首字节 → 码点字节长度（非法字节按 1 防御前进，照抄解释器 utf8_char_length） */
static size_t collie_rt_utf8_char_length(unsigned char c) {
    if ((c & 0x80) == 0)    return 1; /* ASCII */
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

long long collie_rt_str_len(const char* s) {
    long long count = 0;
    size_t n = strlen(s);
    size_t i = 0;
    while (i < n) {
        i += collie_rt_utf8_char_length((unsigned char)s[i]);
        ++count;
    }
    return count;
}

const char* collie_rt_str_index(const char* s, long long index) {
    long long size = collie_rt_str_len(s);
    long long i = index;
    if (i < 0) {
        i += size; /* 负索引：-1 表示最后一个码点（对齐解释器 normalize_index） */
    }
    if (i < 0 || i >= size) {
        fprintf(stderr, "Index %lld out of range (size %lld)\n", index, size);
        exit(1);
    }
    size_t byte = 0;
    long long seen;
    for (seen = 0; seen < i; ++seen) {
        byte += collie_rt_utf8_char_length((unsigned char)s[byte]);
    }
    size_t char_len = collie_rt_utf8_char_length((unsigned char)s[byte]);
    char* out = collie_rt_alloc(char_len + 1);
    memcpy(out, s + byte, char_len);
    out[char_len] = '\0';
    return out;
}

/* ---- 字符串方法 trim / subString（t57）---- */

/* trim 系列：只剥空格与 Tab（对齐解释器 is_blank，见 03-character.md）；
 * mode 0=两端（trim）、1=左（trimLeft）、2=右（trimRight），纯字节操作 */
const char* collie_rt_str_trim(const char* s, int mode) {
    size_t begin = 0;
    size_t end = strlen(s);
    if (mode != 2) { /* 非 trimRight → 剥左端 */
        while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) { ++begin; }
    }
    if (mode != 1) { /* 非 trimLeft → 剥右端 */
        while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) { --end; }
    }
    size_t n = end - begin;
    char* out = collie_rt_alloc(n + 1);
    memcpy(out, s + begin, n);
    out[n] = '\0';
    return out;
}

/* 码点序号 → 字节偏移（idx 不超码点数，照抄解释器 utf8_byte_offset） */
static size_t collie_rt_utf8_byte_offset(const char* s, long long idx) {
    size_t byte = 0;
    long long seen;
    for (seen = 0; seen < idx; ++seen) {
        byte += collie_rt_utf8_char_length((unsigned char)s[byte]);
    }
    return byte;
}

/* subString：UTF-8 码点区间 [start, end)，end==-1 取 length（缺省 end 的传转）；
 * 越界 clamp 截断、start >= end 得空串，对齐解释器 subString（NaN 特例属
 * Double 域，codegen 侧参数限 Int 已拒编，此处无需处理） */
const char* collie_rt_str_substring(const char* s, long long start, long long end) {
    long long size = collie_rt_str_len(s);
    if (end == -1) { end = size; } /* 特判仅 -1；其它负值照常 clamp 到 0 */
    if (start < 0) { start = 0; }
    if (start > size) { start = size; }
    if (end < 0) { end = 0; }
    if (end > size) { end = size; }
    if (start >= end) {
        char* empty = collie_rt_alloc(1);
        empty[0] = '\0';
        return empty;
    }
    size_t from = collie_rt_utf8_byte_offset(s, start);
    size_t to = collie_rt_utf8_byte_offset(s, end);
    size_t n = to - from;
    char* out = collie_rt_alloc(n + 1);
    memcpy(out, s + from, n);
    out[n] = '\0';
    return out;
}

/* ---- 整数溢出陷阱（t58，缺口 CG1）---- */

/* i64 算术溢出：解释器 integer 为 BigInt 任意精度永不溢出，编译产物降为
 * i64 后超范围无法算对；显式报错退出，绝不静默回绕错值 */
void collie_rt_trap_int_overflow(void) {
    fprintf(stderr, "runtime error: integer overflow "
                    "(compiled code uses 64-bit integers, gap CG1)\n");
    exit(1);
}
