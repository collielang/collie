---
sidebar_label: 数字类型（Numeric Type）
---

# 数字类型（Numeric Type）

## 🐳类型简介 {/* #intro */}

## 整数 & 小数类型（Number Type）

|   类型    | 占用空间<br />（字节） | 描述             |
| :-------: | :--------------------: | ---------------- |
| `number`  |          可变          | 可表示整数或小数 |
| `integer` |          可变          | 高精度整数       |
| `decimal` |          可变          | 高精度浮点数     |

### 🏅语法示例 {/* #syntax-example */}

- 定义一个数字：

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

number anOtherDecimalNum = 2f; // 使用 f 结尾也可以表示浮点数，与 2.0, 2.00 等都等价

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

- 数字比较：

```collie
1 == 1.0 // result: true
```

- 数字转换为字符串：

```collie
12.toString(10) // "1"
12.toString(10) // "1"
12.00.toString(10) // "12.0"
NaN.toString() // "NaN"

// TODO 保留小数位数，保留几位有效数字
// TODO 科学计数法转字符串
```

- 字符串转数字：

```collie
"Infinity".toNumber() // result: Infinity
"+Infinity".toNumber() // result: Infinity
"-Infinity".toNumber() // result: -Infinity

"infinity".toNumber() // result: NaN
```

- 科学计数法

```collie
// TODO
```

- 四舍五入：

```collie
// TODO
```

- 取模：

```collie
-1 % 5 // result: 4
-1 % -5 // result: -1
1 % -5 // result: -4
```

:::warning[注意]

负数取模问题在不同编程语言中结果可能不同，例如 `-1 mod 5`，Java 语言中结果为 `-1`，Python 语言中结果为 `4`
:::

- 整除：

```collie
Math.integerDivision(-1, 5) // result: -1
-1.integerDivision(5) // result: 0.2

-1 / 5 // result: -0.2
Math.division(-1, 5) // result: -0.2
```

- 加减乘除：

```collie
1.1 + 2 // result: 3.1

3 - 5.2 // -2.2

6 * 8 // result: 48

1 / 3 // result: 0.3333333333333333
2 / 3 // result: 0.6666666666666666
```

- 取绝对值：

```collie
-1.abs() // result: 1
Math.abs(-1) // result: 1
```

- 取整数部分

```collie
123.456.integerPart() // 123
123.456.decimalPart() // 0.456

-123.456.integerPart() // -123
-123.456.decimalPart() // -0.456
```

{/*
## [@Deprecated] 整数类型

?> **非负整数**和**非正整数**在类型前添加 `+`, `-` 区分。例如：`+short`, `-short`。

|  类型   | 占用空间<br />（字节） |              取值范围               | 最小值<br />object.MIN_VALUE                     | 最大值<br />object.MAX_VALUE                   | 描述     |
| :-----: | :--------------------: | :---------------------------------: | -----------------------------------------------: | ---------------------------------------------: | -------- |
| `short` |         2 byte         | [-2<sup>15</sup>, 2<sup>15</sup>-1] | -32,768<br />(-0x8000)                          | 32,767<br />(0x7FFF)                          | 16位整数 |
| `+short` |         2 byte         | [0, 2<sup>16</sup>-1] | 0<br />(0x0000)                          | 65,535<br />(0xFFFF)                    | 16位非负整数 |
| `-short` |         2 byte         | [-2<sup>16</sup>+1, 0] | -65,535<br />(-0xFFFF)         | -0<br />(-0x0000)              | 16位非正整数 |
|  `int`  |         4 byte         | [-2<sup>31</sup>, 2<sup>31</sup>-1] | -2,147,483,648<br />(-0x80000000)               | 2,147,483,647<br />(0x7FFFFFFF)               | 32位整数 |
|  `+int`  |         4 byte         | [0, 2<sup>32</sup>-1] | 0<br />(0x00000000)               | 4,294,967,295<br />(0xFFFFFFFF)  | 32位非负整数 |
|  `-int`  |         4 byte         | [-2<sup>32</sup>+1, 0] | -4,294,967,295<br />(-0xFFFFFFFF) | -0<br />(-0x00000000) | 32位非正整数 |
| `long`  |         8 byte         | [-2<sup>63</sup>, 2<sup>63</sup>-1] | -9,223,372,036,854,775,808<br />(-0x8000000000000000) | 9,223,372,036,854,775,807<br />(0x7FFFFFFFFFFFFFFF) | 64位整数 |
| `+long`  |         8 byte         | [0, 2<sup>64</sup>-1] | 0<br />(0x0000000000000000) | 18,446,744,073,709,551,615<br />(0xFFFFFFFFFFFFFFFF) | 64位非负整数 |
| `-long`  |         8 byte         | [-2<sup>64</sup>+1, 0] | -18,446,744,073,709,551,615<br />(-0xFFFFFFFFFFFFFFFF) | -0<br />(-0x0000000000000000) | 64位非正整数 |
*/}

### 浮动小数类型

|   类型   | 占用空间<br />（字节） | 取值范围                                                     | 最小值<br />object.MIN_VALUE       | 最大值<br />object.MAX_VALUE                      | 描述         |
| :------: | :--------------------: | :----------------------------------------------------------: | ---------------------------------: | ------------------------------------------------: | ------------ |
| `float`  |         4 byte         | [2<sup>-149</sup>, (2-2<sup>-23</sup>)&middot;2<sup>127</sup>] | 1.4e-45<br />(0x1.0p-126)         | 3.4028235e+38<br />(0x1.FFFFFE0000000p+127)      | 32位浮动小数 |
| `+float`  |         4 byte         | 需要确认 | 需要确认           | 需要确认        | 32位非负浮动小数 |
| `-float`  |         4 byte         | 需要确认 | 需要确认           | 需要确认                                          | 32位非正浮动小数 |
| `double` |         8 byte         | [2<sup>-1074</sup>, (2-2<sup>-52</sup>)&middot;2<sup>1023</sup>] | 4.9e-324<br />(0x0.0000000000001p-1022) | 1.7976931348623157e+308<br />(0x1.FFFFFFFFFFFFFp+1023) | 64位浮动小数 |
| `+double` |         8 byte         | 需要确认 | 需要确认                           | 需要确认                                          | 64位非负浮动小数 |
| `-double` |         8 byte         | 需要确认 | 需要确认                           | 需要确认                                          | 64位非正浮动小数 |

### 通用数字类型

| 类型     | 描述                                                         |
| -------- | ------------------------------------------------------------ |
| `number` | 支持保存整数和小数，对于执行效率要求不高的场景，可使用该类型减少心智负担 |
| `integer` | 高精度整数（能够精确表示任意大小的整数） |
| `decimal` | 高精度浮点数（能够精确表示任意位小数），浮点数运算输出17位长度的结果，但只有15个数字是准确的 |

### 参考文献

#### IEEE 754 标准

- [754-2019 - IEEE Standard for Floating-Point Arithmetic](https://ieeexplore.ieee.org/document/8766229)
- [Lecture Notes on the Status of **IEEE Standard 754  for  Binary Floating-Point Arithmetic**](https://people.eecs.berkeley.edu/~wkahan/ieee754status/IEEE754.PDF)
