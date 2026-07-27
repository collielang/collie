# b05 · 数字工具集

素数判定（试除法）、辗转相除 GCD、数位和、幂运算、手写四舍五入 `roundTo`、number 内建方法组合。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| `.abs()/.integerPart()/.decimalPart()/.isInteger()` | 04-numeric.md | ✅（t29/t30） |
| `%` floor 取模参与数位分解 | 04-numeric.md | ✅（t7） |
| `toString()` / `.trim()` | 内建 / 03-character.md | ✅ |

## 陷阱备忘

- `base` 是保留关键字（t38），函数参数改名 `base_`。
- 无整除运算符：`(n - n % 10) / 10` 模拟 `n // 10`。
- `roundTo(2.675, 2)` 实测得 `2.68`——本例中二进制浮点误差未触发半分位陷阱，但不同实现/量级下可能得 `2.67`，作浮点敏感性观察点。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b05-number-utils\main.collie
```

## 预期输出（实测）

```
true
true
false
false
2 3 5 7 11 13 17 19 23 29
6
6
1
5
15
27
0
1024
1
3.14
2.68
1
true
false
3
3.14
```
