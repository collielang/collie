# a05 · 逻辑类型（bool 与 tribool）

演示 bool、三目运算符（含嵌套右结合）、`&&`/`||` 短路求值；tribool 三态布尔与 `==?` 多目运算符为规范目标特性。

## 状态：🟡 部分可运行

- `main.collie`：✅ bool / 三目 / 短路求值
- `future.collie`：⛔ tribool / unset / `==?` / 扩展三目 `a ? 1 : 2 : 3`

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| bool 类型与 `!` 取反 | data-type/.../05-logical.md | ✅ |
| 三目 `?:`（右结合、惰性求值） | 05-logical.md | ✅（t21） |
| `&&`/`||` 短路求值 | grammer/basic-grammer.md | ✅（M4） |
| **tribool / unset** | 05-logical.md | ⛔ 半截特性（token 有，语法未闭环；语义待作者确认） |
| **`==?` 多目运算符** | 05-logical.md | ⛔ 同上 |
| **扩展三目 `a ? 1 : 2 : 3`** | 05-logical.md | ⛔ |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a05-logic-tribool\main.collie
```

## 预期输出（实测）

```
true
false
false
true
1
2
B
false
true
  <- 副作用发生了！
true
```

注意最后三行：`false && f()` 与 `true || f()` 均未触发副作用输出，只有 `true && f()` 触发——短路语义的可观测证据。

## future.collie 当前实际行为

```
Parse error at line 9, column 13: Expect expression.（tribool t = unset;）
...共 11 个语法错误
```
