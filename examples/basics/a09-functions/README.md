# a09 · 函数

演示函数定义/调用、参数与返回值、递归（阶乘）、提前 return、`none` 返回类型、函数互调与嵌套调用。

## 状态：🟡 部分可运行

- `main.collie`：✅ `function` 过渡文法全部特性
- `future.collie`：⛔ C 风格声明 / 多返回值 / 具名实参 / 可空参数 / 函数作为值

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| `function name(a number) number {}` 过渡文法 | D10 决策 | ✅（t11） |
| 递归 / 提前 return / none 返回 | grammer/function.md | ✅（t11） |
| **C 风格 `public number, string getAge()`** | function.md（目标设计） | ⛔ |
| **多返回值（隐式元组）与解构接收** | function.md / draft.md | ⛔ |
| **具名实参 `fooBar(age: 18)` / 可空参数 `number?`** | draft.md | ⛔ |
| **函数作为值传递** | draft.md | ⛔ |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a09-functions\main.collie
```

## 预期输出（实测）

```
7
Hello, Collie!
3628800
负数
零
正数
[LOG] 系统启动
25
```

## future.collie 当前实际行为

27 个语法错误（`public` 打头的 C 风格声明整体不可解析）。
