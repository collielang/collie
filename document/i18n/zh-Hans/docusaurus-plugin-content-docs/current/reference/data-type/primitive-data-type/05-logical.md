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

### 逻辑运算符（Kleene 三值逻辑） {/* #anthor-kleene-logic */}

`tribool` 参与逻辑运算符（`!`、`&&`、`||`）时，采用 Kleene 三值逻辑（K3）：`unset` 表示「未知」，结果能由已知一侧唯一决定时取已知值，否则为 `unset`。任一操作数为 `tribool` 时，运算在三值域上进行，结果类型为 `tribool`（`bool` 操作数按 `true`/`false` 参与）。

| `a` | `!a` |
| :---: | :---: |
| `true` | `false` |
| `false` | `true` |
| `unset` | `unset` |

| `a && b` | `b = true` | `b = false` | `b = unset` |
| :---: | :---: | :---: | :---: |
| **`a = true`** | `true` | `false` | `unset` |
| **`a = false`** | `false` | `false` | `false` |
| **`a = unset`** | `unset` | `false` | `unset` |

> 速记：有 false 则 false，无 false 时有 unset 则 unset，全 true 则 true

| `a \|\| b` | `b = true` | `b = false` | `b = unset` |
| :---: | :---: | :---: | :---: |
| **`a = true`** | `true` | `true` | `true` |
| **`a = false`** | `true` | `false` | `unset` |
| **`a = unset`** | `true` | `unset` | `unset` |

> 速记：有 true 则 true，无 true 时有 unset 则 unset，全 false 则 false

短路规则不变：`false && …` 直接得 `false`，`true || …` 直接得 `true`，右侧不求值；左侧为 `unset` 时无法短路，需求值右侧（`unset && false` 得 `false`，`unset || true` 得 `true`）。

### 条件语句中的 tribool {/* #anthor-tribool-in-condition */}

`if` / `while` 等条件位置要求 **`bool`** 类型；`tribool` 不会隐式转换，直接写 `if (t)` 是编译错误。必须用下列写法之一明确三态含义：

```collie
tribool t = unset;

// if (t) { ... }   // ❌ 编译错误：条件必须是 bool

// 写法一：自带判定属性，返回 bool
if (t.isTrue())  { /* ... */ }
if (t.isFalse()) { /* ... */ }
if (t.isUnset()) { /* ... */ }

// 写法二：显式比较（==/!= 的比较结果为 bool）
if (t == true)  { /* ... */ }
if (t == false) { /* ... */ }
if (t == unset) { /* ... */ }
```

`isTrue` / `isFalse` / `isUnset` 是 `tribool` 类型的自带属性；注意逻辑运算结果（如 `a && b`）类型仍为 `tribool`，作条件前同样需用上述写法判定。

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

:::info[`==?` 的适用范围与穷尽性规则]
`==?` 多路匹配运算符不限于逻辑类型：任意可 `==` 比较的值（`number`、`string` 等）都能匹配，相当于类 switch 的表达式（参见上方长示例）。分支必须覆盖全部可能：

- **tribool**：穷尽 `true` / `false` / `unset` 三态，或给出默认分支（末尾不带 `值:` 的分支）；
- **bool**：穷尽 `true` / `false` 两态，或给出默认分支；
- **其他类型**（`number`、`string` 等，值域无法穷举）：**必须**有默认分支。
:::

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
