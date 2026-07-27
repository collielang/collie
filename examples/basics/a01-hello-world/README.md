# a01 · Hello World

演示 `print` 内建函数、单行/多行注释、字符串转义。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| `print` 内建函数 | grammer/basic-grammer.md | ✅（M4） |
| 单行 `//` / 多行 `/* */` 注释 | grammer/basic-grammer.md | ✅ |
| 字符串转义 `\n \t \" \\` | data-type/.../03-character.md | ✅ |
| UTF-8 源文件（中文字面量） | —（D3 决策） | ✅（M1） |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a01-hello-world\main.collie
```

## 预期输出（实测）

```
Hello, World!
你好，牧羊犬！
Line1
Line2
Tab	Separated
She said: "Collie!"
Backslash: \
```
