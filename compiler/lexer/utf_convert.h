/*
 * @Author: Zhang Bokai <codingzhang@126.com>
 * @Description: 平台无关的 UTF-8 <-> UTF-16 转换辅助函数
 *
 * 取代已弃用的 std::codecvt / std::wstring_convert，也不再依赖
 * Windows 专属的 MultiByteToWideChar / WideCharToMultiByte，
 * 使 gcc/clang/MSVC 行为一致。遇到非法序列抛 std::runtime_error。
 */
#ifndef COLLIE_UTF_CONVERT_H
#define COLLIE_UTF_CONVERT_H

#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdint>

namespace collie {

// UTF-8 字节序列 -> UTF-16 (char16_t)
inline std::u16string utf8_to_utf16(std::string_view utf8) {
    std::u16string out;
    out.reserve(utf8.size());
    const size_t n = utf8.size();
    size_t i = 0;
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        uint32_t cp;
        size_t extra;
        if (c < 0x80) {
            cp = c;
            extra = 0;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0Fu;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            throw std::runtime_error("Invalid UTF-8 lead byte");
        }

        if (i + extra >= n) {
            throw std::runtime_error("Truncated UTF-8 sequence");
        }
        for (size_t k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
            if ((cc & 0xC0u) != 0x80u) {
                throw std::runtime_error("Invalid UTF-8 continuation byte");
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        i += extra + 1;

        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            throw std::runtime_error("Invalid Unicode code point");
        }

        if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000u;
            out.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
        }
    }
    return out;
}

// UTF-16 (char16_t) -> UTF-8 字节序列
inline std::string utf16_to_utf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size() * 3);
    const size_t n = utf16.size();
    size_t i = 0;
    while (i < n) {
        uint32_t cp = static_cast<uint16_t>(utf16[i]);
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            // 高代理项，需要紧跟一个低代理项
            if (i + 1 >= n) {
                throw std::runtime_error("Truncated UTF-16 surrogate pair");
            }
            uint32_t lo = static_cast<uint16_t>(utf16[i + 1]);
            if (lo < 0xDC00u || lo > 0xDFFFu) {
                throw std::runtime_error("Invalid UTF-16 low surrogate");
            }
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            i += 2;
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            throw std::runtime_error("Unexpected UTF-16 low surrogate");
        } else {
            i += 1;
        }

        if (cp < 0x80u) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
}

} // namespace collie

#endif // COLLIE_UTF_CONVERT_H
