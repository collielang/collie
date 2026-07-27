# c03 · 数组边界

空数组、单元素+尾逗号、混合类型、5 层深嵌套、引用别名陷阱（含嵌套共享）、负索引边界、越界访问诊断。

## 状态：✅ 可运行（main）＋ 预期失败附件（oob）

- `main.collie`：✅ 全部边界用例正常退出
- `oob.collie`：**故意越界**，预期运行时错误退出（退出码 1）

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| 空数组 / 尾逗号 / 混合类型 / 嵌套 | 01-collection.md / draft.md | ✅ |
| 深度相等 `==`（非引用比较） | —（作者确认） | ✅（t24） |
| 引用语义（别名共享底层数据） | — | ✅ |
| 负索引 `a[-len]` 边界 | 01-collection.md | ✅（t23） |
| 越界运行时诊断 | — | ✅ 带行列号 |

## 越界行为（实测，oob.collie）

```
before out-of-bounds
Runtime error at line 50, column 9: Index 3 out of range (size 3)
```

- 报错**带精确行列号与越界详情**，随即中止（`after` 不输出），退出码 1 —— 诊断质量良好。

## 运行

```bat
compiler\build\Release\collie.exe examples\edge-cases\c03-array-edge\main.collie
compiler\build\Release\collie.exe examples\edge-cases\c03-array-edge\oob.collie
```

## 预期输出（实测，main.collie）

```
0
[]
true
1
42
42
4
[3, 4]
99
[100, 2, 3]
true
true
[[77]]
[[77]]
10
c03 main 完成
```

要点：`origin == clone` 为 true 证明 `==` 是深度相等；`outer1/outer2` 同步变 `[[77]]` 证明嵌套元素共享引用。
