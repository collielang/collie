# d02 · 字符串拼接压测

累积拼接 `s = s + "x"` 分级压测（1k/5k/20k），长串上索引/负索引/subString 正确性验证，中文码点长串（UTF-8 多字节路径）2000 码点。

## 状态：✅ 可运行

## 负载参数

顶部 `SMALL=1000 / MEDIUM=5000 / LARGE=20000`。

## 实测量级

- 总耗时 **< 0.1 秒**（26000 次拼接 + 2000 次中文拼接）——此量级下无 O(n²) 显著劣化。
- 想观察劣化曲线可将 LARGE 调至 200000+。
- 长串（2 万字符）上的索引、负索引、subString 结果全部正确；中文 2000 码点串 `zh[1999]` 精确命中。

## 运行

```bat
examples\stress\time_run.bat examples\stress\d02-string-concat\main.collie
```

## 预期输出（实测）

```
SMALL(1000) 长度 = 1000
MEDIUM(5000) 长度 = 5000
LARGE(20000) 长度 = 20000
x
x
xxxxx
中文长串码点数 = 2000
犬
d02 完成
```
