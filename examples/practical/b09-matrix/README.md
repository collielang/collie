# b09 · 矩阵运算

二维数组（嵌套 array 字面量）：矩阵加法、转置、矩阵乘法（2×3 · 3×2）、单位矩阵判定。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 嵌套数组字面量 / 尾逗号 | 01-collection.md / draft.md | ✅ |
| 二维索引读写 `m[i][j] = ...` | 01-collection.md | ✅（链式下标赋值可用） |
| `len(m[i])` 内层长度 | 内建 | ✅ |

## 陷阱备忘

- 无 push/预分配，结果矩阵必须用嵌套字面量**预先写出全 0 形状**。
- 语义分析限制（同 b03/b06）：`m[i][j] == 1` 需先取 `number v = m[i][j];` 再比较。
- 读取二维元素参与运算也建议先落变量（`number av = a[i][k];`），一致绕开 Incomparable 拒绝。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b09-matrix\main.collie
```

## 预期输出（实测）

```
a + b =
[11, 22, 33]
[44, 55, 66]
aᵀ =
[1, 4]
[2, 5]
[3, 6]
a × aᵀ =
[14, 32]
[32, 77]
true
false
```
