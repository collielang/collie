# b03 · 二分查找

迭代版 / 递归版二分查找 + `lowerBound` 插入点定位；覆盖头/中/尾命中、未命中、空数组边界。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| while / 递归 / 提前 return | control-flow.md | ✅ |
| `.integerPart()` 取整（模拟整除） | 04-numeric.md | ✅（t30） |
| 数组索引 / `len()` | 01-collection.md | ✅ |

## 陷阱备忘（⚠ 语义分析发现）

- **`arr[mid] == target` 被语义分析拒绝**：`Incomparable operand types`（下标表达式静态类型非 number）。
  必须先 `number midVal = arr[mid];` 再比较。
- 有趣的不对称：同样场景下 **`arr[mid] < target` 却能通过**（b02 的 `arr[j] > arr[j+1]` 也通过）——
  相等比较与大小比较在语义检查中规则不一致，可作类型检查完善的线索。
- 无整除运算符，`(lo + hi) / 2` 会得小数，需 `.integerPart()`。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b03-binary-search\main.collie
```

## 预期输出（实测）

```
0
3
6
-1
-1
-1
4
-1
3
0
7
-1
```
