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

  如果需要使用 index
  ```collie
  for (item, index : ArrayOrList) {}
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

  如果需要使用 index
  ```collie
  for (key, value, index : map) {}
  ```

  :::warning 注意！没有这种写法：
  ```collie title="❌ 错误示范"
  for (entry, index : map) {} // entry 和 index 实际为 key 和 value
  ```
  :::

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



#### 嵌套循环跳过 / 跳出指定循环

可通过 label 指定跳出 / 跳过的循环，参考[嵌套循环跳出指定循环(本页)](#label-break-out-of-nested-loops) 和 [嵌套循环跳过指定循环(本页)](#label-break-out-of-nested-loops)



#### 跳过相邻多次循环【TODO】

:::danger TODO
似乎这个特性容易让人误解，需要考虑是否保留
:::

:::tip
如果需要跳过相邻多次的循环，可以使用 `continue(integer);` 语句。传入 continue 的值必须是正整数（大于0的整数）。

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

也可以在每次循环中，动态指定跳过的次数

```collie
[list] list = [ 1, 2, 4, 8, 16, 32, 64, 128, 356, 512, 1024, 2048 ]
for (item, index : list) {
    log('item: {item}, index: {index}')
    continue(index + 1);
}
// result:
// item: 1, index: 0
// TODO
```
:::

### 迭代

## label 语句

与 JavaScript, Java 等语言类似，Collie 语言支持通过 label 进行逻辑跳转。

:::note 查阅其它语言的 Label 语法
- [JavaScript](https://developer.mozilla.org/zh-CN/docs/Web/JavaScript/Guide/Loops_and_iteration#label_%E8%AF%AD%E5%8F%A5)
- [Java](https://docs.oracle.com/javase/specs/jls/se24/html/jls-14.html#d5e25652)
:::

##### 跳出嵌套代码块

```collie
function foo() {
    logic1: {
        // some code here
        logic2: {
            // some code here
            if (condition) {
                break logic1;
            }
        }
    }
    // after `break logic1`, we came here
}
```

##### 嵌套循环跳出指定循环 {#label-break-out-of-nested-loops}

如果需要连续跳出多次循环，可以使用 `break label;` 语句。

```collie
// 定义5个不同类别的水果列表
const colors = ['red', 'green', 'yellow'];
const fruits = ['apple', 'grape', 'banana'];
const sizes = ['small', 'medium', 'large'];
const origins = ['domestic', 'imported'];
const qualities = ['A', 'B', 'C'];

// 5层嵌套循环
for (const color of colors) {
    fruits_loop: for (const fruit of fruits) {
        for (const size of sizes) {
            for (const origin of origins) {
                for (const quality of qualities) {
                    log(`A ${color} ${size} ${fruit} from ${origin} origin, quality ${quality}`);

                    if (fruit == "grape") {
                        log("Oh! I hate $fruit$!")
                        // When fruit is "grape", we break out of the loop to `fruits_loop`
                        break fruits_loop;
                    }
                }
            }
        }
    }
    // after `break fruits_loop`, we came here with fruit="grape"
}
```

##### 嵌套循环跳出指定循环 {#label-skip-loop-in-nested-loops}

与跳出类似，跳过使用 continue关键字：

```collie
outer_loop: for (x : 10) {
    for (y : 10) {
        if (x + y > 12) {
            continue outer_loop;
        }
    }
}
```
