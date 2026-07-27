---
sidebar_label: 逻辑类型（Logical Type）
---

# 逻辑类型（Logical Type）

:::danger[是否值得改为 yes, no? (但是 json 转换时可能会不一致)]
- 答：不值得。
- 结论：使用 true, false
:::

## 🐳类型简介 {/* #intro */}

|   类型    | 描述                                                        |
| :-------: | ----------------------------------------------------------- |
|  `bool`   | 二值逻辑（布尔类型），值为 `true` 或 `false`，默认值为 `false`          |
| `tribool` | 三值逻辑（三元类型），值为 `true`，`false` 或 `unset`，默认值为 `unset` |


:::warning[TODO]
需要确认是否设计默认值（看是否可以实现类似 Java 那样的不初始化就不允许使用）
:::

## 布尔类型（Boolean Type） {/* #anthor-boolean-type */}

### 🏅语法示例 {/* #syntax-example */}

- 定义布尔类型：

```collie
bool a = true;
bool b = !a; // result: false

a == false // result: false
a == true  // result: true
```

- 多目运算符及其简化形式：

```collie
bool a;
// 此处省略给 a 变量赋值的相关逻辑

// 简化形式
a ? 1 : 2
// when a equals true,  result: 1
// when a equals false, result: 2

// 原始写法
a ==? true: 1, false: 2
// when a equals true,  result: 1
// when a equals false, result: 2

a ==? false: 1, 2
// when a equals false,       result: 1
// otherwise (a equals true), result: 2
```

## 三态布尔类型（Tri-State Boolean Type） {/* #anthor-tri-state-boolean-type */}

三态布尔类型由布尔类型扩展而来，添加了 `unset` 选项。`unset` 取反仍为 `unset`，其他特性均与布尔类型一致。

> 参考: Kleene 三值逻辑

:::warning[TODO]
tribool 参与逻辑运算符（! && ||）时，unset 如何处理？文档只定义了 ==? 和三元，没写逻辑运算。
我觉得 Kleene 三值逻辑更合适，但是放在if(tribool)这里不允许，也就是说，这里必须 if(tribool.isTrue()) /isFalse/isUnset，或者 if (tribool ==unset) / true/false 这几种写法明确含义。（可以理解为，isTrue/isFalse/isUnset是tribool类型的自带属性，而 if() 条件必须是 bool）
`==?` 多路匹配运算符是否通用于任意类型（number/string 等），还是仅限 tribool？
任意可 == 比较的值都能匹配（类 switch 表达式），与文档长示例一致。tribool 要求穷尽三态或给默认分支；其他类型必须有默认分支。
:::

### 🏅语法示例

- 定义三态布尔类型：

```collie
tribool a = unset;
```

- 布尔类型、三态布尔类型的比较：

```collie
tribool a, bool b = unset, false;
a == b  // result: false
a == !b // result: false

unset == true   // result: false
unset == false  // result: false
unset == !unset // result: true

!true  // result: false
!false // result: true
!unset // result: unset
```

- 多目运算符：

```collie
tribool a;
// 此处省略给 a 变量赋值的相关逻辑

a ==? unset: 1, true: 2, false: 3
// when a equals unset, result: 1
// when a equals true,  result: 2
// when a equals false, result: 3

a ==? unset, true: 2, false: 3
// when a equals true or unset,  result: 2
// when a equals false,          result: 3

a ==? unset, true: 1, 2
// when a equals unset or true, result: 1
// otherwise (a equals false),  result: 2

a ==? unset: 1, 2
// when a equals unset,         result: 1
// when a equals false or true, result: 2

a ==? 2, unset: 1
// when a equals unset,                result: 1
// otherwise (a equals false or true), result: 2

// a ==? unset, true: 2
// ❌ 不允许的写法：缺少 false 分支

/* 如果表达式较长，推荐的格式如下
// 注意，value1, value3 如果都与 hereIsAVeryLongParamName 相等，则会返回第一个匹配上的条件对应结果 (expression 1)
object a = hereIsAVeryLongParamName ==?
                value1, value2: {
                    expression 1
                },
                value3: {
                    expression 2
                },
                expression 3
 */
```

- 多目运算符简化形式：

```collie
tribool a;
// 此处省略给 a 变量赋值的相关逻辑

a ? 1 : 2 : 3
// when a equals true,  result: 1
// when a equals false, result: 2
// when a equals unset, result: 3

a ? 1 : 2
// when a equals true,           result: 1
// when a equals false or unset, result: 2

/* 如果表达式较长，推荐的格式如下
object a = hereIsAVeryLongParamName
               ? expression 1
               : expression 2
               [: expression 3]
 */
```
