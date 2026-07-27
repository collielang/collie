# b01 · FizzBuzz

最经典的控制流练习：for + 取模 + if/else if 级联，附 switch 多值分支变体。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| for / if-else if / switch 多值 | control-flow.md | ✅ |
| `%` 取模（floor 语义） | 04-numeric.md | ✅（t7） |
| `@"{expr}"` 插值 | 03-character.md | ✅ |

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b01-fizzbuzz\main.collie
```

## 预期输出（实测）

```
1
2
Fizz
4
Buzz
Fizz
7
8
Fizz
Buzz
11
Fizz
13
14
FizzBuzz
16
17
Fizz
19
Buzz
7 不是 3 的倍数
```
