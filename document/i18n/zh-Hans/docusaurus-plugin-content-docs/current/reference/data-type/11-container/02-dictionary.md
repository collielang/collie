---
sidebar_label: 字典类型（Dictionary Type）（TODO）
---

# 字典类型（Dictionary Type）

:::info

字典类型为[可变类型](./#mutable-data-type)。

:::

|         类型          | 描述                     |
| :-------------------: | ------------------------ |
| `map[string: number]` | 字典类型。支持键值对存储 |

### 🏅语法示例

```collie
var set = set(1, 2, 3);       // var 自动推断类型为 set[number]
var map = map({ apple: 5, banana: 6 }); // var 自动推断类型为 map[string: number]
```

- TODO

```collie
- 提供方法 `last()`：获取最后一个元素
- 提供转换方法 `toMap()`：将 List 转换为 Map
```
