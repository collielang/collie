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
 * 非整数索引陷阱（t122）：
 *   void collie_rt_trap_index_integer(void);  // number/decimal 下标非整数态
 *     （含 NaN）报错退出，对齐解释器 normalize_index "Index must be an integer"
 *   void collie_rt_trap_index_kind(long long tag);
 *     // 动态承载值（kind 7 等）tag 非 0/1 作下标，按 tag 还原 kind 名报错退出，
 *     // 对齐解释器 "Index must be a number, got <kind>"（t150）
 *
 * 数组运行时（t59，同质数组降级用）：
 *   void* collie_rt_arr_new(long long len, long long kind);  // 单块 malloc 数组对象；
 *     kind：0=integer(i64) 1=decimal(double 位模式) 2=bool(0/1) 3=string(指针位模式)
 *     4=array(内层数组指针位模式，嵌套数组 t85) 5=object(实例指针位模式，t100)
 *     6=tribool(三态 0=false/1=unset/2=true，t136)
 *     7=异质动态标量（t139）：槽为 2×len 步长，每元素 {tag,bits} 两 i64
 *   void collie_rt_arr_set_dyn(void* arr, long long i, long long tag, long long bits);
 *     // kind 7 写（t139）：负索引归一化 + 越界陷阱，写 slots[2i]=tag/2i+1=bits
 *   void collie_rt_arr_get_dyn(void* arr, long long i, long long* otag, long long* obits);
 *     // kind 7 读（t139）：同上索引规则，经出参写回 tag/bits
 *   void collie_rt_dyn_index(long long tag, long long bits, long long i,
 *                            long long* otag, long long* obits);
 *     // 动态承载值作索引接收者（t153）：3=串码点索引返单码点子串（tag 3）、
 *     // 4/7=数组按内层运行期 kind 读、其余落 "Only arrays, strings and tuples
 *     // can be indexed, got <kind>" 陷阱；经出参写回 {tag,bits}
 *   void collie_rt_dyn_set(long long tag, long long bits, long long i,
 *                          long long vtag, long long vbits);
 *     // 动态承载值索引写（t153）：4/7 复用 collie_rt_arr_set_num（含 CG7 失配
 *     // 陷阱）、3=串不可变陷阱、其余 "Only arrays can be index-assigned, got <kind>"
 *   const char* collie_rt_dyn_str_trim(long long tag, long long bits,
 *                                      long long mode, const char* name);
 *   const char* collie_rt_dyn_str_substring(long long tag, long long bits,
 *                                           long long start, long long end,
 *                                           const char* name);
 *     // 动态承载值的串方法（t154）：tag 3 复用 collie_rt_str_trim /
 *     // collie_rt_str_substring；实例位（5）落 "Undefined method '<name>'
 *     // on object"，其余位落 "Method '<name>()' is only supported on
 *     // strings, got <kind>"
 *   void collie_rt_dyn_to_number(long long tag, long long bits,
 *                                long long* otag, long long* obits,
 *                                long long mode);
 *     // 动态承载值的 toNumber（t155）：0/1 透传、2→整数 0/1、3 复用
 *     // collie_rt_str_to_num；其余位 mode 1（方法形式）的实例位报
 *     // "Undefined method 'toNumber' on object"，其余报
 *     // "toNumber() cannot convert <kind>"
 *   void collie_rt_dyn_num_guard(long long tag, const char* name);
 *     // 动态承载值的 number 专属方法接收者守卫（t156）：tag 0/1（与 Num 表示
 *     // 重合）放行，其余位落 "Method '<name>()' is only supported on numbers,
 *     // got <kind>"，实例位落 "Undefined method '<name>' on object"
 *   long long collie_rt_arr_get(void* arr, long long i);     // 取 8 字节槽位模式；
 *     负索引 -1 为最后一个元素；越界 stderr 报错后 exit(1)（对齐解释器）
 *   void collie_rt_arr_set(void* arr, long long i, long long bits); // 存槽，同上索引规则
 *   long long collie_rt_arr_len(void* arr);                  // 元素个数
 *   const char* collie_rt_arr_to_str(void* arr);             // malloc 新串，[1, 2, 3] 格式
 *   long long collie_rt_arr_kind(void* arr);                 // 元素 kind（t70 动态域索引读：
 *     kind 0/1 与 number tag 0/1 编码重合，bits+kind 直接拼 number 零转换；
 *     kind 2/3/4/5/6 槽位模式即 Dyn 的 bits 编码，kind 7 经 arr_get_dyn 取回
 *     真实 {tag,bits}——四路统一产出 Dyn（t151，原 kind>=2 陷阱面解锁）
 *   long long collie_rt_dyn_len(long long tag, long long bits, long long mode);
 *     // 动态承载值长度面（t152）：Dyn 上的 .length（mode 0）与 len()（mode 1）
 *     // 按运行期 tag 分派——3=串 UTF-8 码点数 / 4、7=数组元素个数 / 其余按
 *     // 解释器同消息报错退出（拒错编从陷阱）
 *   void collie_rt_arr_set_num(void* arr, long long i, long long tag, long long bits);
 *     // 动态域索引写（t70/t88）：tag==kind 直存（数值系与 bool/str 同规则）；
 *     // integer 写 decimal 数组提升 double 位模式；其余 tag/kind 失配陷阱退出
 *     // （解释器动态异质可容、编译产物同质表示不可，拒错编从陷阱，缺口 CG7）
 *   long long collie_rt_arr_eq(void* lhs, void* rhs);         // 数组深比较（t79）：先比
 *     // len 再逐元素按运行时 kind（数值系混合 double 视图、string strcmp），返 1/0
 *   long long collie_rt_tuple_get(void* names, void* vals, const char* key); // tuple
 *     // 动态键 get（t84）：同质命名 tuple 物化的 names(kind 3)+values 数组按非空名
 *     // strcmp 查找，命中返对应槽 i64 位模式、未命中打 "Undefined tuple field '<key>'"
 *     // + exit(1)（核心消息对齐解释器，位置前缀缺失同越界陷阱）
 *   void* collie_rt_tuple_new(long long len, void* names, void* values); // 动态 tuple
 *     // 对象（t140）：三元不同形状合流/进函数签名用，names=kind3 名字表/values=kind7
 *     // 标量数组，元素数恒 = len
 *   long long collie_rt_tuple_len(void* t);                     // tuple 元素数（t140）
 *   void collie_rt_tuple_get_dyn(void* t, long long i, long long* otag, long long* obits);
 *     // 按位置索引读 {tag,bits}（t140）：负索引归一化 + 越界陷阱（复用 values norm）
 *   void collie_rt_tuple_key_dyn(void* t, const char* key, long long* otag, long long* obits);
 *     // 按名字动态 get() 读 {tag,bits}（t140）：未命中打 "Undefined tuple field '<key>'"
 *   long long collie_rt_tuple_eq(void* lhs, void* rhs);         // tuple 深比较（t140）：
 *     // len + 名字表逐位 + values 交 arr_eq，返 1/0
 *   const char* collie_rt_tuple_to_str(void* t);                // tuple 转串（t140）：
 *     // (1, 2) / (name: Alice) 格式，malloc 新串（不 free，缺口 CG6）
 *
 * 未定义方法/属性陷阱（t102，S39 残余面）：静态类无此成员但后代类有，
 * upcast 后按对象头类 id 分派到定义该成员的后代；动态类即静态类本身
 * （实例无此成员）时解释器运行期报错，编译产物同消息报错退出不错值：
 *   void collie_rt_trap_undefined_method(const char* name);
 *     // 消息核心对齐解释器 "Undefined method 'X' on object"（stderr + exit(1)）
 *   void collie_rt_trap_undefined_property(const char* name);
 *     // 消息核心对齐解释器 "Undefined property 'X' on object"（同上）
 *
 * 动态承载值进类型化槽的类型陷阱（t147）：kind 7 数组元素/object 动态槽
 * 读出的 {tag,bits} 存进 integer/decimal/number/bool/tribool/string 槽时，
 * tag 与目标类型不符即报错退出（解释器 coerce_to_declared 同为运行期报错）：
 *   void collie_rt_trap_dyn_convert(long long tag, const char* target);
 *     // tag 复用数组 kind 编码（0=int/1=dec/2=bool/3=str/4=arr/5=object/
 *     // 6=tribool/7=异质数组），消息核心对齐 "Type mismatch: cannot assign
 *     // <kind> to '<target>' variable"（integer 收小数态用特例文案）
 *
 * 动态承载值参与算术的类型陷阱（t148）：tag 非数值态（0/1 以外）参与
 *   + - * / % 时按解释器同消息报错退出：
 *   void collie_rt_trap_non_numeric_arith(void);
 *     // "runtime error: Arithmetic operands must be numbers"（stderr + exit(1)）
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
 * 动态承载值比较运行时（t149，tag 编码同数组 kind：0=整数 1=小数 2=bool
 * 3=串 4=数组 5=实例 6=tribool 7=异质数组；值 kind 只有运行期可知）：
 *   long long collie_rt_dyn_eq(atag, abits, btag, bbits);
 *     // 复刻解释器 values_equal 接受表，返 0/1，无陷阱臂（该路径不报错）
 *   int collie_rt_dyn_rel(op, atag, abits, btag, bbits);
 *     // op 2='<' 3='<=' 4='>' 5='>='；双侧非数值且非同串时打
 *     // "Comparison operands must be both numbers or both strings" 后 exit(1)
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

/* number → integer 窄化陷阱（t111）：Num 小数态窄化 integer 形参/变量槽/
 * 返回值时报错退出，对齐解释器 coerce_to_declared 的
 * "cannot assign decimal value to 'integer' variable" */
void collie_rt_trap_num_narrow(void) {
    fprintf(stderr, "runtime error: Type mismatch: cannot assign decimal "
                    "value to 'integer' variable\n");
    exit(1);
}

/* 非整数索引陷阱（t122）：number/decimal 下标非整数态（含 NaN）时报错退出，
 * 对齐解释器 normalize_index 的 "Index must be an integer"（裸消息同越界陷阱） */
void collie_rt_trap_index_integer(void) {
    fprintf(stderr, "Index must be an integer\n");
    exit(1);
}

/* 非数值位动态承载值作下标陷阱（t150）：kind 7 数组元素/异质与动态 tuple 索引/
 * object 动态槽读出的 {tag,bits} tag 落在数值域（0/1）以外时参与索引，解释器
 * normalize_index 先判 is_number() 再报错，此处按 tag 还原 kind 名，消息核心
 * 对齐 "Index must be a number, got <kind>"（裸消息同越界陷阱） */
void collie_rt_trap_index_kind(long long tag) {
    const char* kind = "unknown";
    switch ((int)tag) {
        case 2: kind = "bool"; break;
        case 3: kind = "string"; break;
        case 4: kind = "array"; break;
        case 5: kind = "object"; break;
        case 6: kind = "tribool"; break;
        case 7: kind = "array"; break;
        default: break;
    }
    fprintf(stderr, "Index must be a number, got %s\n", kind);
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
 * 4=array(内层数组指针位模式，嵌套数组 t85) 5=object(实例指针位模式，t100)
 * 6=tribool(三态 0/1/2，t136)
 * 7=异质动态标量（t139）：槽为 2×len 步长，每元素 {tag, bits} 两 i64
 *   （tag 复用 kind 编码 0=int/1=dec/2=bool/3=str/5=object(t144)/6=tribool），
 *   承载异质基本类型数组字面量 [1,"two",true] 及含实例混排 [d,1]（t144，
 *   tag 5 bits 存实例 ptr 位模式，print/toString=<object>、== 恒不等）；
 *   len 字段仍为元素个数（非槽个数） */
typedef struct {
    long long len;
    long long kind;
    long long slots[]; /* C99 柔性数组成员 */
} collie_rt_array;

void* collie_rt_arr_new(long long len, long long kind) {
    /* kind 7 每元素两槽（tag+bits），其余单槽 */
    size_t stride = kind == 7 ? 2 : 1;
    collie_rt_array* a = (collie_rt_array*)collie_rt_alloc(
        sizeof(collie_rt_array) + stride * (size_t)len * sizeof(long long));
    a->len = len;
    a->kind = kind;
    memset(a->slots, 0, stride * (size_t)len * sizeof(long long));
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

/* 动态承载值长度面（t152）：Dyn（kind 7 元素读/动态域元素读/object 动态槽/
 * 动态 tuple 索引/算术结果）上的 `.length` 属性与 len() 内建按运行期 tag 分派。
 * 解释器接受表（visitProperty 的 length 分支 / call_builtin_len）：string 按
 * UTF-8 码点、array 按元素个数，其余运行期报错；tag 5 实例位在 codegen 侧由
 * "无类定义 length 字段"守卫保证解释器同为 Undefined property 报错。
 * mode 0 = `.length` 属性，mode 1 = len() 内建（两者报错文案不同）：
 *   0 → "Property 'length' is only supported on strings and arrays, got <kind>"
 *   1 → "len() expects a string or array, got <kind>"
 * tag 复用数组 kind 编码（0=int 1=dec 2=bool 3=str 4=arr 5=object 6=tribool
 * 7=异质数组），kind 名对齐 Value::kind_name（0/1 同报 number、4/7 同报 array） */
long long collie_rt_dyn_len(long long tag, long long bits, long long mode) {
    switch (tag) {
        case 3:
            return collie_rt_str_len((const char*)(intptr_t)bits);
        case 4:
        case 7:
            return collie_rt_arr_len((void*)(intptr_t)bits);
        case 5:
            fprintf(stderr, mode == 0
                                ? "runtime error: Undefined property 'length' on object\n"
                                : "runtime error: len() expects a string or array, got object\n");
            exit(1);
        default:
            break;
    }
    const char* kind = "unknown";
    switch ((int)tag) {
        case 0:
        case 1: kind = "number"; break;
        case 2: kind = "bool"; break;
        case 6: kind = "tribool"; break;
        default: break;
    }
    fprintf(stderr, mode == 0
                        ? "runtime error: Property 'length' is only supported on strings and arrays, got %s\n"
                        : "runtime error: len() expects a string or array, got %s\n",
            kind);
    exit(1);
}

/* 动态域索引写（t70/t88）：写入值带 kind tag（0/1=number 两态，2=bool，3=string），
 * 按数组实际 kind 对齐——tag==kind 直存（数值系与 bool/str 同规则）；integer 写
 * decimal 数组提升（对齐静态路径的 Int→Double 提升）；kind 7 异质数组任意 tag
 * 直接落 {tag,bits} 双步长槽（t151，与 arr_set_dyn 同规则，解释器异质容任意
 * 元素）；其余 tag/kind 失配无法同质承载（解释器动态异质可容，编译产物陷阱
 * 退出不静默错值，缺口 CG7） */
void collie_rt_arr_set_num(void* arr, long long index, long long tag, long long bits) {
    collie_rt_array* a = (collie_rt_array*)arr;
    long long i = collie_rt_arr_norm_index(a, index);
    if (tag == a->kind) {
        a->slots[i] = bits;
        return;
    }
    if (a->kind == 7) { /* 异质数组：任意元素带 tag 入双步长槽 */
        a->slots[2 * i] = tag;
        a->slots[2 * i + 1] = bits;
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

/* 异质动态标量数组读写（t139，kind 7）：每元素 {tag, bits} 两 i64 步长槽。
 * 索引归一化 + 越界陷阱复用 collie_rt_arr_norm_index（len 为元素个数）。
 * 仅 codegen 静态 elem==Dyn 路径调用；经签名/字段动态域透传后 elem 降 Num，
 * 索引读先按 rt_arr_kind 分派、kind 7 同样经此取回 {tag,bits}（t151） */
void collie_rt_arr_set_dyn(void* arr, long long index, long long tag, long long bits) {
    collie_rt_array* a = (collie_rt_array*)arr;
    long long i = collie_rt_arr_norm_index(a, index);
    a->slots[2 * i] = tag;
    a->slots[2 * i + 1] = bits;
}

void collie_rt_arr_get_dyn(void* arr, long long index,
                           long long* otag, long long* obits) {
    collie_rt_array* a = (collie_rt_array*)arr;
    long long i = collie_rt_arr_norm_index(a, index);
    *otag = a->slots[2 * i];
    *obits = a->slots[2 * i + 1];
}

/* 动态承载值的 kind 名（t153 索引陷阱用）：tag 复用数组 kind 编码，
 * 名称对齐解释器 Value::kind_name（0/1 同报 number、4/7 同报 array） */
static const char* collie_rt_dyn_kind_name(long long tag) {
    switch ((int)tag) {
        case 0:
        case 1: return "number";
        case 2: return "bool";
        case 3: return "string";
        case 4:
        case 7: return "array";
        case 5: return "object";
        case 6: return "tribool";
        default: return "unknown";
    }
}

/* 动态承载值作索引接收者（t153）：`dyn[i]` 按运行期 tag 复刻解释器 visitIndex
 * 接受表——tag 3 串按 UTF-8 码点索引、返单码点子串（打 tag 3）；tag 4/7 数组按
 * 内层运行时 kind 读（kind 7 取回真实 {tag,bits}，其余 kind 即 tag、bits 为槽位
 * 模式，故三层嵌套 `a[i][j][k]` 逐级工作）；其余位落解释器同消息陷阱
 * "Only arrays, strings and tuples can be indexed, got <kind>"（tuple 无动态承载
 * 位，此处不会出现）。负索引归一化与越界陷阱复用既有 rt 路径 */
void collie_rt_dyn_index(long long tag, long long bits, long long index,
                         long long* otag, long long* obits) {
    if (tag == 3) {
        *otag = 3;
        *obits = (long long)(intptr_t)collie_rt_str_index(
            (const char*)(intptr_t)bits, index);
        return;
    }
    if (tag == 4 || tag == 7) {
        collie_rt_array* a = (collie_rt_array*)(intptr_t)bits;
        long long i = collie_rt_arr_norm_index(a, index);
        if (a->kind == 7) {
            *otag = a->slots[2 * i];
            *obits = a->slots[2 * i + 1];
            return;
        }
        *otag = a->kind;
        *obits = a->slots[i];
        return;
    }
    fprintf(stderr, "Only arrays, strings and tuples can be indexed, got %s\n",
            collie_rt_dyn_kind_name(tag));
    exit(1);
}

/* 动态承载值索引写（t153）：`dyn[i] = v` 按运行期 tag 复刻解释器
 * visitIndexAssign——tag 4/7 走既有动态域写（kind 对齐/int→dec 提升/kind 7
 * 双步长槽/异类失配 CG7 陷阱均在 collie_rt_arr_set_num 内）；tag 3 串不可变
 * 陷阱；其余 "Only arrays can be index-assigned, got <kind>" */
void collie_rt_dyn_set(long long tag, long long bits, long long index,
                       long long vtag, long long vbits) {
    if (tag == 3) {
        fprintf(stderr, "Strings are immutable, cannot assign to index\n");
        exit(1);
    }
    if (tag == 4 || tag == 7) {
        collie_rt_arr_set_num((void*)(intptr_t)bits, index, vtag, vbits);
        return;
    }
    fprintf(stderr, "Only arrays can be index-assigned, got %s\n",
            collie_rt_dyn_kind_name(tag));
    exit(1);
}

/* 动态承载值的方法接收者陷阱（t154/t156 共用）：复刻解释器两类消息——实例位
 * （tag 5）走方法查找失败分支 "Undefined method '<name>' on object"，其余走
 * "Method '<name>()' is only supported on <accepted>, got <kind>"（accepted 取
 * 解释器该段的措辞，串方法为 strings、number 方法为 numbers；kind 名对齐
 * Value::kind_name）；调用方仅在无任何类定义同名方法时才落到这里 */
static void collie_rt_dyn_method_trap(long long tag, const char* name,
                                      const char* accepted) {
    if (tag == 5) {
        fprintf(stderr, "Undefined method '%s' on object\n", name);
        exit(1);
    }
    fprintf(stderr, "Method '%s()' is only supported on %s, got %s\n",
            name, accepted, collie_rt_dyn_kind_name(tag));
    exit(1);
}

/* 动态承载值的 number 专属方法接收者守卫（t156）：解释器进 number 方法段先判
 * is_number()，故 tag 0/1（与 Num 表示完全重合）放行由 codegen 走既有 Num 双路
 * 降级，其余位落 collie_rt_dyn_method_trap 的 numbers 消息 */
void collie_rt_dyn_num_guard(long long tag, const char* name) {
    if (tag > 1) {
        collie_rt_dyn_method_trap(tag, name, "numbers");
    }
}

const char* collie_rt_dyn_str_trim(long long tag, long long bits, long long mode,
                                   const char* name) {
    if (tag != 3) {
        collie_rt_dyn_method_trap(tag, name, "strings");
    }
    return collie_rt_str_trim((const char*)(intptr_t)bits, (int)mode);
}

/* 动态承载值的 subString：start/end 已由 codegen 侧按解释器的 floor/clamp/
 * NaN 与 -1 特例算好（与静态串接收者同一套），此处只做接收者 tag 分派 */
const char* collie_rt_dyn_str_substring(long long tag, long long bits,
                                        long long start, long long end,
                                        const char* name) {
    if (tag != 3) {
        collie_rt_dyn_method_trap(tag, name, "strings");
    }
    return collie_rt_str_substring((const char*)(intptr_t)bits, start, end);
}

/* collie_rt_str_to_num 定义在文件后段，此处先行声明供 dyn_to_number 调用 */
void collie_rt_str_to_num(const char* s, long long* otag, long long* obits);

/* 动态承载值的 toNumber（t155）：复刻解释器 to_number_value 的接受表——
 * tag 0/1 数值位原样透传（整数态保持整数表示）、tag 2 bool 转整数 0/1、
 * tag 3 串解析复用 collie_rt_str_to_num（剥空白/严格 Infinity/纯整数精确/
 * strtod 等价，不可解析得 NaN）；其余位报错：方法形式（mode 1）的实例位走
 * 解释器实例接收者的方法查找失败分支 "Undefined method 'toNumber' on object"，
 * 其余与内建形式（mode 0）报 "toNumber() cannot convert <kind>"（kind 名对齐
 * Value::kind_name）；结果经出参写回 {tag,bits}（同 str_to_num 的 ABI 规避） */
void collie_rt_dyn_to_number(long long tag, long long bits, long long* otag,
                             long long* obits, long long mode) {
    if (tag == 0 || tag == 1) {
        *otag = tag;
        *obits = bits;
        return;
    }
    if (tag == 2) {
        *otag = 0;
        *obits = bits != 0;
        return;
    }
    if (tag == 3) {
        collie_rt_str_to_num((const char*)(intptr_t)bits, otag, obits);
        return;
    }
    if (tag == 5 && mode == 1) {
        fprintf(stderr, "Undefined method 'toNumber' on object\n");
        exit(1);
    }
    fprintf(stderr, "toNumber() cannot convert %s\n",
            collie_rt_dyn_kind_name(tag));
    exit(1);
}

/* ---- 未定义方法/属性陷阱（t102，S39 残余面）---- */

/* 父类静态类型调子类特有成员：静态类无此方法但后代类有，codegen 按对象头
 * 类 id switch 到定义该方法的后代单态化实例；动态类即静态类本身（实例无
 * 此方法）时解释器运行期报 RuntimeError，编译产物同消息报错退出不错值 */
void collie_rt_trap_undefined_method(const char* name) {
    fprintf(stderr, "runtime error: Undefined method '%s' on object\n", name);
    exit(1);
}

/* 同上：静态类无此字段但后代类有，switch 到后代字段槽；动态类即静态类
 * 本身（实例无此字段）时解释器报 RuntimeError，编译产物同消息报错退出 */
void collie_rt_trap_undefined_property(const char* name) {
    fprintf(stderr, "runtime error: Undefined property '%s' on object\n", name);
    exit(1);
}

/* 动态承载值进类型化槽的运行期类型校验陷阱（t147）：kind7 数组元素/object
 * 动态槽的 tag 编译期不可定，解释器 coerce_to_declared 在赋值点按实际 kind
 * 校验报错；编译产物按同一接受表守卫，未命中落此陷阱（消息核心对齐解释器
 * "Type mismatch: cannot assign <kind> to '<type>' variable"，integer 收
 * 小数态沿用解释器特例文案 "decimal value"）。
 * tag 复用数组 kind 编码：0=int/1=dec/2=bool/3=str/4=arr/5=object/6=tribool/
 * 7=异质数组（4、7 同报 array） */
void collie_rt_trap_dyn_convert(long long tag, const char* target) {
    const char* kind;
    switch (tag) {
        case 0:
        case 1:
            if (tag == 1 && strcmp(target, "integer") == 0) {
                kind = "decimal value"; // 解释器 integer 特例文案
                break;
            }
            kind = "number";
            break;
        case 2:  kind = "bool";     break;
        case 3:  kind = "string";   break;
        case 4:
        case 7:  kind = "array";    break;
        case 5:  kind = "object";   break;
        case 6:  kind = "tribool";  break;
        default: kind = "value";    break;
    }
    fprintf(stderr, "runtime error: Type mismatch: cannot assign %s to "
                    "'%s' variable\n", kind, target);
    exit(1);
}

/* 动态承载值参与算术的类型陷阱（t148）：kind 7 数组元素/动态 tuple 索引/
 * object 动态承载槽读出的 {tag,bits} 参与 + - * / % 时，解释器 eval_arithmetic
 * 要求双侧皆为数值（integer/decimal/number），bool/tribool/string/实例/数组
 * 一律运行期报错；编译产物按同一规则在 tag 守卫未命中时走此陷阱（消息核心
 * 对齐解释器 "Arithmetic operands must be numbers"，前缀差异同既有陷阱约定） */
void collie_rt_trap_non_numeric_arith(void) {
    fprintf(stderr, "runtime error: Arithmetic operands must be numbers\n");
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
    /* 异质动态标量数组（kind 7，t139）：每元素 {tag,bits} 步长 2。
     * 一侧 kind7 一侧非 kind7 恒不等：kind7 必含 ≥2 种基本类型（异质），
     * 非 kind7 数组同质，逐元素至少一元素 kind 失配（对齐解释器 values_equal
     * 按 Value::kind 分支）；两侧 kind7 逐元素按 tag/bits 比较 */
    if (l->kind == 7 || r->kind == 7) {
        if (l->kind != 7 || r->kind != 7) {
            return 0;
        }
        long long k;
        for (k = 0; k < l->len; ++k) {
            long long lt = l->slots[2 * k];
            long long lb = l->slots[2 * k + 1];
            long long rt_ = r->slots[2 * k];
            long long rb = r->slots[2 * k + 1];
            if (lt == rt_) {
                if (lt == 1) { /* decimal 按 double 值 */
                    double x, y;
                    memcpy(&x, &lb, sizeof x);
                    memcpy(&y, &rb, sizeof y);
                    if (!(x == y)) return 0;
                } else if (lt == 3) { /* string strcmp 内容 */
                    if (strcmp((const char*)(intptr_t)lb,
                               (const char*)(intptr_t)rb) != 0) return 0;
                } else if (lt == 5) { /* object：实例恒不等（对齐解释器
                                      * values_equal 无 Instance 分支，同 kind 5
                                      * 单数组 t100 同规则，t144） */
                    return 0;
                } else if (lt == 4 || lt == 7) { /* 数组位（动态域嵌套数组读出 /
                                      * 嵌套数组写 kind 7 槽，t153）：bits 即内层
                                      * 数组 ptr，递归深比较——此前按指针直比，
                                      * 结构相等的两个内层数组会静默判不等 */
                    if (!collie_rt_arr_eq((void*)(intptr_t)lb,
                                          (void*)(intptr_t)rb)) return 0;
                } else { /* int/bool/tribool：位模式即值 */
                    if (lb != rb) return 0;
                }
            } else if ((lt == 0 || lt == 1) && (rt_ == 0 || rt_ == 1)) {
                /* int × decimal 混合表示：double 视图（5 == 5.0） */
                double x, y;
                if (lt == 0) { x = (double)lb; } else { memcpy(&x, &lb, sizeof x); }
                if (rt_ == 0) { y = (double)rb; } else { memcpy(&y, &rb, sizeof y); }
                if (!(x == y)) return 0;
            } else if ((lt == 2 && rt_ == 6) || (lt == 6 && rt_ == 2)) {
                /* bool × tribool：bool 0/1 → 三态 0/2 后直比（t136 同规则） */
                long long xt = lt == 2 ? lb * 2 : lb;
                long long yt = rt_ == 2 ? rb * 2 : rb;
                if (xt != yt) return 0;
            } else {
                return 0; /* tag 失配（str/bool 对数值等）恒不等 */
            }
        }
        return 1;
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
            } else if (l->kind == 5) { /* object：实例恒不等（对齐解释器 values_equal 无 Instance 分支，t100） */
                return 0;
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
        } else if ((l->kind == 2 && r->kind == 6) ||
                   (l->kind == 6 && r->kind == 2)) {
            /* bool × tribool 混合 kind（t136）：bool 槽 0/1 映射三态 0/2 后直比
             * （对齐解释器 values_equal 的 tri_of(bool)：false→0/true→2，
             * unset=1 与任何 bool 恒不等） */
            long long lt = l->kind == 2 ? lb * 2 : lb;
            long long rt_ = r->kind == 2 ? rb * 2 : rb;
            if (lt != rt_) return 0;
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
            case 6: /* tribool：0/1/2 → false/unset/true（对齐解释器 Value::to_string Tribool 分支，t136） */
                collie_rt_sb_append(&out, &n, &cap,
                                    a->slots[i] == 2 ? "true"
                                    : a->slots[i] == 1 ? "unset" : "false");
                break;
            case 3: /* string：指针位模式还原 */
                collie_rt_sb_append(&out, &n, &cap, (const char*)(intptr_t)a->slots[i]);
                break;
            case 5: /* object：实例转串固定 <object>（对齐解释器 Value::to_string Instance 分支，t100） */
                collie_rt_sb_append(&out, &n, &cap, "<object>");
                break;
            case 7: { /* 异质动态标量（t139）：逐元素 {tag,bits} 按 tag 格式化，
                       * 串不加引号（对齐解释器 Array 分支递归 to_string） */
                long long tag = a->slots[2 * i];
                long long bits = a->slots[2 * i + 1];
                switch ((int)tag) {
                    case 0: /* integer */
                        snprintf(tmp, sizeof tmp, "%lld", bits);
                        collie_rt_sb_append(&out, &n, &cap, tmp);
                        break;
                    case 1: { /* decimal */
                        double v;
                        memcpy(&v, &bits, sizeof v);
                        collie_rt_format_f64(tmp, sizeof tmp, v);
                        collie_rt_sb_append(&out, &n, &cap, tmp);
                        break;
                    }
                    case 2: /* bool */
                        collie_rt_sb_append(&out, &n, &cap, bits ? "true" : "false");
                        break;
                    case 6: /* tribool */
                        collie_rt_sb_append(&out, &n, &cap,
                                            bits == 2 ? "true"
                                            : bits == 1 ? "unset" : "false");
                        break;
                    case 5: /* object：实例固定 <object>（t144，对齐 kind 5 t100） */
                        collie_rt_sb_append(&out, &n, &cap, "<object>");
                        break;
                    case 4:
                    case 7: /* 数组位（动态域嵌套数组读出 / 嵌套数组写入 kind 7 槽，
                             * t153）：bits 即内层数组 ptr，递归转串——此前落入
                             * 默认串分支会把数组对象指针当 char* 打印成静默错值 */
                        collie_rt_sb_append(&out, &n, &cap,
                                            collie_rt_arr_to_str((void*)(intptr_t)bits));
                        break;
                    default: /* 3 = string：指针位模式还原，原样不加引号 */
                        collie_rt_sb_append(&out, &n, &cap,
                                            (const char*)(intptr_t)bits);
                        break;
                }
                break;
            }
            default: /* 4 = array：指针位模式还原后递归转串（嵌套数组，t85） */
                collie_rt_sb_append(&out, &n, &cap,
                                    collie_rt_arr_to_str((void*)(intptr_t)a->slots[i]));
                break;
        }
    }
    collie_rt_sb_append(&out, &n, &cap, "]");
    return out;
}

/* ---- 动态 tuple 运行时对象（t140）----
 * 三元/==? 分支返回不同形状 tuple、tuple 进函数签名（形参/返回值）等跨静态
 * 展开边界的场景：形状运行期才定，无法静态解构，故引入堆对象承载。
 * 布局 {len, names, values}——names 复用 kind 3 字符串数组（无名槽存 ""），
 * values 复用 kind 7 异质动态标量数组（每元素 {tag,bits}，t139）。二者元素数
 * 恒等 = len。仅 print/toString/插值、length、非常量索引（读得 Dyn）、动态键
 * get()（读得 Dyn）、==/!= 深比较消费；含 Obj/Arr/嵌套 tuple 元素的 tuple 在
 * codegen 物化处即拒编（is_dyn_scalar 不含），进算术/类型化槽经 Dyn 既有面拒编。 */
typedef struct {
    long long len;
    void* names;  /* collie_rt_array kind 3：与 values 等长的名字表，无名槽为空串 */
    void* values; /* collie_rt_array kind 7：每元素 {tag,bits} 步长 2 */
} collie_rt_tuple;

void* collie_rt_tuple_new(long long len, void* names, void* values) {
    collie_rt_tuple* t = (collie_rt_tuple*)collie_rt_alloc(sizeof(collie_rt_tuple));
    t->len = len;
    t->names = names;
    t->values = values;
    return t;
}

long long collie_rt_tuple_len(void* t) {
    return ((collie_rt_tuple*)t)->len;
}

/* 按位置索引读动态标量（负索引归一化 + 越界陷阱复用 values 数组的 norm_index，
 * 消息 "Index N out of range (size M)" 与解释器 tuple 索引一致） */
void collie_rt_tuple_get_dyn(void* t, long long index,
                             long long* otag, long long* obits) {
    collie_rt_tuple* tp = (collie_rt_tuple*)t;
    collie_rt_array* v = (collie_rt_array*)tp->values;
    long long i = collie_rt_arr_norm_index(v, index);
    *otag = v->slots[2 * i];
    *obits = v->slots[2 * i + 1];
}

/* 按名字动态 get()：非空名 strcmp 扫描 names，命中读对应 values {tag,bits}，
 * 未命中打 "Undefined tuple field '<key>'" + exit(1)（核心消息对齐解释器） */
void collie_rt_tuple_key_dyn(void* t, const char* key,
                             long long* otag, long long* obits) {
    collie_rt_tuple* tp = (collie_rt_tuple*)t;
    const collie_rt_array* nm = (const collie_rt_array*)tp->names;
    const collie_rt_array* v = (collie_rt_array*)tp->values;
    long long i;
    for (i = 0; i < nm->len; ++i) {
        const char* name = (const char*)(intptr_t)nm->slots[i];
        if (name && name[0] != '\0' && strcmp(name, key) == 0) {
            *otag = v->slots[2 * i];
            *obits = v->slots[2 * i + 1];
            return;
        }
    }
    fprintf(stderr, "Undefined tuple field '%s'\n", key);
    exit(1);
}

/* 深比较（对齐解释器 values_equal Tuple 分支）：先比元素数，再逐位比名字表
 * （含无名槽空串），最后 values 两个 kind 7 数组交给 collie_rt_arr_eq 逐元素
 * {tag,bits} 比较（数值系 double 视图、string strcmp、bool/tribool 加宽）。
 * 元素含不可比较类（Obj）在物化处已拒编，此处无需 Instance 分支。返 1/0 */
long long collie_rt_tuple_eq(void* lhs, void* rhs) {
    const collie_rt_tuple* a = (const collie_rt_tuple*)lhs;
    const collie_rt_tuple* b = (const collie_rt_tuple*)rhs;
    if (a->len != b->len) {
        return 0;
    }
    const collie_rt_array* an = (const collie_rt_array*)a->names;
    const collie_rt_array* bn = (const collie_rt_array*)b->names;
    long long i;
    for (i = 0; i < a->len; ++i) {
        const char* x = (const char*)(intptr_t)an->slots[i];
        const char* y = (const char*)(intptr_t)bn->slots[i];
        if (strcmp(x, y) != 0) {
            return 0;
        }
    }
    return collie_rt_arr_eq(a->values, b->values);
}

/* 单元素 {tag,bits} 追加到增长缓冲（对齐解释器 Value::to_string 各标量分支、
 * 与 arr_to_str kind 7 case 同格式：整数 %lld、小数四步格式、bool/tribool
 * 文字、string 原样不加引号） */
static void collie_rt_append_tagged(char** buf, size_t* n, size_t* cap,
                                    long long tag, long long bits) {
    char tmp[64];
    switch ((int)tag) {
        case 0: /* integer */
            snprintf(tmp, sizeof tmp, "%lld", bits);
            collie_rt_sb_append(buf, n, cap, tmp);
            break;
        case 1: { /* decimal */
            double v;
            memcpy(&v, &bits, sizeof v);
            collie_rt_format_f64(tmp, sizeof tmp, v);
            collie_rt_sb_append(buf, n, cap, tmp);
            break;
        }
        case 2: /* bool */
            collie_rt_sb_append(buf, n, cap, bits ? "true" : "false");
            break;
        case 6: /* tribool */
            collie_rt_sb_append(buf, n, cap,
                                bits == 2 ? "true" : bits == 1 ? "unset" : "false");
            break;
        default: /* 3 = string：指针位模式还原，原样不加引号 */
            collie_rt_sb_append(buf, n, cap, (const char*)(intptr_t)bits);
            break;
    }
}

/* tuple 转串：形如 (1, 2) / (name: Alice)（对齐解释器 Value::to_string Tuple 分支：
 * ", " 分隔、命名字段前缀 "name: "、string 元素不加引号、空元组 "()"） */
const char* collie_rt_tuple_to_str(void* t) {
    collie_rt_tuple* tp = (collie_rt_tuple*)t;
    const collie_rt_array* nm = (const collie_rt_array*)tp->names;
    const collie_rt_array* v = (collie_rt_array*)tp->values;
    size_t cap = 64;
    size_t n = 0;
    char* out = collie_rt_alloc(cap);
    out[0] = '\0';
    collie_rt_sb_append(&out, &n, &cap, "(");
    long long i;
    for (i = 0; i < tp->len; ++i) {
        if (i > 0) {
            collie_rt_sb_append(&out, &n, &cap, ", ");
        }
        const char* name = (const char*)(intptr_t)nm->slots[i];
        if (name && name[0] != '\0') {
            collie_rt_sb_append(&out, &n, &cap, name);
            collie_rt_sb_append(&out, &n, &cap, ": ");
        }
        collie_rt_append_tagged(&out, &n, &cap, v->slots[2 * i], v->slots[2 * i + 1]);
    }
    collie_rt_sb_append(&out, &n, &cap, ")");
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

/* 动态承载值判等（t149）：kind 7 数组元素/异质与动态 tuple 索引/object 动态
 * 承载槽读出的 {tag,bits}，值 kind 只有运行期可知，故按 tag 复刻解释器
 * values_equal 的接受表——tribool 参与时双侧限 tribool/bool（bool 加宽为三态
 * false→0/true→2）三态直比、与其它类型恒不等；kind 失配恒 false；Number
 * （tag 0/1，含跨 tag 混合表示）下沉 num_cmp（双整数直比、混合 double 视图，
 * 5 == 5.0）；串位 strcmp 内容比较；数组位（kind 4 单类型 / kind 7 异质）
 * arr_eq 递归深比较；实例位（tag 5）与未列 tag 恒 false（解释器 values_equal
 * 无 Instance 分支）。解释器此路径不报错，故本函数无陷阱臂。返 1/0 */
long long collie_rt_dyn_eq(long long atag, long long abits,
                           long long btag, long long bbits) {
    if (atag == 6 || btag == 6) {
        if (atag != 6 && atag != 2) return 0;
        if (btag != 6 && btag != 2) return 0;
        long long a = atag == 2 ? abits * 2 : abits;
        long long b = btag == 2 ? bbits * 2 : bbits;
        return a == b;
    }
    if (atag != btag) {
        if ((atag == 0 || atag == 1) && (btag == 0 || btag == 1)) {
            return collie_rt_num_cmp(0, atag, abits, btag, bbits);
        }
        return 0;
    }
    switch (atag) {
        case 0:
        case 1:
            return collie_rt_num_cmp(0, atag, abits, btag, bbits);
        case 2:
            return abits == bbits; /* bool：位模式即值 */
        case 3:
            return strcmp((const char*)(intptr_t)abits,
                          (const char*)(intptr_t)bbits) == 0;
        case 4:
        case 7:
            return collie_rt_arr_eq((void*)(intptr_t)abits,
                                    (void*)(intptr_t)bbits);
        default:
            return 0; /* 实例位等：解释器 default 恒 false */
    }
}

/* 动态承载值关系比较（t149）：解释器 eval_comparison 要求双侧同为数值
 * （双整数精确、混合 double 视图）或同串（逐字节字典序），否则报
 * "Comparison operands must be both numbers or both strings"；tag 运行期才定，
 * 故数值态（tag 0/1）下沉 num_cmp，串位（tag 3）走 strcmp，其余（bool/
 * tribool/数组/实例位与跨 kind 混排）落陷阱报错退出（消息核心对齐解释器，
 * 位置前缀缺失同既有陷阱约定；陷阱路径 stdout 无增量）。
 * op 编码同 num_cmp：2='<' 3='<=' 4='>' 5='>='。返 1/0 */
int collie_rt_dyn_rel(long long op, long long atag, long long abits,
                      long long btag, long long bbits) {
    if ((atag == 0 || atag == 1) && (btag == 0 || btag == 1)) {
        return collie_rt_num_cmp(op, atag, abits, btag, bbits);
    }
    if (atag == 3 && btag == 3) {
        int c = strcmp((const char*)(intptr_t)abits,
                      (const char*)(intptr_t)bbits);
        switch ((int)op) {
            case 2:  return c < 0;
            case 3:  return c <= 0;
            case 4:  return c > 0;
            default: return c >= 0;
        }
    }
    fprintf(stderr, "runtime error: Comparison operands must be both numbers or both strings\n");
    exit(1);
    return 0;
}

/* number 转串：整数表示 %lld（对齐 BigInt::to_string 在 i64 域的输出），
 * 小数表示四步格式；复用既有转串接口，malloc 新串不 free（缺口 CG6）。
 * tag 2/3/6 为异质 tuple Dyn 动态标量扩展（t138，编码同数组 kind）：
 * number 值恒为 tag 0/1，两域共用此分派；tag 4/7 为动态域读出的数组元素
 * （t151，缺口 CG9 消除）：bits 即数组 ptr，转串走 arr_to_str */
const char* collie_rt_num_to_str(long long tag, long long bits) {
    switch (tag) {
        case 0:
            return collie_rt_i64_to_str(bits);
        case 2:
            return bits ? "true" : "false"; /* Dyn bool，静态串勿 free */
        case 3:
            return (const char*)(intptr_t)bits; /* Dyn string：bits 即串 ptr */
        case 6:
            /* Dyn tribool：0=false/1=unset/2=true（对齐 Value::to_string） */
            return bits == 1 ? "unset" : (bits == 2 ? "true" : "false");
        case 5:
            return "<object>"; /* Dyn 实例（t144）：静态串勿 free，对齐 Value::to_string Instance */
        case 4:
        case 7:
            /* Dyn 数组（t151）：bits 即数组 ptr，格式与静态域转串一致 */
            return collie_rt_arr_to_str((void*)(intptr_t)bits);
        default:
            return collie_rt_f64_to_str(collie_rt_num_as_f64(tag, bits));
    }
}

/* number 打印：与 print_i64/print_f64 同格式（对齐 Value::to_string Number 分支）；
 * tag 2/3/6 为 Dyn 动态标量（t138），bool/tribool 串与解释器 to_string 对齐 */
void collie_rt_print_num(long long tag, long long bits) {
    switch (tag) {
        case 0:
            printf("%lld", bits);
            return;
        case 2:
            fputs(bits ? "true" : "false", stdout);
            return;
        case 3:
            collie_rt_print_str((const char*)(intptr_t)bits);
            return;
        case 6:
            fputs(bits == 1 ? "unset" : (bits == 2 ? "true" : "false"), stdout);
            return;
        case 5:
            fputs("<object>", stdout); /* Dyn 实例（t144，对齐 print 的 kind 5 t100） */
            return;
        case 4:
        case 7:
            /* Dyn 数组（t151）：print 输出与静态域数组一致 [1, 2, 3] */
            collie_rt_print_str(collie_rt_arr_to_str((void*)(intptr_t)bits));
            return;
        default:
            collie_rt_print_f64(collie_rt_num_as_f64(tag, bits));
            return;
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
