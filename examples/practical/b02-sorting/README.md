# b02 · 排序算法

冒泡排序、选择排序（就地交换）、`isSorted` 判定；覆盖重复元素、负数、空数组、单元素边界。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 数组索引读写 `arr[i] = ...` | 01-collection.md | ✅（t22） |
| 数组引用语义（函数内修改可见） | —（作者确认） | ✅ |
| 嵌套 for / 函数 | control-flow.md / D10 | ✅ |

## 陷阱备忘

- `arr[i] += x` 不支持（复合赋值仅限变量），必须写 `arr[i] = arr[i] + x`。
- 数组传参是引用：`bubbleSort(data1)` 就地排序，无需接收返回值。
- 当前没有数组 push/append 内建，无法在函数内构造副本 —— 想保留原数组需在调用前另写字面量。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b02-sorting\main.collie
```

## 预期输出（实测）

```
排序前：[5, 2, 9, 1, 7, 3]
冒泡后：[1, 2, 3, 5, 7, 9]
true
选择后：[-7, -1, 0, 2, 4, 4]
true
true
[42]
```
