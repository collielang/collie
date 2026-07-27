/**
 * @file big_int.h
 * @brief 任意精度有符号整数（Python 式自动扩容，经作者确认的数字类型设计）
 *
 * 表示：符号位 + base 2^32 的 limb 数组（little-endian，最高 limb 非零）。
 * 供解释器 integer 类型使用：+ - * % 精确运算，除法由语言层走 IEEE 754
 * 小数路径（Python true division），此处仅提供截断除/floor 取模。
 *
 * 性能取舍：schoolbook 乘法 O(n^2)、二进制长除 O(n·bits)，对树遍历
 * 解释器足够；后续如需可替换为更快算法，接口不变。
 */
#ifndef COLLIE_INTERPRETER_BIG_INT_H
#define COLLIE_INTERPRETER_BIG_INT_H

#include <cstdint>
#include <string>
#include <vector>

namespace collie {

class BigInt {
public:
    /// 零值
    BigInt() = default;

    /// 从机器整数构造
    explicit BigInt(long long v);

    /**
     * @brief 从十进制字符串构造（可带 '+'/'-' 前缀，其余必须全为数字）
     * @throws std::invalid_argument 字符串为空或含非法字符
     */
    static BigInt from_decimal_string(const std::string& text);

    bool is_zero() const { return sign_ == 0; }
    /// 符号：-1 / 0 / 1
    int sign() const { return sign_; }

    BigInt negated() const;

    /// 三路比较：a<b 返回 -1，a==b 返回 0，a>b 返回 1
    static int compare(const BigInt& a, const BigInt& b);

    BigInt operator+(const BigInt& rhs) const;
    BigInt operator-(const BigInt& rhs) const;
    BigInt operator*(const BigInt& rhs) const;

    /**
     * @brief 截断除法（C/C++ 语义，商向零取整），除数为零时抛异常
     * @throws std::domain_error 除数为零
     */
    static void divmod_trunc(const BigInt& a, const BigInt& b,
                             BigInt& quotient, BigInt& remainder);

    /**
     * @brief floor 取模（Python 语义：结果符号与除数一致），除数为零时抛异常
     * @throws std::domain_error 除数为零
     */
    BigInt floor_mod(const BigInt& divisor) const;

    /// 转为 double（超出范围得 ±Infinity；大数存在精度损失，仅用于混合运算）
    double to_double() const;

    /// 十进制字符串（负数带 '-'，无前导零）
    std::string to_string() const;

    bool operator==(const BigInt& rhs) const { return compare(*this, rhs) == 0; }
    bool operator!=(const BigInt& rhs) const { return compare(*this, rhs) != 0; }

private:
    int sign_ = 0;                  ///< -1/0/1；为 0 时 limbs_ 为空
    std::vector<uint32_t> limbs_;   ///< base 2^32，little-endian，最高位非零

    void normalize();  ///< 去除高位零 limb，空则置 sign_ = 0

    // 幅值（无符号）运算辅助
    static int cmp_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b);
    static std::vector<uint32_t> add_mag(const std::vector<uint32_t>& a,
                                         const std::vector<uint32_t>& b);
    /// 要求 a >= b
    static std::vector<uint32_t> sub_mag(const std::vector<uint32_t>& a,
                                         const std::vector<uint32_t>& b);
    static std::vector<uint32_t> mul_mag(const std::vector<uint32_t>& a,
                                         const std::vector<uint32_t>& b);
    /// 二进制长除：返回商，rem 输出余数（均为幅值）
    static std::vector<uint32_t> divmod_mag(const std::vector<uint32_t>& a,
                                            const std::vector<uint32_t>& b,
                                            std::vector<uint32_t>& rem);
};

} // namespace collie

#endif // COLLIE_INTERPRETER_BIG_INT_H
