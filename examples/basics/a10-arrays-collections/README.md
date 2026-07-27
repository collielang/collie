# a10 · 数组与容器

演示 array 字面量、索引读写、负索引、嵌套数组、**引用语义陷阱**、`len()`/`.length`、深度相等比较、遍历求和。

## 状态：🟡 部分可运行

- `main.collie`：✅ array 基础全部特性
- `future.collie`：⛔ 切片 / 展开 / `+=` 拼接 / 索引复合赋值 / List/Set/Map

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 数组字面量（含尾逗号）/ 索引 / 负索引 | container/ 与 draft.md | ✅（t22） |
| 引用语义（赋值共享底层数组） | draft.md（`a[:]` 浅拷贝概念的对偶） | ✅（t22） |
| `len()` / `.length` / 深度相等 | container/ | ✅（t22/t28） |
| **切片 `a[1:-1]` / `a[:]`** | draft.md | ⛔ `Expect ']' after index expression.` |
| **展开 `[...a]`** | draft.md | ⛔ |
| **`a += [4,5]` 拼接 / `a[0] += 1` 索引复合赋值** | draft.md | ⛔ `Invalid compound assignment target.` |
| **List/Set/Map 泛型容器** | container/01-collection.md | ⛔ 泛型语法不可解析 |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a10-arrays-collections\main.collie
```

## 预期输出（实测）

```
[1, 2, 3, 4, 5]
5
5
1
5
[10, 2, 3, 4, 5]
3
[alpha, beta]
[1, two, true]
[99, 2, 3]
true
总价 = 24.5
```

## future.collie 当前实际行为

24 个语法错误（切片冒号、展开 `...`、泛型尖括号均不可解析）。
