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
 * byte/word 范围与移位量陷阱（t69）：
 *   void collie_rt_trap_bit_range(const char* name, long long max, long long got);
 *     // byte/word 赋值点越界（0-255/0-65535），stderr 报错后 exit(1)
 *   void collie_rt_trap_shift_count(void);  // 移位量越界 0-63，同上
 *
 * 数组运行时（t59，同质数组降级用）：
 *   void* collie_rt_arr_new(long long len, long long kind);  // 单块 malloc 数组对象；
 *     kind：0=integer(i64) 1=decimal(double 位模式) 2=bool(0/1) 3=string(指针位模式)
 *     4=array(内层数组指针位模式，嵌套数组 t85)
 *   long long collie_rt_arr_get(void* arr, long long i);     // 取 8 字节槽位模式；
 *     负索引 -1 为最后一个元素；越界 stderr 报错后 exit(1)（对齐解释器）
 *   void collie_rt_arr_set(void* arr, long long i, long long bits); // 存槽，同上索引规则
 *   long long collie_rt_arr_len(void* arr);                  // 元素个数
 *   const char* collie_rt_arr_to_str(void* arr);             // malloc 新串，[1, 2, 3] 格式
 *   long long collie_rt_arr_kind(void* arr);                 // 元素 kind（t70 动态域索引读：
 *     kind 0/1 与 number tag 0/1 编码重合，bits+kind 直接拼 number 零转换）
 *   void collie_rt_arr_set_num(void* arr, long long i, long long tag, long long bits);
 *     // 动态域索引写（t70/t88）：tag==kind 直存（数值系与 bool/str 同规则）；
 *     // integer 写 decimal 数组提升 double 位模式；其余 tag/kind 失配陷阱退出
 *     // （解释器动态异质可容、编译产物同质表示不可，拒错编从陷阱，缺口 CG7）
 *   void collie_rt_trap_arr_kind(long long kind);             // 动态域索引读 kind>=2
 *     // 陷阱（t88）：bool/str/嵌套数组经透传后元素静态类型不可定，报错退出
 *     // 不错值（缺口 CG9，解释器动态类型可行）
 *   long long collie_rt_arr_eq(void* lhs, void* rhs);         // 数组深比较（t79）：先比
 *     // len 再逐元素按运行时 kind（数值系混合 double 视图、string strcmp），返 1/0
 *   long long collie_rt_tuple_get(void* names, void* vals, const char* key); // tuple
 *     // 动态键 get（t84）：同质命名 tuple 物化的 names(kind 3)+values 数组按非空名
 *     // strcmp 查找，命中返对应槽 i64 位模式、未命中打 "Undefined tuple field '<key>'"
 *     // + exit(1)（核心消息对齐解释器，位置前缀缺失同越界陷阱）
 *
 * 类实例运行时（t60，class 降级用）：
 *   void* collie_rt_obj_new(long long size);                 // 字段块 malloc + 零初始化；
 *     size 由 codegen 按字段类型累计上界给定（Num 16、其余 8，t74），
 *     字段初始值紧随 new 写入
 *
 * number 双表示运行时（t62，缺口 CG5 收窄）：值 = tag + 8 字节位模式
 *   （tag 0=整数 i64 / 1=小数 double 位模式），算术/比较/转串集中在运行时
 *   单点对齐解释器 eval_arithmetic/eval_comparison/Value::to_string：
 *   void collie_rt_num_arith(op, atag, abits, btag, bbits, otag, obits);
 *     // op 0=+ 1=- 2=* 3=/ 4=% 5=一元负号（b 忽略）；双整数精确 + - * %
 *     // （i64 溢出走 CG1 陷阱）、除法恒小数、混合走 double、除零 IEEE 754；
 *     // 结果经出参写回（16 字节 struct 返回在 Win x64 走隐藏指针，出参避开 ABI 错配）
 *   int collie_rt_num_cmp(op, atag, abits, btag, bbits);
 *     // op 0='==' 1='!=' 2='<' 3='<=' 4='>' 5='>='，返 0/1；NaN 比较与 '=='
 *     // 恒 false、'!=' 恒 true（IEEE 语义对齐解释器）
 *   const char* collie_rt_num_to_str(long long tag, long long bits); // malloc 新串
 *   void collie_rt_print_num(long long tag, long long bits);         // 格式对齐 to_string
 *
 * toNumber 字符串解析（t63，内建 toNumber/方法形式共用）：
 *   void collie_rt_str_to_num(s, otag, obits);
 *     // 复刻解释器 to_number_value 的 string 分支，解析失败返 NaN 不报错；
 *     // 超 i64 纯整数串走 CG1 陷阱（解释器 BigInt 精确，不静默错编）
 *
 * decimal 格式化四步（移植 Value::to_string 的 Number 小数分支）：
 *   1) NaN                → "NaN"
 *   2) +Inf / -Inf        → "+Infinity" / "-Infinity"
 *   3) 整值且 |v| < 1e15  → 按 long long 打印，无小数点
 *   4) 其余               → "%g"（C++ ostringstream 默认与 printf %g 同为 6 位有效数字）
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
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

/* ---- byte/word 范围与移位量陷阱（t69）---- */

/* byte/word 赋值点范围校验失败：对齐解释器 coerce_to_declared 的
 * "Value out of range" 报错（byte 0-255 / word 0-65535，无回绕） */
void collie_rt_trap_bit_range(const char* name, long long max, long long got) {
    fprintf(stderr, "runtime error: Value out of range for '%s' "
                    "(expected 0-%lld, got %lld)\n", name, max, got);
    exit(1);
}

/* 移位量越界（<0 或 >63）：对齐解释器 eval_bitwise 的运行期报错，
 * 回避 LLVM shl/ashr 移位量越界的 poison */
void collie_rt_trap_shift_count(void) {
    fprintf(stderr, "runtime error: Shift count must be in range 0-63\n");
    exit(1);
}

/* ---- 数组运行时（t59）---- */

/* 数组对象：单块 malloc 的 len + kind + 8 字节槽；codegen 以不透明 ptr 持有，
 * 指针拷贝即引用语义（对齐解释器 shared_ptr<ArrayStorage>）。
 * kind：0=integer(i64 直存) 1=decimal(double 位模式) 2=bool(0/1) 3=string(指针位模式)
 * 4=array(内层数组指针位模式，嵌套数组 t85) */
typedef struct {
    long long len;
    long long kind;
    long long slots[]; /* C99 柔性数组成员 */
} collie_rt_array;

void* collie_rt_arr_new(long long len, long long kind) {
    collie_rt_array* a = (collie_rt_array*)collie_rt_alloc(
        sizeof(collie_rt_array) + (size_t)len * sizeof(long long));
    a->len = len;
    a->kind = kind;
    memset(a->slots, 0, (size_t)len * sizeof(long long));
    return a;
}

/* 负索引归一化 + 越界报错退出（对齐解释器 normalize_index：-1 为最后一个元素） */
static long long collie_rt_arr_norm_index(const collie_rt_array* a, long long index) {
    long long i = index;
    if (i < 0) {
        i += a->len;
    }
    if (i < 0 || i >= a->len) {
        fprintf(stderr, "Index %lld out of range (size %lld)\n", index, a->len);
        exit(1);
    }
    return i;
}

long long collie_rt_arr_get(void* arr, long long index) {
    collie_rt_array* a = (collie_rt_array*)arr;
    return a->slots[collie_rt_arr_norm_index(a, index)];
}

void collie_rt_arr_set(void* arr, long long index, long long bits) {
    collie_rt_array* a = (collie_rt_array*)arr;
    a->slots[collie_rt_arr_norm_index(a, index)] = bits;
}

long long collie_rt_arr_len(void* arr) {
    return ((collie_rt_array*)arr)->len;
}

/* 元素 kind（t70）：动态域（数组经函数签名边界后元素类型静态不可知）的索引读
 * 据此拼 number：kind 0=integer/1=decimal 与 number tag 编码重合，零转换；
 * 进动态域的数组 codegen 静态保证 kind ∈ {0,1}（bool/str 数组作实参/返回值拒编） */
long long collie_rt_arr_kind(void* arr) {
    return ((collie_rt_array*)arr)->kind;
}

/* 动态域索引写（t70/t88）：写入值带 kind tag（0/1=number 两态，2=bool，3=string），
 * 按数组实际 kind 对齐——tag==kind 直存（数值系与 bool/str 同规则）；integer 写
 * decimal 数组提升（对齐静态路径的 Int→Double 提升）；其余 tag/kind 失配无法
 * 同质承载（解释器动态异质可容，编译产物陷阱退出不静默错值，缺口 CG7） */
void collie_rt_arr_set_num(void* arr, long long index, long long tag, long long bits) {
    collie_rt_array* a = (collie_rt_array*)arr;
    long long i = collie_rt_arr_norm_index(a, index);
    if (tag == a->kind) {
        a->slots[i] = bits;
        return;
    }
    if (tag == 0 && a->kind == 1) { /* integer 写 decimal 数组：提升 double */
        double v = (double)bits;
        long long out;
        memcpy(&out, &v, sizeof out);
        a->slots[i] = out;
        return;
    }
    fprintf(stderr, "runtime error: array element type mismatch "
                    "(compiled arrays are homogeneous, gap CG7)\n");
    exit(1);
}

/* 动态域索引读 kind>=2 陷阱（t88）：bool/str/嵌套数组经透传（签名/字段/返回值）
 * 后元素静态类型不可定，codegen 读出无法承载——报错退出不错值（缺口 CG9，
 * 解释器动态类型可行；print/len/== 不经此路径，全 kind 天然工作） */
void collie_rt_trap_arr_kind(long long kind) {
    const char* what = kind == 2 ? "bool" : kind == 3 ? "string" : "nested";
    fprintf(stderr, "runtime error: reading %s array element in dynamic "
                    "context (element type not statically known, gap CG9)\n", what);
    exit(1);
}

/* 数组深比较（t79）：对齐解释器 values_equal Array 分支——先比 len 再逐元素。
 * 同 kind：integer/bool i64 直比、decimal 位模式还原 double 后按值比较
 * （NaN != NaN 与解释器 as_number() == 一致）、string strcmp 内容比较、
 * array 递归深比较（嵌套数组，t85）；
 * kind {0,1} 混合按 double 视图（对齐解释器 Number 混合表示 5 == 5.0）；
 * bool/string/array 与其它 kind 配对元素 kind 不等恒不等；len==0 天然相等。返 1/0 */
long long collie_rt_arr_eq(void* lhs, void* rhs) {
    const collie_rt_array* l = (const collie_rt_array*)lhs;
    const collie_rt_array* r = (const collie_rt_array*)rhs;
    if (l->len != r->len) {
        return 0;
    }
    long long i;
    for (i = 0; i < l->len; ++i) {
        long long lb = l->slots[i];
        long long rb = r->slots[i];
        if (l->kind == r->kind) {
            if (l->kind == 1) { /* decimal：按 double 值比较 */
                double x, y;
                memcpy(&x, &lb, sizeof x);
                memcpy(&y, &rb, sizeof y);
                if (!(x == y)) return 0;
            } else if (l->kind == 3) { /* string：指针位模式还原后内容比较 */
                if (strcmp((const char*)(intptr_t)lb,
                           (const char*)(intptr_t)rb) != 0) return 0;
            } else if (l->kind == 4) { /* array：递归深比较（嵌套数组，t85） */
                if (!collie_rt_arr_eq((void*)(intptr_t)lb,
                                      (void*)(intptr_t)rb)) return 0;
            } else { /* integer / bool：位模式即值 */
                if (lb != rb) return 0;
            }
        } else if ((l->kind == 0 || l->kind == 1) &&
                   (r->kind == 0 || r->kind == 1)) {
            /* integer × decimal 混合 kind：double 视图比较 */
            double x, y;
            if (l->kind == 0) { x = (double)lb; } else { memcpy(&x, &lb, sizeof x); }
            if (r->kind == 0) { y = (double)rb; } else { memcpy(&y, &rb, sizeof y); }
            if (!(x == y)) return 0;
        } else {
            return 0; /* bool/string 与其它 kind 配对：元素 kind 不等恒 false */
        }
    }
    return 1;
}

/* tuple 动态键 get（t84）：同质命名 tuple 物化的 names 数组（kind 3 string）+
 * values 数组（元素 kind）按名字查找——非空名 strcmp 命中即返对应 values 槽 i64
 * 位模式（调用侧 bits_to_elem 还原），未命中打 "Undefined tuple field '<key>'"
 * 后 exit(1)（核心消息对齐解释器 RuntimeError，位置前缀缺失同数组越界陷阱既定
 * 分歧）。names/values 等长逐位对应；空名槽跳过（对齐解释器非空名匹配） */
long long collie_rt_tuple_get(void* names, void* vals, const char* key) {
    const collie_rt_array* nm = (const collie_rt_array*)names;
    const collie_rt_array* vl = (const collie_rt_array*)vals;
    long long i;
    for (i = 0; i < nm->len; ++i) {
        const char* name = (const char*)(intptr_t)nm->slots[i];
        if (name && name[0] != '\0' && strcmp(name, key) == 0) {
            return vl->slots[i];
        }
    }
    fprintf(stderr, "Undefined tuple field '%s'\n", key);
    exit(1);
}

/* 追加一段字节到增长缓冲（arr_to_str 专用；旧块不 free，缺口 CG6 同其它 malloc 串） */
static void collie_rt_sb_append(char** buf, size_t* n, size_t* cap, const char* s) {
    size_t add = strlen(s);
    if (*n + add + 1 > *cap) {
        while (*n + add + 1 > *cap) {
            *cap *= 2;
        }
        char* grown = collie_rt_alloc(*cap);
        memcpy(grown, *buf, *n);
        *buf = grown;
    }
    memcpy(*buf + *n, s, add + 1); /* 含结尾 '\0' */
    *n += add;
}

/* 数组转串：形如 [1, 2, 3]，元素按 kind 格式化（对齐解释器 Value::to_string 的
 * Array 分支：整数 %lld、小数四步格式、bool true/false、字符串原样不加引号） */
const char* collie_rt_arr_to_str(void* arr) {
    collie_rt_array* a = (collie_rt_array*)arr;
    size_t cap = 64;
    size_t n = 0;
    char* out = collie_rt_alloc(cap);
    out[0] = '\0';
    collie_rt_sb_append(&out, &n, &cap, "[");
    long long i;
    for (i = 0; i < a->len; ++i) {
        if (i > 0) {
            collie_rt_sb_append(&out, &n, &cap, ", ");
        }
        char tmp[64];
        switch ((int)a->kind) {
            case 0: /* integer */
                snprintf(tmp, sizeof tmp, "%lld", a->slots[i]);
                collie_rt_sb_append(&out, &n, &cap, tmp);
                break;
            case 1: { /* decimal：位模式还原 double 后四步格式化 */
                double v;
                memcpy(&v, &a->slots[i], sizeof v);
                collie_rt_format_f64(tmp, sizeof tmp, v);
                collie_rt_sb_append(&out, &n, &cap, tmp);
                break;
            }
            case 2: /* bool */
                collie_rt_sb_append(&out, &n, &cap, a->slots[i] ? "true" : "false");
                break;
            case 3: /* string：指针位模式还原 */
                collie_rt_sb_append(&out, &n, &cap, (const char*)(intptr_t)a->slots[i]);
                break;
            default: /* 4 = array：指针位模式还原后递归转串（嵌套数组，t85） */
                collie_rt_sb_append(&out, &n, &cap,
                                    collie_rt_arr_to_str((void*)(intptr_t)a->slots[i]));
                break;
        }
    }
    collie_rt_sb_append(&out, &n, &cap, "]");
    return out;
}

/* 类实例块（t60）：codegen 按 LLVM struct 布局读写字段，运行时只管分配；
 * 零初始化仅防御（字段必有初始值，new 降级会逐字段覆写）；不 free，缺口 CG6 */
void* collie_rt_obj_new(long long size) {
    char* p = collie_rt_alloc((size_t)size);
    memset(p, 0, (size_t)size);
    return p;
}

/* ---- number 双表示运行时（t62，缺口 CG5 收窄）---- */

/* number 值 = tag + 8 字节位模式：tag 0=整数（i64 直存）、1=小数（double 位模式）。
 * 语义集中在此单点对齐解释器；整数域 i64 溢出走既有 CG1 陷阱（解释器 BigInt
 * 自动扩容，编译产物边界内一致、越界显式报错不静默错值） */

static double collie_rt_num_as_f64(long long tag, long long bits) {
    if (tag == 0) {
        return (double)bits; /* 整数表示的 double 视图（对齐 Value::as_number） */
    }
    double v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

static long long collie_rt_f64_bits(double v) {
    long long bits;
    memcpy(&bits, &v, sizeof bits);
    return bits;
}

/* op：0=+ 1=- 2=* 3=/ 4=% 5=一元负号（b 忽略）；结果经 otag/obits 写回 */
void collie_rt_num_arith(long long op, long long atag, long long abits,
                         long long btag, long long bbits,
                         long long* otag, long long* obits) {
    if (op == 5) { /* 一元负号：整数走溢出检查（-LLONG_MIN 超范围），小数直接取负 */
        if (atag == 0) {
            if (abits == LLONG_MIN) {
                collie_rt_trap_int_overflow();
            }
            *otag = 0;
            *obits = -abits;
        } else {
            *otag = 1;
            *obits = collie_rt_f64_bits(-collie_rt_num_as_f64(atag, abits));
        }
        return;
    }
    /* 双整数精确路径（对齐解释器 eval_arithmetic）：+ - * 溢出陷阱；
     * % floor 语义；/ 恒产小数与取模除零均落到下方 double 路径 */
    if (atag == 0 && btag == 0) {
        long long a = abits;
        long long b = bbits;
        switch ((int)op) {
            case 0: /* + */
                if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
                    collie_rt_trap_int_overflow();
                }
                *otag = 0;
                *obits = a + b;
                return;
            case 1: /* - */
                if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
                    collie_rt_trap_int_overflow();
                }
                *otag = 0;
                *obits = a - b;
                return;
            case 2: /* *：除法预判法，四象限分支覆盖 LLONG_MIN × -1 边界 */
                if (a > 0) {
                    if (b > 0 ? a > LLONG_MAX / b : b < LLONG_MIN / a) {
                        collie_rt_trap_int_overflow();
                    }
                } else if (a < 0) {
                    if (b > 0 ? a < LLONG_MIN / b : b < LLONG_MAX / a) {
                        collie_rt_trap_int_overflow();
                    }
                }
                *otag = 0;
                *obits = a * b;
                return;
            case 4: /* %：floor 语义（结果符号与除数一致）；除零落 double 路径得 NaN */
                if (b != 0) {
                    long long r;
                    if (b == -1) {
                        r = 0; /* a % -1 恒 0，绕开 LLONG_MIN / -1 硬件陷阱 */
                    } else {
                        r = a % b;
                        if (r != 0 && ((r < 0) != (b < 0))) {
                            r += b;
                        }
                    }
                    *otag = 0;
                    *obits = r;
                    return;
                }
                break;
            default: /* 3 = '/'：恒产小数（Python 式 true division） */
                break;
        }
    }
    /* double 路径（混合表示 / 除法 / 整数取模除零）：对齐解释器同名分支 */
    {
        double a = collie_rt_num_as_f64(atag, abits);
        double b = collie_rt_num_as_f64(btag, bbits);
        double r;
        switch ((int)op) {
            case 0: r = a + b; break;
            case 1: r = a - b; break;
            case 2: r = a * b; break;
            case 3: r = a / b; break; /* 除零 IEEE 754：±Infinity / NaN */
            default: /* 4 = %：floor 语义；除数为 0 得 NaN（fmod(x, 0) 对齐） */
                if (b == 0.0) {
                    r = NAN;
                } else {
                    r = fmod(a, b);
                    if (r != 0.0 && ((r < 0.0) != (b < 0.0))) {
                        r += b;
                    }
                }
                break;
        }
        *otag = 1;
        *obits = collie_rt_f64_bits(r);
    }
}

/* op：0='==' 1='!=' 2='<' 3='<=' 4='>' 5='>='，返 0/1；双整数走 i64 精确比较，
 * 其余走 double 视图（NaN：全序比较与 '==' 均 false、'!=' 为 true，C 比较
 * 运算符天然满足，与解释器 eval_comparison/values_equal 一致） */
int collie_rt_num_cmp(long long op, long long atag, long long abits,
                      long long btag, long long bbits) {
    if (atag == 0 && btag == 0) {
        long long a = abits;
        long long b = bbits;
        switch ((int)op) {
            case 0:  return a == b;
            case 1:  return a != b;
            case 2:  return a < b;
            case 3:  return a <= b;
            case 4:  return a > b;
            default: return a >= b;
        }
    }
    {
        double a = collie_rt_num_as_f64(atag, abits);
        double b = collie_rt_num_as_f64(btag, bbits);
        switch ((int)op) {
            case 0:  return a == b;
            case 1:  return a != b;
            case 2:  return a < b;
            case 3:  return a <= b;
            case 4:  return a > b;
            default: return a >= b;
        }
    }
}

/* number 转串：整数表示 %lld（对齐 BigInt::to_string 在 i64 域的输出），
 * 小数表示四步格式；复用既有转串接口，malloc 新串不 free（缺口 CG6） */
const char* collie_rt_num_to_str(long long tag, long long bits) {
    if (tag == 0) {
        return collie_rt_i64_to_str(bits);
    }
    return collie_rt_f64_to_str(collie_rt_num_as_f64(tag, bits));
}

/* number 打印：与 print_i64/print_f64 同格式（对齐 Value::to_string Number 分支） */
void collie_rt_print_num(long long tag, long long bits) {
    if (tag == 0) {
        printf("%lld", bits);
    } else {
        collie_rt_print_f64(collie_rt_num_as_f64(tag, bits));
    }
}

/* toNumber 字符串解析（t63）：复刻解释器 to_number_value 的 string 分支——
 * 剥两端空白 → 严格大小写 "Infinity"/"+Infinity"/"-Infinity" → 纯整数串
 * （可带单个 +/- 前缀）精确整数表示（超 i64 走 CG1 陷阱：解释器 BigInt
 * 精确，i64 承载不了则报错退出不静默错编）→ strtod 等价 std::stod
 * （须消费到剥空白后的串尾且结果有限，"1.5f" 残留/"infinity" 宽松
 * 拼写均失败）→ 一切失败返 NaN 不报错 */
void collie_rt_str_to_num(const char* s, long long* otag, long long* obits) {
    size_t b = 0;
    size_t e = strlen(s);
    while (b < e && isspace((unsigned char)s[b])) ++b;
    while (e > b && isspace((unsigned char)s[e - 1])) --e;
    size_t len = e - b;

    /* 特殊形式严格大小写匹配（对齐解释器："infinity" 应得 NaN） */
    if ((len == 8 && strncmp(s + b, "Infinity", 8) == 0) ||
        (len == 9 && strncmp(s + b, "+Infinity", 9) == 0)) {
        *otag = 1;
        *obits = collie_rt_f64_bits(INFINITY);
        return;
    }
    if (len == 9 && strncmp(s + b, "-Infinity", 9) == 0) {
        *otag = 1;
        *obits = collie_rt_f64_bits(-INFINITY);
        return;
    }
    /* 纯整数形式（可带符号）→ 整数表示；解释器走 BigInt 不丢精度，
     * 此处 strtoll 超 i64 范围（ERANGE）触发整数溢出陷阱 */
    if (len > 0) {
        size_t digits_begin = (s[b] == '+' || s[b] == '-') ? 1 : 0;
        int all_digits = digits_begin < len;
        for (size_t i = digits_begin; i < len; ++i) {
            if (!isdigit((unsigned char)s[b + i])) {
                all_digits = 0;
                break;
            }
        }
        if (all_digits) {
            char* endp = NULL;
            errno = 0;
            long long v = strtoll(s + b, &endp, 10);
            if (errno == ERANGE) {
                collie_rt_trap_int_overflow();
            }
            *otag = 0;
            *obits = v;
            return;
        }
        /* strtod 行为等价 std::stod（支持 .5 前导点/科学计数法/十六进制
         * 浮点）：尾部残留即失败（endp 未到串尾，剥后空白处 strtod 自行
         * 停步）；非有限（"inf"/"nan" 宽松拼写、上溢）与 ERANGE 下溢
         * （stod 抛 out_of_range）均视为不可解析 */
        {
            char* endp = NULL;
            errno = 0;
            double n = strtod(s + b, &endp);
            if (endp == s + e && errno != ERANGE && isfinite(n)) {
                *otag = 1;
                *obits = collie_rt_f64_bits(n);
                return;
            }
        }
    }
    /* 空串/不可解析 → NaN（对齐解释器：不报错） */
    *otag = 1;
    *obits = collie_rt_f64_bits(NAN);
}
