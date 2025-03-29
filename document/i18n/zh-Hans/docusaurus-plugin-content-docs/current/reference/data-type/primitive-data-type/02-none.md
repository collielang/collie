---
sidebar_label: 空类型（None Type）
---

# 空类型（None Type）

## 🐳类型简介 {#intro}

空类型也是一个对象。

|  类型  | 描述   |
| :----: | ------ |
| `none` | 空类型 |

:::warning 注意
Collie 语言的空类型与其他语言有些差异， `null` 是一个特殊对象，也有其自身属性。
:::

## 🏅基础方法 {#method}

| 方法                                      | 结果   | 描述             |
| ----------------------------------------- | ------ | ---------------- |
| null.toString()                           | "null" | 将对象转为字符串 |
| null.valueEquals(object? anotherNull)     | true   |                  |
| null.referenceEquals(object? anotherNull) | true   |                  |

## 🏅语法示例 {#syntax-example}

- 定义一个空：

```collie
none nullObject = null; // none 类型的所有对象都是 null(引用和值均相等), 所以这里也可以简写作：none nullObject;

if (nullObject.isNull()) { // 或 if (nullObject == null) {
    print("人生就是一场空");
}
```
