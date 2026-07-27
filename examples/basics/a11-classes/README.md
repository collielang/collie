# a11 · 类（class 基础）

演示字段（显式/缺省初始化）、构造器、方法、`this`、public/private 修饰符、多实例隔离、外部属性读写、实例引用语义。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| class 声明（字段/方法/构造器） | uncategorized.md 附录 | ✅（t34） |
| `new` / `this` / 属性读写 | uncategorized.md 附录 | ✅（t34） |
| 实例引用语义（shared_ptr） | —（与数组一致） | ✅（t34） |
| 字段类型运行期校验 | — | ✅（t36） |
| 方法内字符串插值引用 `this.x` | 03-character.md | ✅ |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a11-classes\main.collie
```

## 预期输出（实测）

```
(5, 6)
(10, 20)
5
(100, 6)
3
4
```

注意末行 `4`：`alias = c` 后对 alias 的修改反映到 c —— 实例是引用语义。

## 已知限制（规范目标，未入示例）

静态成员、方法重载、类作为一等值、`private` 的访问控制强制（当前运行期未拦截外部访问 private 字段）。
