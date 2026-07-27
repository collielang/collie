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
| **tribool / unset** | 05-logical.md | ⛔ 半截特性（token 有，语法未闭环；**语义已澄清，见下**） |
| **`==?` 多目运算符** | 05-logical.md | ⛔ 同上（通用性与穷尽性规则已澄清） |
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

## 语义澄清（作者定夺，2026-07，已写入 05-logical.md）

1. **逻辑运算符（`!` `&&` `||`）采用 Kleene 三值逻辑（K3）**：`unset` 表示未知，结果能由已知一侧唯一决定时取已知值（`unset && false` → `false`，`unset || true` → `true`），否则为 `unset`。
2. **`if`/`while` 条件必须是 `bool`**：`if (tribool)` 是编译错误；须用自带属性 `t.isTrue()` / `t.isFalse()` / `t.isUnset()`，或显式比较 `t == true/false/unset`。
3. **`==?` 通用于任意可 `==` 比较类型**（类 switch 表达式）：tribool 须穷尽三态或给默认分支；`number`/`string` 等值域不可穷举的类型**必须**有默认分支。

## future.collie 当前实际行为

```
Parse error at line 13, column 13: Expect expression.（tribool t = unset;）
...共 20 个语法错误
```
