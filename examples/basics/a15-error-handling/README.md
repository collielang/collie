# a15 · 错误处理

演示传统 try-catch-finally、`throw new Exception(...)` 主动抛出、try-with-resources、嵌套 try 与异常向外传播（finally 先行执行）。

## 状态：⛔ 等待特性

整例按规范目标语法书写，异常处理落地后应可直接运行。

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| **`try { } catch (Exception e) { } finally { }`** | uncategorized.md「错误处理」 | ⛔ |
| **`throw new Exception("msg")` / `e.message`** | uncategorized.md 附录 | ⛔ |
| **try-with-resources `try (res = open(...))`** | uncategorized.md / draft2.md | ⛔ |
| **除零抛异常语义** | uncategorized.md 附录（`10 / 0` → Division by zero） | ⛔（当前实现为 IEEE 754 Infinity，见 a03） |
| Exception 类型 / 异常类体系 | error-handling.md（TODO 占位） | ⛔ 规范本身未定稿 |

## 规范冲突备忘

`10 / 0`：uncategorized.md 附录期望抛异常，但**当前实现已确认走 IEEE 754**（`Infinity`，a03 已实测）。异常特性落地时需要作者裁决二者取舍——本例第 1 段保留了规范原文写法，届时可能需要换成显式 `throw`。

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a15-error-handling\main.collie
```

## 当前实际行为（实测）

61 个语法错误，退出码 1。首错：

```
Parse error at line 6, column 5: Expect ';' after expression.
```

## 诊断观察（排错线索）

`try` 未注册为关键字，被当作普通标识符表达式解析，随后的 `{` 触发断裂；每个 try/catch/finally 块引发 5～10 个级联错误，61 错是 A 系列 ⛔ 示例中错误恢复放大最严重的一例——异常语法落地前，解析器对「标识符后跟块」的错误恢复策略值得优化。
