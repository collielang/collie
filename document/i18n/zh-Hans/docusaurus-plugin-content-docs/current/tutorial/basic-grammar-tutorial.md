---
sidebar_position: 3
sidebar_label: 基础语法教程
---

# 基础语法教程

:::danger TODO
以下文档由 AI 生成，仅供参考，需要重写
:::

## 变量声明

在 Collie 中，你可以使用 `var` 关键字声明变量：

```collie
var num = 10;
var str = "Hello";
```

## 数据类型

Collie 支持多种数据类型，如整数、浮点数、字符串等：

```collie
number num = 20;
string str = "Collie";
```

## 控制结构

### if 语句

```collie
var num = 10;
if (num > 5) {
    println("num 大于 5");
} else {
    println("num 小于等于 5");
}
```

### for 循环

```collie
for (var i = 0; i < 5; i++) {
    println(i);
}
```
