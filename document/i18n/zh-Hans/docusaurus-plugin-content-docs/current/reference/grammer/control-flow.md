---
sidebar_position: 2
sidebar_label: 流程控制
---

# 流程控制

## 语句块

## 条件语句

### if ... else ... 语句

## 循环与迭代

### 循环语句

#### for

- **for** 循环

  ```collie
  for (初始化; 条件; 更新) {
      // 执行代码块

      if (you feel sad, we just skip current case, and move on to the next case) {
      	// maybe you want to do some thing before we continue...
          continue;
      }
  }
  ```

- **for-each** 循环

  ```collie
  for (item : ArrayOrList) { // 或 for (item in ArrayOrList)
      // 执行代码块
  }
  ```

- **for-map** 循环

  ```collie
  for (key, value : map) { // 或 for (key, value in map) {
      // 执行代码块
  }
  for (entry : map) { // 或 for (entry in map) {
      var key = entry.key;
      var value = entry.value;
      // 执行代码块
  }
  ```

- **for-number** 循环

  ```collie
  for (number) { // 循环 number 次
      // 执行代码块
  }
  // 如果需要
  for (i : number) { // 循环 number 次，i 的值从 0 至 number-1
      // 执行代码块
  }
  ```

- **for-range** 循环

  ```collie
  for (i : 1, 5) { // 循环 4 次，i 的值从 1 至 4
      // 执行代码块
  }
  ```

- **for-iterator** 循环：使用迭代器循环

  ```collie
  for (item : iterator) {
      // 执行代码块
  }
  ```

- **dead-for** 循环

  ```collie
  for! { // 或者 for (true) {
      // 执行代码块

      // 一直循环，直到 break 跳出，等价于 while(true)
      if (some condition) {
      	break;
      }
  }
  ```

  :::danger
  另一个方案：使用 `loop` 循环
  :::




#### while

- **while** 循环

  ```collie
  while (条件) {
      // 执行代码块
  }
  ```

- **do-while** 循环
  ```collie
  do {
      // 执行代码块
  } while (条件)
  ```



:::tip
如果需要连续跳过多次循环，可以使用 `continue(integer);` 语句。

例如：
```collie
[list] fruitList = [
    "Apple", "Banana", "Orange", "Grape", "Strawberry",
    "Watermelon", "Pineapple", "Mango", "Kiwi", "Blueberry",
    "Peach", "Pear", "Cherry", "Lemon", "Coconut",
    "Papaya", "Raspberry", "Blackberry", "Durian", "Pomegranate"
]
[list] resultList = []
for (fruit : fruitList) {
    resultList.add(fruit)
    continue(5);
}
// resultList: [ "Apple", "Watermelon", "Peach", "Papaya" ]
```
:::

::::tip
如果需要连续跳出多次循环，可以使用 `break(integer);` 语句。

```collie
// 定义5个不同类别的水果列表
const colors = ['red', 'green', 'yellow'];
const fruits = ['apple', 'grape', 'banana'];
const sizes = ['small', 'medium', 'large'];
const origins = ['domestic', 'imported'];
const qualities = ['A', 'B', 'C'];

// 5层嵌套循环
for (const color of colors) { // level 1
    for (const fruit of fruits) { // level 2
        for (const size of sizes) { // level 3
            for (const origin of origins) {  // level 4
                for (const quality of qualities) { // level 5
                    log(`A ${color} ${size} ${fruit} from ${origin} origin, quality ${quality}`);

                    if (fruit == "grape") {
                        log("Oh! I hate $fruit$!")
                        // When fruit is "grape", we jump out of the loop one by one
                        // in order: level 5, level 4, level 3
                        break(4);
                    }
                }
            }
        }
    }
    // after `break(4)`, we came here with fruit="grape"
}
```

:::warning 注意
如果 break 传入数字超过循环嵌套层级，那么将无法通过编译
:::
::::

### 迭代