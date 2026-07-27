# c04 · 深递归

递归深度边界探测：尾递归形、累积计算形（sumTo）；附 `probe.collie` 极限探针（顶部 `DEPTH` 可调）。

## 状态：✅ 可运行（main）＋ 探针附件（probe）

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 递归函数 | function.md | ✅（t11） |
| const 顶层参数 | D4 | ✅ |

## 实测极限（重要基准，Windows x64 Release 构建）

| 深度 | 结果 |
|-----:|------|
| 1000 | ✅ 安全（main.collie 使用） |
| 5000 | ✅ 存活 |
| 6500 | ✅ 存活 |
| 8000 | 💥 崩溃 |
| 10000 | 💥 崩溃 |

**可用递归深度 ≈ 6500～8000 层**（树遍历解释器每层消耗大量 C++ 原生栈）。

## ⚠ 崩溃质量发现（排错线索）

1. **栈溢出崩溃无任何诊断**：退出码 `3221225725`（0xC00000FD STATUS_STACK_OVERFLOW），无错误消息——建议解释器加递归深度计数器，超限抛出可读的运行时错误。
2. **崩溃时缓冲输出全部丢失**：崩溃前已执行的 `print` 一行都看不到（stdout 缓冲未刷新）——排查"程序毫无输出就退出"时首先怀疑栈溢出。
3. **相互递归当前无法实现**：语义分析单遍顺序，前向引用报 `Undefined variable 'isOdd'`——需要两遍分析（先收集全部函数签名）才能支持。

## 运行

```bat
compiler\build\Release\collie.exe examples\edge-cases\c04-deep-recursion\main.collie
compiler\build\Release\collie.exe examples\edge-cases\c04-deep-recursion\probe.collie
```

## 预期输出（实测，main.collie）

```
0
5050
500500
深递归测试完成
```

probe.collie（DEPTH=6500）输出 `探测深度：6500 / 0 / 存活`；调到 8000 以上则无输出直接崩溃。
