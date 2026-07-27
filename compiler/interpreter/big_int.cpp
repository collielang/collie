/**
 * @file big_int.cpp
 * @brief 任意精度有符号整数实现（见 big_int.h 的设计说明）
 */
#include "big_int.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace collie {

namespace {

constexpr uint64_t kLimbBase = 1ull << 32;

/// 幅值乘单个小整数再加小整数（from_decimal_string 按 9 位块累积用）
void mul_add_small(std::vector<uint32_t>& limbs, uint32_t mul, uint32_t add) {
    uint64_t carry = add;
    for (uint32_t& limb : limbs) {
        uint64_t cur = static_cast<uint64_t>(limb) * mul + carry;
        limb = static_cast<uint32_t>(cur & 0xFFFFFFFFull);
        carry = cur >> 32;
    }
    while (carry != 0) {
        limbs.push_back(static_cast<uint32_t>(carry & 0xFFFFFFFFull));
        carry >>= 32;
    }
}

/// 幅值短除单个小整数，返回余数（to_string 反复除 1e9 用）
uint32_t divmod_small(std::vector<uint32_t>& limbs, uint32_t divisor) {
    uint64_t rem = 0;
    for (size_t i = limbs.size(); i-- > 0;) {
        uint64_t cur = (rem << 32) | limbs[i];
        limbs[i] = static_cast<uint32_t>(cur / divisor);
        rem = cur % divisor;
    }
    while (!limbs.empty() && limbs.back() == 0) {
        limbs.pop_back();
    }
    return static_cast<uint32_t>(rem);
}

} // namespace

BigInt::BigInt(long long v) {
    if (v == 0) return;
    sign_ = v > 0 ? 1 : -1;
    // 先转无符号再取反，避免 LLONG_MIN 取负溢出
    uint64_t mag = v > 0 ? static_cast<uint64_t>(v)
                         : ~static_cast<uint64_t>(v) + 1ull;
    limbs_.push_back(static_cast<uint32_t>(mag & 0xFFFFFFFFull));
    if (mag >> 32) {
        limbs_.push_back(static_cast<uint32_t>(mag >> 32));
    }
}

BigInt BigInt::from_decimal_string(const std::string& text) {
    size_t pos = 0;
    int sign = 1;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        if (text[pos] == '-') sign = -1;
        ++pos;
    }
    if (pos >= text.size()) {
        throw std::invalid_argument("BigInt: empty digit string");
    }
    BigInt result;
    // 按 9 位十进制一块累积：limbs = limbs * 10^k + 块值
    while (pos < text.size()) {
        size_t chunk_len = std::min<size_t>(9, text.size() - pos);
        uint32_t chunk = 0;
        uint32_t chunk_base = 1;
        for (size_t i = 0; i < chunk_len; ++i, ++pos) {
            char c = text[pos];
            if (c < '0' || c > '9') {
                throw std::invalid_argument("BigInt: invalid digit character");
            }
            chunk = chunk * 10 + static_cast<uint32_t>(c - '0');
            chunk_base *= 10;
        }
        mul_add_small(result.limbs_, chunk_base, chunk);
    }
    result.sign_ = result.limbs_.empty() ? 0 : sign;
    result.normalize();
    return result;
}

void BigInt::normalize() {
    while (!limbs_.empty() && limbs_.back() == 0) {
        limbs_.pop_back();
    }
    if (limbs_.empty()) {
        sign_ = 0;
    }
}

BigInt BigInt::negated() const {
    BigInt r = *this;
    r.sign_ = -r.sign_;
    return r;
}

int BigInt::cmp_mag(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

int BigInt::compare(const BigInt& a, const BigInt& b) {
    if (a.sign_ != b.sign_) {
        return a.sign_ < b.sign_ ? -1 : 1;
    }
    if (a.sign_ == 0) return 0;
    int mag = cmp_mag(a.limbs_, b.limbs_);
    return a.sign_ > 0 ? mag : -mag;
}

std::vector<uint32_t> BigInt::add_mag(const std::vector<uint32_t>& a,
                                      const std::vector<uint32_t>& b) {
    std::vector<uint32_t> out;
    out.reserve(std::max(a.size(), b.size()) + 1);
    uint64_t carry = 0;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        uint64_t cur = carry;
        if (i < a.size()) cur += a[i];
        if (i < b.size()) cur += b[i];
        out.push_back(static_cast<uint32_t>(cur & 0xFFFFFFFFull));
        carry = cur >> 32;
    }
    if (carry) out.push_back(static_cast<uint32_t>(carry));
    return out;
}

std::vector<uint32_t> BigInt::sub_mag(const std::vector<uint32_t>& a,
                                      const std::vector<uint32_t>& b) {
    std::vector<uint32_t> out;
    out.reserve(a.size());
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int64_t cur = static_cast<int64_t>(a[i]) - borrow -
                      (i < b.size() ? static_cast<int64_t>(b[i]) : 0);
        if (cur < 0) {
            cur += static_cast<int64_t>(kLimbBase);
            borrow = 1;
        } else {
            borrow = 0;
        }
        out.push_back(static_cast<uint32_t>(cur));
    }
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

std::vector<uint32_t> BigInt::mul_mag(const std::vector<uint32_t>& a,
                                      const std::vector<uint32_t>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<uint32_t> out(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b.size(); ++j) {
            uint64_t cur = static_cast<uint64_t>(a[i]) * b[j] + out[i + j] + carry;
            out[i + j] = static_cast<uint32_t>(cur & 0xFFFFFFFFull);
            carry = cur >> 32;
        }
        size_t k = i + b.size();
        while (carry != 0) {
            uint64_t cur = out[k] + carry;
            out[k] = static_cast<uint32_t>(cur & 0xFFFFFFFFull);
            carry = cur >> 32;
            ++k;
        }
    }
    while (!out.empty() && out.back() == 0) out.pop_back();
    return out;
}

std::vector<uint32_t> BigInt::divmod_mag(const std::vector<uint32_t>& a,
                                         const std::vector<uint32_t>& b,
                                         std::vector<uint32_t>& rem) {
    // 二进制长除：从最高 bit 到最低 bit，余数左移一位并带入当前 bit，
    // 够减则减去除数并在商的对应 bit 置 1。正确性优先，性能足够解释器使用。
    rem.clear();
    if (cmp_mag(a, b) < 0) {
        rem = a;
        return {};
    }
    std::vector<uint32_t> quot(a.size(), 0);
    for (size_t i = a.size(); i-- > 0;) {
        for (int bit = 31; bit >= 0; --bit) {
            // rem = rem << 1 | 当前 bit
            uint32_t carry = (a[i] >> bit) & 1u;
            for (uint32_t& limb : rem) {
                uint32_t next_carry = limb >> 31;
                limb = (limb << 1) | carry;
                carry = next_carry;
            }
            if (carry) rem.push_back(carry);
            if (cmp_mag(rem, b) >= 0) {
                rem = sub_mag(rem, b);
                quot[i] |= (1u << bit);
            }
        }
    }
    while (!quot.empty() && quot.back() == 0) quot.pop_back();
    return quot;
}

BigInt BigInt::operator+(const BigInt& rhs) const {
    if (sign_ == 0) return rhs;
    if (rhs.sign_ == 0) return *this;
    BigInt out;
    if (sign_ == rhs.sign_) {
        out.limbs_ = add_mag(limbs_, rhs.limbs_);
        out.sign_ = sign_;
    } else {
        int mag = cmp_mag(limbs_, rhs.limbs_);
        if (mag == 0) return BigInt();
        if (mag > 0) {
            out.limbs_ = sub_mag(limbs_, rhs.limbs_);
            out.sign_ = sign_;
        } else {
            out.limbs_ = sub_mag(rhs.limbs_, limbs_);
            out.sign_ = rhs.sign_;
        }
    }
    out.normalize();
    return out;
}

BigInt BigInt::operator-(const BigInt& rhs) const {
    return *this + rhs.negated();
}

BigInt BigInt::operator*(const BigInt& rhs) const {
    if (sign_ == 0 || rhs.sign_ == 0) return BigInt();
    BigInt out;
    out.limbs_ = mul_mag(limbs_, rhs.limbs_);
    out.sign_ = sign_ == rhs.sign_ ? 1 : -1;
    out.normalize();
    return out;
}

void BigInt::divmod_trunc(const BigInt& a, const BigInt& b,
                          BigInt& quotient, BigInt& remainder) {
    if (b.sign_ == 0) {
        throw std::domain_error("BigInt: division by zero");
    }
    if (a.sign_ == 0) {
        quotient = BigInt();
        remainder = BigInt();
        return;
    }
    std::vector<uint32_t> rem_mag;
    std::vector<uint32_t> quot_mag = divmod_mag(a.limbs_, b.limbs_, rem_mag);
    quotient = BigInt();
    quotient.limbs_ = std::move(quot_mag);
    quotient.sign_ = quotient.limbs_.empty() ? 0 : (a.sign_ == b.sign_ ? 1 : -1);
    remainder = BigInt();
    remainder.limbs_ = std::move(rem_mag);
    remainder.sign_ = remainder.limbs_.empty() ? 0 : a.sign_;  // 截断除余数随被除数
    quotient.normalize();
    remainder.normalize();
}

BigInt BigInt::floor_mod(const BigInt& divisor) const {
    BigInt q, r;
    divmod_trunc(*this, divisor, q, r);
    // floor 语义（Python 风格）：非零余数与除数异号时补一个除数
    if (!r.is_zero() && r.sign() != divisor.sign()) {
        r = r + divisor;
    }
    return r;
}

double BigInt::to_double() const {
    if (sign_ == 0) return 0.0;
    double mag = 0.0;
    // 从高 limb 到低 limb 累积：mag = mag * 2^32 + limb
    for (size_t i = limbs_.size(); i-- > 0;) {
        mag = mag * static_cast<double>(kLimbBase) + static_cast<double>(limbs_[i]);
        if (std::isinf(mag)) break;  // 超出 double 范围，直接饱和为 Infinity
    }
    return sign_ > 0 ? mag : -mag;
}

std::string BigInt::to_string() const {
    if (sign_ == 0) return "0";
    // 反复短除 1e9，得到从低到高的 9 位十进制块
    std::vector<uint32_t> mag = limbs_;
    std::vector<uint32_t> chunks;
    while (!mag.empty()) {
        chunks.push_back(divmod_small(mag, 1000000000u));
    }
    std::string out = sign_ < 0 ? "-" : "";
    out += std::to_string(chunks.back());
    for (size_t i = chunks.size() - 1; i-- > 0;) {
        std::string part = std::to_string(chunks[i]);
        out += std::string(9 - part.size(), '0') + part;  // 中间块补齐前导零
    }
    return out;
}

} // namespace collie
