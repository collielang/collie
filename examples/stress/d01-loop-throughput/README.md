# d01 · 循环吞吐

纯循环压测：for 一百万次、while 一百万次、带乘/模算术负载十万次；输出校验和验证正确性。

## 状态：✅ 可运行

## 负载参数

文件顶部 `const number N = 1000000`，可调。

## 实测量级（Windows x64 Release，time_run.bat 计时）

- 总耗时 **≈ 1.1 秒**（210 万次循环体 + 10 万次算术），吞吐约 **200 万次迭代/秒**。
- 校验和全部正确：`1000000 / 2000000 / 245000`。

作为树遍历解释器这个量级正常；后续若加字节码 VM 或 JIT，本例是首选基准。

## 运行

```bat
examples\stress\time_run.bat examples\stress\d01-loop-throughput\main.collie
```

## 预期输出（实测）

```
for 完成：1000000
while 完成：2000000
算术校验和：245000
d01 完成
```
