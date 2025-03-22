---
title: Unicode Encoding Standards
hide_table_of_contents: true
---

# Introduction to Unicode Encoding Standards

UTF-8, UTF-16, and UTF-32 are different implementations of **Unicode encoding**, each with its own design philosophy and suited for different storage and transmission scenarios. Let’s examine their encoding designs at the bit level.

### 1. UTF-8 Encoding {#utf-8}

UTF-8 is a variable-length encoding that supports characters ranging from **U+0000** to **U+10FFFF**. It uses **1 to 4 bytes** to encode a character based on its Unicode code point (i.e., the character’s numerical value). The design philosophy of UTF-8 is to use the **high bits of a byte** to indicate how many bytes are needed to represent the current character.

#### Design Rules
- UTF-8 uses **1 to 4 bytes**, and the high bits of each byte are used as markers to indicate whether the character requires additional bytes.
- For each byte, the first few bits represent the character’s information, while the remaining bits are used for continuation markers.

The specific encoding rules are as follows:

- **1 byte (7-bit characters)**:
  - For characters in the range U+0000 to U+007F (i.e., basic ASCII characters), UTF-8 uses **1 byte**.
  - Structure: `0xxxxxxx` (7 valid bits)
  - Example: The character `A` (U+0041) is encoded in UTF-8 as `0x41` (i.e., `01000001`)

- **2 bytes (11-bit characters)**:
  - For characters in the range U+0080 to U+07FF, UTF-8 uses **2 bytes**.
  - Structure: `110xxxxx 10xxxxxx` (the leading byte marker is `110`, and the continuation byte marker is `10`)
  - Example: The character `é` (U+00E9) is encoded in UTF-8 as `0xC3 0xA9` (i.e., `11000011 10101001`)

- **3 bytes (16-bit characters)**:
  - For characters in the range U+0800 to U+FFFF, UTF-8 uses **3 bytes**.
  - Structure: `1110xxxx 10xxxxxx 10xxxxxx` (the leading byte marker is `1110`, and the continuation byte marker is `10`)
  - Example: The character `中` (U+4E2D) is encoded in UTF-8 as `0xE4 0xB8 0xAD` (i.e., `11100100 10111000 10101101`)

- **4 bytes (21-bit characters)**:
  - For characters in the range U+10000 to U+10FFFF, UTF-8 uses **4 bytes**.
  - Structure: `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx` (the leading byte marker is `11110`, and the continuation byte marker is `10`)
  - Example: The character `𐍈` (U+10348) is encoded in UTF-8 as `0xF0 0x90 0x8D 0x88` (i.e., `11110000 10010000 10001101 10001000`)

#### Summary
- **UTF-8** uses **1 to 4 bytes** to encode characters, with the high bits of each byte indicating whether it is a continuation byte.
- Byte structure:
  - 1 byte: `0xxxxxxx`
  - 2 bytes: `110xxxxx 10xxxxxx`
  - 3 bytes: `1110xxxx 10xxxxxx 10xxxxxx`
  - 4 bytes: `11110xxx 10xxxxxx 10xxxxxx 10xxxxxx`

---

### 2. UTF-16 Encoding {#utf-16}

UTF-16 is a variable-length encoding that uses **2 or 4 bytes** to represent characters. It uses **16 bits** to represent characters and supports characters ranging from **U+0000** to **U+10FFFF**. For characters outside the BMP (Basic Multilingual Plane, U+0000 to U+FFFF), UTF-16 uses the **surrogate pairs** mechanism.

#### Design Rules
- **U+0000 to U+FFFF**: Uses **1 16-bit unit (2 bytes)**.
  - Structure: `00000000 00000000` (16 bits)
  - Example: The character `A` (U+0041) is encoded in UTF-16 as `0x0041` (i.e., `00000000 01000001`)

- **U+10000 to U+10FFFF**: These characters exceed 16 bits and require **2 16-bit units (4 bytes)**, known as **surrogate pairs**.
  - Calculation of surrogate pairs:
    - Subtract `0x10000` from the character’s code point (resulting in a 20-bit number).
    - Split it into two parts:
      - High surrogate: `0xD800 | (high 10 bits)`
      - Low surrogate: `0xDC00 | (low 10 bits)`
  - Example: The character `𐍈` (U+10348):
    - Subtract `0x10000`: `U+10348 - 0x10000 = 0x0348` (20 bits)
    - High surrogate: `0xD800 | (0x0348 >> 10) = 0xD800 | 0x0003 = 0xD803`
    - Low surrogate: `0xDC00 | (0x0348 & 0x3FF) = 0xDC00 | 0x348 = 0xDC48`
    - UTF-16 encoding: `0xD803 0xDC48` (i.e., `11010111 10000011 11011100 01001000`)

#### Summary
- **UTF-16** uses **2 bytes** to represent BMP characters and **surrogate pairs** (two 16-bit units, totaling 4 bytes) for characters outside the BMP.
- Character ranges:
  - 1 unit (2 bytes): `00000000 00000000`
  - 2 units (4 bytes, surrogate pairs): `110110xxxx xxxx 110111xxxx xxxx`

---

### 3. UTF-32 Encoding {#utf-32}

UTF-32 is a fixed-length encoding that uses **4 bytes** (i.e., 32 bits) to represent each character. It can directly represent all Unicode characters without the need for variable lengths or surrogate pairs.

#### Design Rules
- **U+0000 to U+10FFFF**: Each character is represented by **4 bytes**.
  - Structure: `00000000 00000000 00000000 xxxxxxxx` (32 bits)
  - Example: The character `A` (U+0041) is encoded in UTF-32 as `0x00000041` (i.e., `00000000 00000000 00000000 01000001`)
  - Example: The character `中` (U+4E2D) is encoded in UTF-32 as `0x00004E2D` (i.e., `00000000 00000000 01001110 00101101`)

#### Summary
- **UTF-32** uses **4 bytes** (32 bits) to represent each character.
- Character range: `00000000 00000000 00000000 xxxxxxxx` (32 bits)

---

### Summary

1. **UTF-8**: Uses 1 to 4 bytes to encode characters based on their Unicode code point, with the high bits of each byte indicating whether it is a continuation byte.
2. **UTF-16**: Uses 2 bytes to represent BMP characters and surrogate pairs (4 bytes) for characters outside the BMP.
3. **UTF-32**: Uses 4 bytes to represent each character, with no need for variable lengths or surrogate pairs.

Each encoding has its use cases: UTF-8 is commonly used for storage and data transmission, UTF-16 is often used for internal processing (e.g., in Java and Windows), and UTF-32 is used when direct support for all characters is required, though it consumes more memory.
