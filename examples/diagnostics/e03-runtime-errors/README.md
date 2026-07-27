# e03 · 运行时错误集（预期失败）

通过语法与语义检查、只在运行时暴露的故障。运行时**首错即停**，因此每种错误独立成文件。

## 状态：⛔ 预期失败（main / string_oob 退出码 1；tonumber_probe 意外地退出码 0，见下）

## 文件与实测结果

| 文件 | 植入错误 | 退出码 | 实测诊断 |
|---|---|---|---|
| `main.collie` | 数组越界 `arr[3]`（size 3） | 1 | `Runtime error at line 7, column 10: Index 3 out of range (size 3)` |
| `string_oob.collie` | 字符串越界 `s[5]`（size 3） | 1 | `Runtime error at line 6, column 8: Index 5 out of range (size 3)` |
| `tonumber_probe.collie` | `toNumber("12ab")` / `toNumber("hello")` | **0** | 静默返回 `NaN`，程序继续执行 |

## 运行

```bat
cmd /c compiler\build\Release\collie.exe examples\diagnostics\e03-runtime-errors\main.collie
cmd /c compiler\build\Release\collie.exe examples\diagnostics\e03-runtime-errors\string_oob.collie
cmd /c compiler\build\Release\collie.exe examples\diagnostics\e03-runtime-errors\tonumber_probe.collie
```

## 实测输出（main.collie）

```
越界前：正常执行
30
Runtime error at line 7, column 10: Index 3 out of range (size 3)
```

## 诊断质量观察（排错素材）

- **越界诊断是运行时最好的一档**：精确行列号 + 实际索引 + 容器大小，数组与字符串共用同一套报错（`Index N out of range (size M)`），一致性好。
- 中止前的正常输出**已刷出**（对比 c04 栈溢出连缓冲都丢），说明普通运行时错误走的是受控中止路径。
- **`toNumber` 非法输入静默返 `NaN` 且退出码 0**——不是错误而是哨兵值，且 `"12ab"` 不做前缀解析（不同于 JS `parseFloat` 的 12，行为类似 JS `Number()`）。业务代码必须自行判 NaN，否则脏数据会顺流而下；语言未提供 `isNaN`，可用 `x != x` 判定（IEEE 754 NaN 特性，见 c01）。
- 运行时错误一次只能暴露一个（首错即停），与 e02 语义批量报错形成对照 —— 修复运行时错误注定是逐个迭代的过程。

## 与其他系列的运行时故障对照

| 故障 | 诊断 | 退出码 | 输出保留 |
|---|---|---|---|
| 数组/字符串越界（本例） | 精确行列号 | 1 | 保留 |
| 栈溢出（c04） | 无任何输出 | 3221225725 | **全部丢失** |
| 除零（c01） | 非故障：得 `+Infinity` | 0 | — |
| toNumber 非法串（本例） | 非故障：得 `NaN` | 0 | — |
