---
sidebar_label: 元组类型（Tuple Type）（TODO）
---

# 元组类型（Tuple Type）

> 可以参考 C# 元组：https://learn.microsoft.com/zh-cn/dotnet/csharp/language-reference/builtin-types/value-tuples

## 🐳类型简介 {/* #intro */}

|         类型          | 描述                     |
| :-------------------: | ------------------------ |

## 🏅语法示例 {/* #syntax-example */}

```collie
Tuple a = (name: "Alice", age: 18);
```

## 🔑成员访问 {/* #member-access */}

经作者确认（不采用 Rust 风格 `.0`，避免与前导点小数 `.5` 的词法冲突；也不采用 C# 风格 `Item1`）：

- **按索引访问**：`t[0]`、`t[1]`（0 起始，支持负索引，与数组索引语法一致）
- **命名字段访问**：`t.name`（仅命名元组字段可用）
- **动态获取**：`t.get("key")`（键名可为运行期字符串）

```collie
Tuple a = (name: "Alice", age: 18);
print(a[0]);           // "Alice"
print(a.name);         // "Alice"
print(a.get("age"));   // 18
print(a.length);       // 2

Tuple p = (10, 20);    // 无名元组，仅能按索引访问
print(p[1]);           // 20
```

元组不可变：`t[0] = x` 非法。
