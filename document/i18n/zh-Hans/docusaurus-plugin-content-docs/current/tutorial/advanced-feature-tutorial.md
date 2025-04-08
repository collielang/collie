---
sidebar_position: 4
sidebar_label: 高级特性教程
---

# 高级特性教程

:::danger TODO
以下文档由 AI 生成，仅供参考，需要重写
:::

## 泛型

泛型允许你编写可重用的代码，而不需要指定具体的数据类型：

```collie
function printArray<T>(arr: Array<T>) {
    for (var i = 0; i < arr.length; i++) {
        println(arr[i]);
    }
}

var intArray: Array<int> = [1, 2, 3];
printArray(intArray);
```

## 多态

多态允许你以统一的方式处理不同类型的对象：

```collie
interface Shape {
    function area(): float;
}

class Circle implements Shape {
    var radius: float;

    constructor(r: float) {
        this.radius = r;
    }

    function area(): float {
        return 3.14 * this.radius * this.radius;
    }
}

class Rectangle implements Shape {
    var width: float;
    var height: float;

    constructor(w: float, h: float) {
        this.width = w;
        this.height = h;
    }

    function area(): float {
        return this.width * this.height;
    }
}

function printArea(shape: Shape) {
    println(shape.area());
}

var circle = new Circle(5);
var rectangle = new Rectangle(3, 4);

printArea(circle);
printArea(rectangle);
```
