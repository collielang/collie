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
