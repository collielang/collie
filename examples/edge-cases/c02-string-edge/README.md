# c02 · 字符串边界

空串、Unicode 多语言码点、emoji（4 字节 UTF-8）、索引边界、转义全家桶、插值边界、字典序比较、长拼接链。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| UTF-8 码点级 `.length` / 索引 / 负索引 | 03-character.md | ✅（t25/t26） |
| 转义 `\t \n \" \\` | 03-character.md | ✅ |
| `@"{expr}"` 插值（含算式） | 03-character.md | ✅（t27） |
| 字符串字典序比较 `< >` | 03-character.md | ✅ |

## 实测亮点

- **emoji 按码点正确计数**：`"🐕".length == 1`、`"a🐕b"[1] == "🐕"`——4 字节 UTF-8 处理无误。
- 连续插值 `@"{x}{x}{x}"`、插值内表达式 `@"{x + x * 2}"` 均正常。

## ⚠ 发现（排错线索）

- **插值内不能嵌套插值字符串**：`@"{@"inner"}"` 报 `Unmatched '{' in interpolated string`（词法器不支持插值递归）。

## 运行

```bat
compiler\build\Release\collie.exe examples\edge-cases\c02-string-edge\main.collie
```

## 预期输出（实测）

```
0
true
x

3
12
牧
犬
1
3
🐕
a
c
a
Tab:	End
Newline:
(second line)
Quote:" Backslash:\
777
21
literal only
true
true
true
true
1234567890
10
```

第 4 行为空行（`"".trim()`）。
