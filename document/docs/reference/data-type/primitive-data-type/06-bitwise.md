---
sidebar_label: Bitwise Type
---

# Bitwise Type

## 🐳Type Overview {/* #intro */}

| Type   | Size | Range | Min value<br/>object.MIN_VALUE | Max value<br />object.MAX_VALUE | Description           |
| :----: | :-------------------: | :-------------------: | --------------------: | --------------------: | --------------------- |
| `bit`  | 1 bit | [0, 2<sup>1</sup>-1] | 0<br />(0b0) | 1<br />（0b1) | A bit   |
| `byte` | 1 byte (8 bit) | [0, 2<sup>8</sup>-1] | 0<br />(0b0000 0000)<br />（0x00) | 255<br />(0b1111 1111)<br />(0xFF) | A byte. 8-bit unsigned integer |
| `word` | 2 byte (16 bit) | [0, 2<sup>16</sup>-1] | 0<br />(0x00 00) | 65,535<br />(0xFF FF) | A word. 16-bit unsigned integer |
| `dword` | 4 byte (32 bit) | [0, 2<sup>32</sup>-1] | 0<br />(0x00 00 00 00) | 4,294,967,295<br />(0xFF FF FF FF) | A double word. 32-bit unsigned integer |

:::info

Collie uses **big-endian** byte order by default. In this storage scheme, the **high-order byte of the data is stored at the lower address, and the low-order byte at the higher address**.

Take the 16-bit word `0x1234` as an example: under big-endian storage, the high-order byte `0x12` is stored at the lower memory address, and the low-order byte `0x34` at the higher memory address.

It is as if data is stored in left-to-right order (high-order first), which matches the way humans naturally think — seeing the high-order part first.

From a data perspective, the 32-bit integer `0x12345678` has, in big-endian order, the byte sequence `0x12` `0x34` `0x56` `0x78` (most-significant byte → least-significant byte).

:::

### Bitwise Operators

- Bitwise AND: `&`
- Bitwise OR: `|`
- Bitwise XOR: `^`
- Bitwise NOT: `~`
- Left shift: `<<`
- Right shift: `>>`

### 🏅Bitwise Operation Examples {/* #syntax-example */}

- Bitwise operations

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

- Basic operations on a bit

```collie
// Using literals
bit bit1 = 0b0; // or simply write 0
bit bit2 = 0b1; // or simply write 1
```

- Basic operations on a byte

```collie
// Using a literal
byte byteVal = 0b10101010;                   // result: 0b10101010

// 8 bits form a byte
bit[] bits = [1, 0, 1, 0, 1, 0, 1, 0];
byte byteVal1 = byte(bits);                    // result: 0b10101010
byte byteVal2 = byte(1, 0, 1, 0, 1, 0, 1, 0); // result: 0b10101010

// Extract a bit from a byte
bit firstBit = byteVal.getHighBit(); // or byteVal.getBitByIndex(0);  // get the highest bit, result: 0b1
bit lastBit = byteVal.getLowBit();   // or byteVal.getBitByIndex(-1); // get the lowest bit, result: 0b0
bit[] allBits = byteVal.toBits();  // convert to a bit array, result: [0b1, 0b0, 0b1, 0b0, 0b1, 0b0, 0b1, 0b0]

// Convert a bit[] to a byte
byte byteFormBits = allBits.toBytes(); // result: 0b10101010
// ⚠ Note: the number of elements must be correct
```

- Basic operations on a word

```collie
// Using a literal
word wordVal = 0xFF00;          // result: 0xFF00

// 2 bytes form a word
byte high, low = 0xFF, 0x00;
word wordVal1 = word(high, low);                                      // result: 0xFF00

// 16 bits form a word
byte byteVal2 = word(1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0); // result: 0xFF00

// Extract a byte from a word
byte highByte = wordVal.getHighByte();  // get the high byte, result: 0xFF
byte lowByte = wordVal.getLowByte();    // get the low byte, result: 0x00
byte[] bytes = wordVal.toBytes();  // convert to a byte array

// Extract a bit from a word
bit firstBit = byteVal[0];   // get the highest bit
bit lastBit = byteVal[15];    // get the lowest bit
bit[] allBits = wordVal.toBits();  // convert to a bit array
```

- Basic operations on a double word

```collie
// 2 words form a dword
word w1 = 0xFF00
word w0 = 0x00FF
dword dwordVal1 = dword(w1, w0);  // result: 0xFF00FF00
// 4 bytes form a dword
byte b3 = 0xFF;
byte b2 = 0x00;
byte b1 = 0xFF;
byte b0 = 0x00;
dword dwordVal2 = dword(b3, b2, b1, b0);  // result: 0xFF00FF00
// Using a literal
dword dwordVal = 0xFF00FF00;

// Extract a word from a dword
byte highByte = dwordVal.high;  // get the high byte
byte lowByte = dwordVal.low;    // get the low byte
word[] words = dwordVal.toWords();  // convert to a word array

// Extract a byte from a dword
byte[] allBits = dwordVal.toBytes();  // convert to a byte array

// Extract a bit from a dword
byte firstByte = dwordVal[0];    // get the highest bit
byte lastByte = dwordVal[31];    // get the lowest bit
bit[] allBits = dwordVal.toBits();  // convert to a bit array
```

- Type conversion

```collie
byte byteValue = 255;
word wordValue = word(byteValue);      // implicit conversion: 0x00FF
dword dwordValue = dword(wordValue);   // implicit conversion: 0x0000FF00
```

- Bit extraction

```collie
bit getBit(byte value, int position) {  // position: 0-7
    return (value >> position) & 1;
}
```

- Bit setting

```collie
byte setBit(byte value, int position, bit newBit) {  // position: 0-7
    if (newBit == 1) {
        return value | (1 << position);
    } else {
        return value & ~(1 << position);
    }
}
```
