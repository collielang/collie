# a04 · 字符串

演示 `@"{}"` 插值、UTF-8 码点索引（负索引）、trim/subString 方法、`length` 属性、多行字符串。

## 状态：✅ 可运行（有 1 处已知行为偏差，见下）

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 插值 `@"{expr}"`、`\{` 转义 | data-type/.../03-character.md | ✅（t32） |
| UTF-8 码点索引 / 负索引 | 03-character.md | ✅（t24） |
| `length` 属性（码点数） | 03-character.md | ✅（t28） |
| trim/trimLeft/trimRight/subString | 03-character.md | ✅（t28） |
| 多行字符串 `"""`（dedent） | 03-character.md | 🟡 可解析，dedent 行为存疑（见下） |

## 陷阱备忘

- `word` 是位类型关键字（06-bitwise.md），**不能用作变量名**——本例最初命名 `word` 触发 `Expect variable name.`。

## ⚠ 已知偏差（排错线索）

多行字符串实测输出**保留了缩进且首部多一个空行**，与 `lexer_test.cpp` 的 `MultilineStrings` 期望（`"Hello,\nWorld!\n"`，缩进被剥除、无首空行）不一致。疑似解释器路径与词法测试路径的 dedent 行为不一致，待排查。

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a04-strings\main.collie
```

## 预期输出（实测，末段为多行字符串的当前实际行为）

```
Collie is 3 years old
1 + 2 = 3
literal braces: {not interpolated}
9
牧
犬
C
e
[hello world]
[hello world  ]
[  hello world]
hello
world
羊
Age: 3

    牧羊犬看守羊群，
    编译器看守类型。

```
