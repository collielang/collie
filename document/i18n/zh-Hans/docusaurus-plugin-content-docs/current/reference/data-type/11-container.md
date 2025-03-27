---
sidebar_position: 11
sidebar_label: 容器类型（Container Type）
---

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

# 容器类型（Container Type）

:::info

容器类型为[可变类型](./#mutable-data-type)。

:::

## 🐳类型简介 {#intro}

数据容器支持动态添加、删除、修改、获取元素。

<Tabs>
  <TabItem value="mermaid" label="Mermaid" default>
    <!-- https://mermaid.js.org/syntax/classDiagram.html#define-namespace -->
    ```mermaid
    classDiagram
    namespace Collections {
        class Collection

        class AbstractListCollection
        class AbstractSetCollection

        class list
        class set
    }

    namespace Dictionarys {
        class Dictionary
        class AbstractDictionary

        class map
    }

    object <|-- Collection
    object <|-- Dictionary

    Collection <|-- AbstractListCollection : implements
    Collection <|-- AbstractSetCollection : implements
    Dictionary <|-- AbstractDictionary : implements

    AbstractListCollection <|-- list : extends
    AbstractSetCollection <|-- set : extends
    AbstractDictionary <|-- map : extends

    <<interface>> Collection
    <<abstract>> AbstractListCollection
    <<abstract>> AbstractSetCollection
    <<abstract>> AbstractDictionary
    <<clazz>> list
    <<clazz>> set
    <<clazz>> map
    <<interface>> Dictionary
    object : equals()
    ```
  </TabItem>
  <TabItem value="plantuml" label="Plantuml (not support)">
    ```plantuml
    @startuml

    class object

    package "Collections" #EEEEEE {
        interface Collection
        object <|-- Collection

        abstract AbstractListCollection
        abstract AbstractSetCollection
        Collection <|-- AbstractListCollection
        Collection <|-- AbstractSetCollection

        class list
        class set

        AbstractListCollection <|-- list

        AbstractSetCollection <|-- set
        ' list <|-- set
    }

    package "Dictionarys" #EEEEEE {
        interface Dictionary
        object <|-- Dictionary

        abstract AbstractDictionary
        Dictionary <|-- AbstractDictionary

        class map

        AbstractDictionary <|-- map
    }

    object : equals()

    @enduml
    ```
  </TabItem>
</Tabs>

## 数组与集合类型

|      类型      | 描述                          |
| :------------: | ----------------------------- |
| `list[object]` | 元素**可重复**的**有序**集合   |
| `set[object]`  | 元素**不可重复**的**无序**集合 |

:::danger TODO
refer:
- JDK
- C++ STL: https://www.cnblogs.com/cscshi/p/15612343.html

集合 Collection
- add(): boolean 返回是否真正添加成功（对于 set 插入已有元素时，返回 false）
- remove(element)
- removeByIndex()
- removeFirst() / removeFirstOrThrow()
- removeLast() / removeLastOrThrow()

队列 Queue
- push()
- pop()
- popOrThrow() // 如果没有元素，则调用报错

栈 Stack
- push()
- pop()
- popOrThrow() // 如果没有元素，则调用报错

双向队列
- poll()/popIfExists(): 从最前面取出一个元素，空集合返回 null
- pop()/removeFirst()
- peakFirst(): 获取第一个元素，但不从集合中移除

---

- List
  - 数组：ArrayList
  - 链表：LinkedList
  - 不可修改数组：UnmodifiableList
  - 无序数组：UnorderedList
  - 不可重复无序：ArraySet
- Set
- 先进先出列表（队列）：FifoList / Queue
- 后进先出列表（栈）：LifoList / Stack
- 双向进出列表（双向队列）：Deque
:::

### 🏅基础方法 {#method}

| 方法                                                      | 描述                                                         |
| --------------------------------------------------------- | ------------------------------------------------------------ |
| collection.toString()                                     | 将数组 / 集合对象转为字符串。以 `','` 连接所有元素。对每个元素都会调用其 `.toString()` 方法 |
| collection.join(string\|character str)                    | 对每个元素调用其 `.toString()` 方法，并将所得字符串使用 `str` 进行拼接 |
| collection.valueEquals(collection? anotherCollection)     | 比较两个数组 / 集合对象元素个数及每个元素的值是否相等。对每个元素都会调用其 `.valueEquals()` 方法 |
| collection.referenceEquals(collection? anotherCollection) | 比较两个数组 / 集合对象元素个数及每个元素的引用是否相等。对每个元素都会调用其 `.referenceEquals()` 方法 |

<!--
#### 数组基础方法

| 方法                                                      | 描述                                                         |
| --------------------------------------------------------- | ------------------------------------------------------------ |
| collection.addFirst/addLast(object object1[, object object2[, ...]]) | 向 collection 对象最前面 / 最后面添加若干个元素 |
| collection.addAllFirst/addAllLast([object] objectList1[, object objectList2[, ...]]) | 将传入的若干数组元素按先后次序，逐一添加到 collection 对象最前面 / 最后面 |
-->

### 🏅语法示例 {#syntax-example}

- 定义

```collie
// 使用 new 关键字初始化
list[number] = new list(1, 2, 3, 4, 5);

// 字面量初始化
var list1 = list(1, 2, 3, 4); // var 自动推断类型为 list[number]
var list2 = [1, 2, 3, 4]; // 也可以直接使用 `[]`
```

- 数组拼接：

```collie
var list1 = list(1, 2, 3, 4);
var list2 = list(5, 6, 7, 8);

[number] list;
list = list1 + list2; // list 为新数组，不会改变原数组 result: [1, 2, 3, 4, 5, 6, 7, 8]
list = [...list1, ...list2]; // 同上
list = [...list1].concat(list2); // 同上

list = list1.concat(list2); // 将 list2 的元素全部添加到 list1 中，并且返回。此时 list list1 指向统一数组，list2 不变。
```

- 多维数组：

```collie
// 二维数组
list[[number]] numList_2 = [[1, 2], [3, 4], [5, 6]];

// 三维数组
list[[[number]]] numList_3 = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];

// 数组拍平
list[number] flatNumList = numList_3.flat(); // 拍平数组。返回新数组，不改变原数组 result: [1, 2, 3, 4, 5, 6, 7, 8]
```

?> *TODO 多维数组是否可以写作：`number[n]`？（n为一个 integer）*

- 数组、集合互转：

```collie
// 数组转集合
set[[number]] numSet = numList_2.toSet(referenceEquals: true);
// 注意，referenceEquals 为 true 时按照引用去重，为 false 时将对比数组每一项其中的值是否相等。默认为 true

// 集合转数组
list[[number]] numList = numSet.toList();
// 如果需要排序，可以使用 `numSet.toList().sortBy()`
```

- 多类型数组：

```collie
list[number | [number]] list1 = [1, [2, 3], 4];
list[string | [number]] list2 = ["1", [2, 3], "4"];

// 注意：多类型的数组遍历时只能使用其公有方法
for (item : list2) {
	// 因为 string 和 [number] 都支持获取长度，所以这里 ✅ 可以使用
	integer len = item.length;
	// 因为 string 和 [number] 都支持 + 运算符，但是 + 右侧所需类型不同，所以这里 ❌ 不能使用
	// 因为 string 和 [number] 都支持 * 运算符，所以这里 ✅ 可以使用
	item *= 2;
}
// 此时 list2 的值为: ["11", [2, 3, 2, 3], "44"];
```



## 字典类型

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
