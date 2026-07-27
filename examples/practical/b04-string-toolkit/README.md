# b04 · 字符串工具箱

反转、回文判定、字符统计（UTF-8 中文码点验证）、trim/subString 链式组合、重复拼接。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 字符串码点索引 `s[i]` | 03-character.md | ✅（t26，UTF-8 码点级） |
| `.length` / `.trim()` / `.subString()` 链式 | 03-character.md | ✅（t25/t28） |
| 字符串拼接累积 | 03-character.md | ✅ |

## 语义观察

- `s[i] == target`、`s[i] != s[j]`（字符串索引相等比较）**通过**语义检查——与 b03 中数组元素 `arr[mid] == target` 被拒形成对照，说明字符串索引的静态类型推导比数组下标更完整。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b04-string-toolkit\main.collie
```

## 预期输出（实测）

```
olleh
犬羊牧

true
false
true
true
true
4
2
0
[Collie Lang]
Collie
ababab
==========
```

第 3 行为空行（`reverse("")` 输出空串）。
