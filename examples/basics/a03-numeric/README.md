# a03 · 数值类型

演示数值字面量全形式（前导点/`f` 后缀/科学计数法）、floor 取模、IEEE 754 特殊值（Infinity/NaN/除零）、number 内建方法。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 字面量 `.5` / `2f` / `1.5e3` | data-type/.../04-numeric.md | ✅（t26） |
| floor 取模（符号随除数） | 04-numeric.md | ✅（t26） |
| IEEE 754 除零 / Infinity / NaN | 04-numeric.md | ✅（t31/t33） |
| number 方法 abs/integerPart/decimalPart/is* | 04-numeric.md | ✅（t27） |
| toString / toNumber | 04-numeric.md | ✅（t23/t27） |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a03-numeric\main.collie
```

## 预期输出（实测）

```
123
3.14
0.5
2
1500
4
-1
-4
1
+Infinity
-Infinity
NaN
NaN
+Infinity
false
123.456
-123
-0.456
true
true
true
true
13
255!
```
