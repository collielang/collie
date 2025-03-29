---
sidebar_label: 联合类型（Union Type）
---

# 联合类型（Union Type）

:::info

联合类型为[不可变类型](./#immutable-data-type)。

:::

## 🐳类型简介 {#intro}

```collie
string | number obj = 'aaa';
obj = 3.14;

string | character separator = ',';
[1, 2, 3].join(separator); // result: "1,2,3"
separator = " and ";
[1, 2, 3].join(separator); // result: "1 and 2 and 3"
```
