# a02 · 变量与类型

演示基础类型声明（number/string/bool/array/object）、const 常量、隐式转换、object 动态重绑定。

## 状态：🟡 部分可运行

`main.collie` 可运行；`none` 字面量部分拆到 `future.collie`（当前不可解析）。

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| number/string/bool/array/object 声明 | data-type/ | ✅ |
| `const` 常量（含语义层重赋值拦截） | grammer/basic-grammer.md | ✅（t14–t16） |
| 隐式转换 string ← number/bool | data-type/.../01-base.md | ✅（t35） |
| **`none` 字面量作为表达式** | data-type/.../02-none.md | ⛔ parser 报 `Expect expression.` |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a02-variables-types\main.collie
```

## 预期输出（实测）

```
42
3.14159
collie
true
[1, 2, 3]
动态类型，先放字符串
100
123
false
999
```

## future.collie 当前实际行为

```
Parse error at line N: Expect expression.
```

`none` 关键字存在于词法层（`KW_NONE`），但 parser 的 `parse_primary` 未接受其作为表达式。特性落地后请将 `future.collie` 合并回 `main.collie` 并更新本状态。
