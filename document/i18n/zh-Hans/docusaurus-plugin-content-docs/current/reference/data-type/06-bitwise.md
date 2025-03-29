---
sidebar_position: 6
sidebar_label: 位类型（Bitwise Type）
---

# 位类型（Bitwise Type）

:::info

位类型为[不可变类型](./#immutable-data-type)。

:::

## 🐳类型简介 {#intro}

| 类型   | 占用空间 | 取值范围 | 最小值<br/>object.MIN_VALUE | 最大值<br />object.MAX_VALUE | 描述                  |
| :----: | :-------------------: | :-------------------: | --------------------: | --------------------: | --------------------- |
| `bit`  | 1 bit | [0, 2<sup>1</sup>-1] | 0<br />(0b0) | 1<br />（0b1) | 位   |
| `byte` | 1 byte (8 bit) | [0, 2<sup>8</sup>-1] | 0<br />(0b0000 0000)<br />（0x00) | 255<br />(0b1111 1111)<br />(0xFF) | 字节。8位无符号整数 |
| `word` | 2 byte (16 bit) | [0, 2<sup>16</sup>-1] | 0<br />(0x00 00) | 65,535<br />(0xFF FF) | 单字。16位无符号整数 |
| `dword` | 4 byte (32 bit) | [0, 2<sup>32</sup>-1] | 0<br />(0x00 00 00 00) | 4,294,967,295<br />(0xFF FF FF FF) | 双字。32位无符号整数 |

:::info

collie 语言默认采用**大端序（Big-Endian）**。在这种存储方式中，数据的**高位字节存于低地址，低位字节存于高地址**。

以 16 位 word `0x1234` 为例，在大端序存储时，高位字节 `0x12` 存于内存的低地址，低位字节 `0x34` 存于内存的高地址。

就好像按照从左到右（高位在前）的顺序存储数据，这符合人类正常的思维习惯，先看到高位部分。

从数据角度看，32 位整数 `0x12345678` 在大端序下，字节序列为 `0x12` `0x34` `0x56` `0x78` (最高位字节 → 最低位字节)。

:::

### 位操作符

- 按位与：`&`
- 按位或：`|`
- 按位异或：`^`
- 按位取反：`~`
- 左移：`<<`
- 右移：`>>`

### 🏅位操作示例 {#syntax-example}

- 位运算

```collie
byte a = 0b10101010;
byte b = 0b11001100;

byte c = a & b;  // result: 0b10001000
byte d = a | b;  // result: 0b11101110
byte e = a ^ b;  // result: 0b01100110
byte f = ~a;     // result: 0b01010101

byte g = a << 2; // result: 0b10101000
byte h = a >> 2; // result: 0b00101010
```

- 位(bit)基础操作

```collie
// 使用字面量
bit bit1 = 0b0; // 或者直接写作 0
bit bit2 = 0b1; // 或者直接写作 1
```

- 字节(byte)基础操作

```collie
// 使用字面量
byte byteVal = 0b10101010;                   // result: 0b10101010

// 8 个 bit 组成 byte
bit[] bits = [1, 0, 1, 0, 1, 0, 1, 0];
byte byteVal1 = byte(bits);                    // result: 0b10101010
byte byteVal2 = byte(1, 0, 1, 0, 1, 0, 1, 0); // result: 0b10101010

// 从 byte 中提取 bit
bit firstBit = byteVal.getHighBit(); // 或 byteVal.getBitByIndex(0);  // 获取最高位 result: 0b1
bit lastBit = byteVal.getLowBit();   // 或 byteVal.getBitByIndex(-1); // 获取最低位 result: 0b0
bit[] allBits = byteVal.toBits();  // 转换为 bit 数组 result: [0b1, 0b0, 0b1, 0b0, 0b1, 0b0, 0b1, 0b0]

// 将 bit[] 转为 byte
byte byteFormBits = allBits.toBytes(); // result: 0b10101010
// ⚠ 注意元素个数需要正确
```

- 单字(word)基础操作

```collie
// 使用字面量
word wordVal = 0xFF00;          // result: 0xFF00

// 2 个 byte 组成 word
byte high, low = 0xFF, 0x00;
word wordVal1 = word(high, low);                                      // result: 0xFF00

// 16 个 bit 组成 word
byte byteVal2 = word(1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0); // result: 0xFF00

// 从 word 中提取 byte
byte highByte = wordVal.getHighByte();  // 获取高字节 result: 0xFF
byte lowByte = wordVal.getLowByte();    // 获取低字节 result: 0x00
byte[] bytes = wordVal.toBytes();  // 转换为 byte 数组

// 从 word 中提取 bit
bit firstBit = byteVal[0];   // 获取最高位
bit lastBit = byteVal[15];    // 获取最低位
bit[] allBits = wordVal.toBits();  // 转换为 bit 数组
```

- 双字(dword)基础操作

```collie
// 2 个 word 组成 dword
word w1 = 0xFF00
word w0 = 0x00FF
dword dwordVal1 = dword(w1, w0);  // result: 0xFF00FF00
// 4 个 byte 组成 dword
byte b3 = 0xFF;
byte b2 = 0x00;
byte b1 = 0xFF;
byte b0 = 0x00;
dword dwordVal2 = dword(b3, b2, b1, b0);  // result: 0xFF00FF00
// 使用字面量
dword dwordVal = 0xFF00FF00;

// 从 dword 中提取 word
byte highByte = dwordVal.high;  // 获取高字节
byte lowByte = dwordVal.low;    // 获取低字节
word[] words = dwordVal.toWords();  // 转换为 word 数组

// 从 dword 中提取 byte
byte[] allBits = dwordVal.toBytes();  // 转换为 byte 数组

// 从 dword 中提取 bit
byte firstByte = dwordVal[0];    // 获取最高位
byte lastByte = dwordVal[31];    // 获取最低位
bit[] allBits = dwordVal.toBits();  // 转换为 bit 数组
```

- 类型转换

```collie
byte byteValue = 255;
word wordValue = word(byteValue);      // 隐式转换：0x00FF
dword dwordValue = dword(wordValue);   // 隐式转换：0x0000FF00
```

- 位提取

```collie
bit getBit(byte value, int position) {  // position: 0-7
    return (value >> position) & 1;
}
```

- 位设置

```collie
byte setBit(byte value, int position, bit newBit) {  // position: 0-7
    if (newBit == 1) {
        return value | (1 << position);
    } else {
        return value & ~(1 << position);
    }
}
```
