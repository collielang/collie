---
sidebar_label: Numeric Type
---

# Numeric Type

## 🐳Type Overview {/* #intro */}

## Integer & Decimal Types (Number Type)

|   Type    | Size<br />(bytes) | Description                        |
| :-------: | :---------------: | ---------------------------------- |
| `number`  |      variable     | Can represent an integer or decimal |
| `integer` |      variable     | High-precision integer             |
| `decimal` |      variable     | High-precision floating-point      |

### 🏅Syntax Examples {/* #syntax-example */}

- Define a number:

```collie
number num = 2025;
num.isInfinity() // result: false
num.isFinite() // result: true
num.isPositive() // result: true
num.isNegative() // result: false
num.isNaN() // result: false
num.isInteger() // result: true
num.isDecimal() // result: false
num.toString() // result: "2025"

number decimalNum = -.123456; // -0.123456
decimalNum.isInfinity() // result: false
decimalNum.isFinite() // result: true
decimalNum.isPositive() // result: false
decimalNum.isNegative() // result: true
num.isNaN() // result: false
num.isInteger() // result: false
num.isDecimal() // result: true
num.toString() // result: "-0.123456"

number anOtherDecimalNum = 2f; // Ending with f can also denote a floating-point number, equivalent to 2.0, 2.00, etc.

number positiveFinity = Infinity
positiveFinity.isInfinity() // result: true
positiveFinity.isFinite() // result: false
positiveFinity.isPositive() // result: true
positiveFinity.isNegative() // result: false
positiveFinity.isNaN() // result: false
positiveFinity.isInteger() // result: false
positiveFinity.isDecimal() // result: false
positiveFinity.toString() // result: "+Infinity"

number negativeFinity = -Infinity
negativeFinity.isInfinity() // result: true
negativeFinity.isFinite() // result: false
negativeFinity.isPositive() // result: false
negativeFinity.isNegative() // result: true
negativeFinity.isNaN() // result: false
negativeFinity.isInteger() // result: false
negativeFinity.isDecimal() // result: false
negativeFinity.toString() // result: "-Infinity"

number notANumber = NaN;
notANumber.isInfinity() // result: false
notANumber.isFinite() // result: false
notANumber.isPositive() // result: false
notANumber.isNegative() // result: false
notANumber.isNaN() // result: true
notANumber.isInteger() // result: false
notANumber.isDecimal() // result: false
negativeFinity.toString() // result: "NaN"
```

- Number comparison:

```collie
1 == 1.0 // result: true
```

- Convert a number to a string:

```collie
12.toString(10) // "1"
12.toString(10) // "1"
12.00.toString(10) // "12.0"
NaN.toString() // "NaN"

// TODO: keep a given number of decimal places, keep a given number of significant figures
// TODO: convert scientific notation to a string
```

- Convert a string to a number:

```collie
"Infinity".toNumber() // result: Infinity
"+Infinity".toNumber() // result: Infinity
"-Infinity".toNumber() // result: -Infinity

"infinity".toNumber() // result: NaN
```

- Scientific notation

```collie
// TODO
```

- Rounding:

```collie
// TODO
```

- Modulo:

```collie
-1 % 5 // result: 4
-1 % -5 // result: -1
1 % -5 // result: -4
```

:::warning[Note]

The result of the modulo of a negative number may differ across programming languages. For example, for `-1 mod 5`, the result is `-1` in Java and `4` in Python.
:::

- Integer division:

```collie
Math.integerDivision(-1, 5) // result: -1
-1.integerDivision(5) // result: 0.2

-1 / 5 // result: -0.2
Math.division(-1, 5) // result: -0.2
```

- Addition, subtraction, multiplication, division:

```collie
1.1 + 2 // result: 3.1

3 - 5.2 // -2.2

6 * 8 // result: 48

1 / 3 // result: 0.3333333333333333
2 / 3 // result: 0.6666666666666666
```

- Absolute value:

```collie
-1.abs() // result: 1
Math.abs(-1) // result: 1
```

- Get the integer part

```collie
123.456.integerPart() // 123
123.456.decimalPart() // 0.456

-123.456.integerPart() // -123
-123.456.decimalPart() // -0.456
```

{/*
## [@Deprecated] Integer Types

?> **Non-negative integers** and **non-positive integers** are distinguished by prefixing the type with `+` or `-`. For example: `+short`, `-short`.

|  Type   | Size<br />(bytes) |              Range               | Min value<br />object.MIN_VALUE                  | Max value<br />object.MAX_VALUE                | Description   |
| :-----: | :---------------: | :------------------------------: | -----------------------------------------------: | ---------------------------------------------: | ------------- |
| `short` |         2 byte         | [-2<sup>15</sup>, 2<sup>15</sup>-1] | -32,768<br />(-0x8000)                          | 32,767<br />(0x7FFF)                          | 16-bit integer |
| `+short` |         2 byte         | [0, 2<sup>16</sup>-1] | 0<br />(0x0000)                          | 65,535<br />(0xFFFF)                    | 16-bit non-negative integer |
| `-short` |         2 byte         | [-2<sup>16</sup>+1, 0] | -65,535<br />(-0xFFFF)         | -0<br />(-0x0000)              | 16-bit non-positive integer |
|  `int`  |         4 byte         | [-2<sup>31</sup>, 2<sup>31</sup>-1] | -2,147,483,648<br />(-0x80000000)               | 2,147,483,647<br />(0x7FFFFFFF)               | 32-bit integer |
|  `+int`  |         4 byte         | [0, 2<sup>32</sup>-1] | 0<br />(0x00000000)               | 4,294,967,295<br />(0xFFFFFFFF)  | 32-bit non-negative integer |
|  `-int`  |         4 byte         | [-2<sup>32</sup>+1, 0] | -4,294,967,295<br />(-0xFFFFFFFF) | -0<br />(-0x00000000) | 32-bit non-positive integer |
| `long`  |         8 byte         | [-2<sup>63</sup>, 2<sup>63</sup>-1] | -9,223,372,036,854,775,808<br />(-0x8000000000000000) | 9,223,372,036,854,775,807<br />(0x7FFFFFFFFFFFFFFF) | 64-bit integer |
| `+long`  |         8 byte         | [0, 2<sup>64</sup>-1] | 0<br />(0x0000000000000000) | 18,446,744,073,709,551,615<br />(0xFFFFFFFFFFFFFFFF) | 64-bit non-negative integer |
| `-long`  |         8 byte         | [-2<sup>64</sup>+1, 0] | -18,446,744,073,709,551,615<br />(-0xFFFFFFFFFFFFFFFF) | -0<br />(-0x0000000000000000) | 64-bit non-positive integer |
*/}

### Floating-point Types

|   Type   | Size<br />(bytes) | Range                                                        | Min value<br />object.MIN_VALUE    | Max value<br />object.MAX_VALUE                   | Description   |
| :------: | :---------------: | :----------------------------------------------------------: | ---------------------------------: | ------------------------------------------------: | ------------- |
| `float`  |         4 byte         | [2<sup>-149</sup>, (2-2<sup>-23</sup>)&middot;2<sup>127</sup>] | 1.4e-45<br />(0x1.0p-126)         | 3.4028235e+38<br />(0x1.FFFFFE0000000p+127)      | 32-bit floating-point |
| `+float`  |         4 byte         | to be confirmed | to be confirmed           | to be confirmed        | 32-bit non-negative floating-point |
| `-float`  |         4 byte         | to be confirmed | to be confirmed           | to be confirmed                                          | 32-bit non-positive floating-point |
| `double` |         8 byte         | [2<sup>-1074</sup>, (2-2<sup>-52</sup>)&middot;2<sup>1023</sup>] | 4.9e-324<br />(0x0.0000000000001p-1022) | 1.7976931348623157e+308<br />(0x1.FFFFFFFFFFFFFp+1023) | 64-bit floating-point |
| `+double` |         8 byte         | to be confirmed | to be confirmed                           | to be confirmed                                          | 64-bit non-negative floating-point |
| `-double` |         8 byte         | to be confirmed | to be confirmed                           | to be confirmed                                          | 64-bit non-positive floating-point |

### General Numeric Types

| Type     | Description                                                  |
| -------- | ------------------------------------------------------------ |
| `number` | Supports storing both integers and decimals. For scenarios where execution efficiency is not critical, use this type to reduce mental overhead |
| `integer` | High-precision integer (can represent integers of arbitrary magnitude exactly) |
| `decimal` | High-precision floating-point (can represent decimals of arbitrary length exactly). Floating-point operations output a 17-digit-long result, but only 15 digits are accurate |

### References

#### IEEE 754 Standard

- [754-2019 - IEEE Standard for Floating-Point Arithmetic](https://ieeexplore.ieee.org/document/8766229)
- [Lecture Notes on the Status of **IEEE Standard 754 for Binary Floating-Point Arithmetic**](https://people.eecs.berkeley.edu/~wkahan/ieee754status/IEEE754.PDF)
