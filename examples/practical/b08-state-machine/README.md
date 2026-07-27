# b08 · 状态机

订单生命周期状态机：`created → paid → shipped → delivered`（cancelled 分支）；switch 驱动转移表、非法转移拦截、终态封闭、转移计数。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| switch 字符串值分支 / 多值 case | control-flow.md | ✅（t14/t15） |
| class 状态封装 / const 全局常量 | uncategorized.md / D4 | ✅ |
| 方法内 switch(this.state) | — | ✅ |

## 设计备忘

- enum ⛔ 未实现，状态用 `const string` 模拟 —— a13 enum 落地后本例是天然的重构对照点（`switch` 的 case 也应换成 `OrderState.Paid`）。
- switch 的 case 必须是**字面量**：`ST_PAID` 常量不能作 case 标签，因此 case 处直接写 `"paid"` 字符串。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b08-state-machine\main.collie
```

## 预期输出（实测）

```
[A001] created --pay--> paid
[A001] paid --ship--> shipped
[A001] shipped --deliver--> delivered
[A001] 非法转移：delivered --pay-->（忽略）
[A001] 终态：delivered，共 3 次转移
[A002] created --pay--> paid
[A002] paid --cancel--> cancelled
[A002] 非法转移：cancelled --ship-->（忽略）
[A002] 终态：cancelled，共 2 次转移
[A003] 非法转移：created --deliver-->（忽略）
[A003] 非法转移：created --ship-->（忽略）
[A003] 终态：created，共 0 次转移
```
