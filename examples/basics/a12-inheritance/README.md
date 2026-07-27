# a12 · 继承

演示 `extends` 单继承、方法覆写、继承方法沿链查找、`: base(args)` 构造器委托、多级继承。

## 状态：🟡 部分可运行

- `main.collie`：✅ t38 已实现的继承子集
- `future.collie`：⛔ `base.method()` 显式父类调用（t39 进行中）、`@override` 注解

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| `extends` 单继承 / 方法覆写 / 沿链查找 | uncategorized.md 附录 | ✅（t38） |
| `: base(args)` 构造器委托 | uncategorized.md 附录 | ✅（t38） |
| **`base.method()` 显式父类方法调用** | uncategorized.md 附录（C# 风格） | ⛔ t39 进行中 |
| **`@override` 注解** | draft.md（注解 @deprecated/@override） | ⛔ 词法层不支持 |

## 陷阱备忘

- `base` 自 t38 起是保留关键字，不能用作变量/字段名。

## ⚠ 诊断质量发现（排错线索）

`future.collie` 中 `@override` 使 **词法器直接报错且无行号**：

```
Error during tokenization: Unexpected character
```

`@` 当前只有后跟 `"` 的插值字符串一种用法；`@identifier` 注解会触发无位置信息的 LexError——注解特性落地前，报错定位体验值得先改进。

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a12-inheritance\main.collie
```

## 预期输出（实测）

```
Generic makes a sound
Rex says Woof!
I am Rex
Puppy says Woof!
I am Puppy
```
