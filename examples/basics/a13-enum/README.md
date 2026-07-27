# a13 · 枚举

演示 `enum` 基础声明、枚举属性（构造参数）、枚举属性 + 枚举值（`: type` 后缀与 `===` 值比较）、枚举参与 switch 多值分支。

## 状态：⛔ 等待特性

整例按规范目标语法书写，enum 落地后应可直接运行。

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| **`enum Name { V1, V2, }` 基础枚举** | 07-enum.md / uncategorized.md | ⛔ |
| **枚举属性 `enum Season(string name)` + `V(name='春')`** | 07-enum.md | ⛔ |
| **枚举值 `enum E(...) : type { V(...) = 值 }` 与 `===` 比较** | 07-enum.md | ⛔ |
| **枚举成员访问 `Season.Spring` / `.label`** | 07-enum.md | ⛔ |
| switch 多值分支（枚举作 case 值） | control-flow.md | 🟡（switch ✅，枚举值 ⛔） |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a13-enum\main.collie
```

## 当前实际行为（实测）

7 个语法错误，退出码 1。首错：

```
Parse error at line 6, column 13: Expect ';' after variable declaration.
```

## 诊断观察（排错线索）

`enum Season {` 被解析器当作「类型 `enum` + 变量名 `Season`」的变量声明处理后在 `{` 处断裂——说明 `enum` 尚未注册为关键字/声明起始符。错误数少（7 个）是因为枚举体内的 `V,` 行恰好能被错误恢复跳过。
