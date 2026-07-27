# c01 · 数值极限与浮点边界

IEEE 754 行为系统验证：除零家族、Infinity 运算、NaN 传播与自反性、精度陷阱、2^53 整数上界、上溢/下溢、负零、floor 取模负数行为、科学计数法。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 除零 → ±Infinity / NaN | 04-numeric.md + 作者决策 | ✅（t8） |
| `.abs()` / 容差比较 | 04-numeric.md | ✅ |
| `.5` / `2e3` / `1.5e-2` 字面量 | 04-numeric.md | ✅（t10） |

## ⚠ 数字显示格式发现（重要排错线索）

实测发现 number 的 print 输出使用 **约 6 位有效数字** 的默认格式，带来两类误导：

1. **`0.1 + 0.2` 显示为 `0.3`**，但 `0.1 + 0.2 == 0.3` 是 `false`——显示精度掩盖了浮点误差，调试时极易误判"值明明相等"。
2. **大整数丢显示精度**：`9007199254740992`（2^53）显示为 `9.0072e+15`，无法从输出区分 `2^53` 与 `2^53 + 1`。

建议后续将 toString/print 的 number 格式改为最短往返表示（shortest round-trip，如 C++17 `std::to_chars`）。

另外：正无穷输出为 `+Infinity`（带 + 号），与文档示例 `Infinity` 略有出入。

## 运行

```bat
compiler\build\Release\collie.exe examples\edge-cases\c01-numeric-limits\main.collie
```

## 预期输出（实测）

```
+Infinity
-Infinity
NaN
+Infinity
NaN
NaN
0
-Infinity
NaN
false
true
0.3
false
true
9.0072e+15
9.0072e+15
1e+308
+Infinity
4.94066e-324
0
true
-Infinity
2
-2
-1
1
2000
0.015
```

要点：NaN 不等于自身（第 10~11 行）；`-7 % 3 == 2` 是 floor 取模语义（与 C/Java 的 `-1` 不同）；`1 / -0` 得 `-Infinity` 证明内部保留负零。
