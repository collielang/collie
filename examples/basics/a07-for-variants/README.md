# a07 · for 循环家族

规范定义了 6 种 for 形态：经典三段式、for-each、for-map、for-number、for-range、`for!` 死循环。当前仅经典三段式可用。

## 状态：🟡 部分可运行

- `main.collie`：✅ 经典 for（含嵌套、break/continue、空缺三段式）
- `future.collie`：⛔ for-each / for-number / for-range / `for!`

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 经典 for（含省略初始化/更新） | grammer/control-flow.md | ✅ |
| **for-each `(item : arr)` / `(item, index : arr)`** | control-flow.md | ⛔ `Expect variable name.` |
| **for-number `for (3)` / `for (i : 3)`** | control-flow.md | ⛔ |
| **for-range `for (i : 1, 5)`** | control-flow.md | ⛔ |
| **`for!` 死循环** | control-flow.md | ⛔ `Expect '(' after 'for'.` |
| for-map | control-flow.md | ⛔ 依赖 Map 类型（未实现，未写入示例） |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a07-for-variants\main.collie
```

## 预期输出（实测）

```
1..100 求和 = 5050
1x1=1 
1x2=2 2x2=4 
1x3=3 2x3=6 3x3=9 
3 的倍数: 3
3 的倍数: 6
倒计时 3
倒计时 2
倒计时 1
```

## future.collie 当前实际行为

38 个语法错误（`for (item : fruits)` 报 `Expect variable name.`；`for!` 报 `Expect '(' after 'for'.`）。
