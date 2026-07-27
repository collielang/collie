# a08 · label 语句

演示规范中的 label 跳转：`break label` 跳出指定嵌套循环、`continue label` 跳过外层循环当前轮、代码块 label（`break` 跳出嵌套块）。

## 状态：⛔ 等待特性（整个示例按规范目标语法书写）

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| label 定义 `name:` | grammer/control-flow.md | ⛔ `Expect ';' after expression.` |
| `break label;` | control-flow.md | ⛔ `Expect ';' after 'break'.` |
| `continue label;` | control-flow.md | ⛔ `Expect ';' after 'continue'.` |
| 代码块 label + break | control-flow.md | ⛔ |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a08-labels\main.collie
```

## 当前实际行为（实测）

```
Parse error at line 7, column 11: Expect ';' after expression.
...共 20 个语法错误，非零退出
```

parser 将 `outer_loop:` 解析为普通表达式后遇 `:` 报错；`break`/`continue` 后不接受标识符。

## 特性落地后的预期输出（人工推演，待验证）

```
x=0, y=0
...（x+y<=5 的组合）
找到 6 x 7 = 42
进入 logic1
进入 logic2
foo 结束
```
