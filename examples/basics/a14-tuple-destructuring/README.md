# a14 · 元组与解构

演示具名元组 `(name: "Alice", age: 18)`、多变量同时赋值、变量交换 `a, b = b, a`、三变量轮转、多返回值函数（隐式元组）与解构接收。

## 状态：⛔ 等待特性

整例按规范目标语法书写，Tuple/解构落地后应可直接运行。

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| **具名元组字面量 `(name: "Alice", age: 18)`** | 03-tuple.md / draft.md | ⛔ |
| **元组成员访问 `t.name`** | draft.md | ⛔ |
| **多变量声明赋值 `number a, number b = 1, 2`** | draft.md（多变量赋值） | ⛔ |
| **变量交换/轮转 `a, b = b, a`** | draft.md（右侧先求值成临时元组再解构） | ⛔ |
| **多返回值函数 `... number, number {}` / `return lo, hi`** | draft.md / function.md | ⛔ |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a14-tuple-destructuring\main.collie
```

## 当前实际行为（实测）

13 个语法错误，退出码 1。代表性报错：

```
Parse error at line 6, column 21: Expect ')' after expression.    ← (name: "Alice", ...) 具名元组
Parse error at line 11, column 9: Expect ';' after variable declaration.  ← number a, number b = 1, 2
Parse error at line 16, column 2: Expect ';' after expression.    ← a, b = b, a
Parse error at line 26, column 37: Expect '{' before function body.  ← 多返回值类型 number, number
```

## 诊断观察（排错线索）

四类元组相关语法在解析器中的断裂点各不相同且报错均有准确行列号，可作为 Tuple 特性开发时的回归基准。
