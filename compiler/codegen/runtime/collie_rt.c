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
 * decimal 格式化四步（移植 Value::to_string 的 Number 小数分支）：
 *   1) NaN                → "NaN"
 *   2) +Inf / -Inf        → "+Infinity" / "-Infinity"
 *   3) 整值且 |v| < 1e15  → 按 long long 打印，无小数点
 *   4) 其余               → "%g"（C++ ostringstream 默认与 printf %g 同为 6 位有效数字）
 */

#include <math.h>
#include <stdio.h>

void collie_rt_print_str(const char* s) {
    fputs(s, stdout);
}

void collie_rt_print_i64(long long v) {
    printf("%lld", v);
}

void collie_rt_print_f64(double v) {
    if (isnan(v)) {
        fputs("NaN", stdout);
        return;
    }
    if (isinf(v)) {
        fputs(v > 0 ? "+Infinity" : "-Infinity", stdout);
        return;
    }
    /* 整值且绝对值 < 1e15：按整数打印，与解释器一致（避免 %g 的 3e+06 科学计数） */
    if (v == floor(v) && fabs(v) < 1e15) {
        printf("%lld", (long long)v);
        return;
    }
    printf("%g", v);
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
