# b10 · JSON 构造器

无标准库 json 模块时的实用写法：手写转义（`\` `"` 换行）、number 数组序列化、类封装 builder 风格对象构造、嵌套对象。

## 状态：✅ 可运行（绕过一处语义限制后）

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 字符串转义 `\\` `\"` `\n` | 03-character.md | ✅ |
| 字符串码点遍历 + 拼接 | 03-character.md | ✅ |
| class 私有字段累积状态 / 私有方法 | uncategorized.md | ✅（t34） |
| `toString(number/bool)` | 内建 | ✅ |

对照：标准库规范 `standard-library/05-serialization/01-json.md` 定义了目标 `Json.stringify` 等 API（⛔ 标准库未实现）——落地后本例可对照重写。

## ⚠ 语义分析限制（本例实测发现）

- **`this.body != ""`（字段与字面量比较）被拒**：`Incomparable operand types`——与 b03 的数组下标、b07 的方法调用同源：非局部变量表达式的类型未参与比较推导。需先 `string current = this.body;` 再比较。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b10-json-builder\main.collie
```

## 预期输出（实测）

```
"hello"
"say \"hi\" and \\ end"
[1,2.5,-3]
[]
{"name":"牧羊犬","age":3,"trained":true,"scores":[98,95,100]}
{"owner":"Coz","address":{"city":"Shanghai","street":"Collie Road"},"tags":["dog","smart"]}
{}
```
