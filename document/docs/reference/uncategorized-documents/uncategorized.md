---
sidebar_position: 99
sidebar_label: Collie 编程语言官方文档
---

# Collie 编程语言官方文档


### TODO

| 类型             | 描述                                           |
| ---------------- | ---------------------------------------------- |
| `stream`         | 自定义流类型                                   |
| `object?`        | 可为空类型                                     |
| `var`            | 类型推断，自动推断变量类型                     |
| `Proxy<object>`  | 代理对象，允许在访问或修改对象时执行自定义操作 |
| `Tuple`          | 元组类型，用于返回多个值或传递多个参数         |



##### 自定类型推断

- **var**：自动推断类型

---

## 语法设计

### 空值（Nullable Value）

| 类型      | 描述               |
| --------- | ------------------ |
| `object?` | 可以为空的对象类型 |

#### 🏅语法示例

- 示例：

```collie
number? nullableNumber = null;
nullableNumber.isNull(); // result: true
nullableNumber.isNotNull(); // result: false

number result1 = nullableNumber + 1; // ❌ 无法通过编译：number 类型不可保存 null
number? result2 = nullableNumber + 1; // result: null

number? nullableNumber = null;
nullableNumber + 1; // result: 4

```



### 常量（Constant）

**const**：静态类型常量。一旦定义，不能再修改其值。

```collie
const number MAX_SIZE = 100;
const string NAME = "Alice";
```



### 代理对象（Proxy object）

代理对象：用于在访问或修改对象值时执行自定义操作

```collie
number value = 3.1415926;

number proxyNum1 = proxy(value);
proxyNum.isProxy(); // result: true

number proxyNum2 = proxy(proxyNum1);
proxyNum2.beforeGet(void (rawObject) {
	return rawObject++;
}); // 此处仅做语法示例，实际使用中，我们一般不这样做（容易引起歧义）

print(proxyNum1); // result: 3.1415926

print(proxyNum2); // result: 4.1415926
print(proxyNum2); // result: 5.1415926

print(proxyNum1); // result: 5.1415926

value.valueEquals(proxyNum1)
```



## 语法糖

#### 交换变量

- 支持一行交换多个变量的值

```collie
int a = 0, b = 0;
a, b = b, a;
```

#### 异名函数

- 支持通过函数名与参数名组合来定义异名函数

```collie
public number add(int delta1)AndThenMinus(int delta2) {
   return this.num + delta1 - delta2;
}

NumberObject no = new NumberObject();
no.add(10)AndThenMinus(20);
```

#### 创建对象

- 通过 `new` 关键字创建类的实例

```collie
Animal a = new Animal();
```

### 类定义

- 类支持方法和多返回值，且方法返回值可以为元组

```collie
class <Class Name> {
   <Access Specifier> <Return Type1>,<Return Type2> <Method Name>(Parameter List)
   {
         Method Body
         return <Object Type1>, <Object Type2>;
   }
}
```

### 枚举

- 定义枚举类型

```collie
enum <Enum Name> {
   <Value1>, <Value2>, <Value3>
}
```

### 注解
- 支持常见的注解，如：
  - `@deprecated`
  - `@override`

---

## 控制结构

### 运算符
- 基本运算符：`+ - * / %`
- 增减运算符：`i++`, `i--`, `++i`, `--i`
- 复合运算符：`i+=1`, `i*=1`, `i/=1`, `i%=1`
- 比较运算符：`==`, `!=`, `>`, `<`, `>=`, `<=`
- 逻辑运算符：`!`, `&&`, `||`

- 多目运算符：

```collie
tribool a;
// 此处省略给 a 变量赋值的相关逻辑

a ==? unset: 1, true: 2, false: 3
// when a equals unset, result: 1
// when a equals true,  result: 2
// when a equals false, result: 3

a ==? unset, true: 2, false: 3
// when a equals true or unset,  result: 2
// when a equals false,          result: 3

a ==? unset, true: 1, 2
// when a equals unset or true, result: 1
// otherwise (a equals false),  result: 2

a ==? unset: 1, 2
// when a equals unset,         result: 1
// when a equals false or true, result: 2

a ==? 2, unset: 1
// when a equals unset,                result: 1
// otherwise (a equals false or true), result: 2

// a ==? unset, true: 2
// ❌ 不允许的写法：缺少 false 分支

/* 如果表达式较长，推荐的格式如下
// 注意，value1, value3 如果都与 hereIsAVeryLongParamName 相等，则会返回第一个匹配上的条件对应结果 (expression 1)
object a = hereIsAVeryLongParamName ==?
                value1, value2: {
                    expression 1
                },
                value3: {
                    expression 2
                },
                expression 3
 */
```

- 多目运算符简化形式：

```collie
tribool a;
// 此处省略给 a 变量赋值的相关逻辑

a ? 1 : 2 : 3
// when a equals true,  result: 1
// when a equals false, result: 2
// when a equals unset, result: 3

a ? 1 : 2
// when a equals true,           result: 1
// when a equals false or unset, result: 2

/* 如果表达式较长，推荐的格式如下
object a = hereIsAVeryLongParamName
               ? expression 1
               : expression 2
               [: expression 3]
 */
```

### 条件语句

#### if

```collie
if (条件1 && (条件2 || 条件3)) {
    // 执行代码块 1
} else if (条件2) {
    // 执行代码块 2
} else {
    // 执行代码块 3
}
```

#### switch

?> 与其它语言不同的是，collie 语言 switch 不需要 case 和 break关键字，每个代码块执行完毕后会直接走出 switch 块，不会进入下一个 case/

- **switch** 语句
  ```collie
  switch (表达式) {
      值1 {
          // 执行代码块 1
          // 不需要 break
      }
      值2 {
          // 执行代码块 2
      }
      值3, 值4 {
          // 执行代码块 2
      }
      null {
          // 执行代码块 2
      }
      default {
          // 执行代码块 3
      }
  }
  ```


### 类型判断
```collie
if (对象 instanceof 类型) {
    // 执行代码块
}
if (对象 is 类型) {
    // 执行代码块
}
```

### 错误处理
- **传统 try-catch**
  ```collie
  try {
      // 执行代码块
  } catch (Exception e) {
      // 处理异常
  } finally {
      // 执行清理代码
  }
  ```

- **try-with-resources**
  ```collie
  try (try-with-resources) {
      // 执行代码块
  } catch (Exception e) {
      // 处理异常
  } finally {
      // 执行清理代码
  }
  ```

### for-else
- 在每个循环结束时，如果没有被 `break` 中断，则执行 `else` 代码块。

---

## 常用模块

### 基础模块
- **Date**：日期
- **Time**：时间
- **TimeSpan**：时间间隔
- **Timestamp**：时间戳

### 金融模块
- **Money**：财务计算和货币处理

---

## 附录

### 示例代码

```collie
class Animal {
    public string name;
    public int age;
}

class Dog extends Animal {
    public void bark() {
        print("Woof");
    }
}

Dog dog = new Dog();
dog.bark();  // 输出 "Woof"
```

### 示例代码（继续）

```collie
// 创建一个包含多个元素的 List
List<object> list = new List<object>();
list.add(10);
list.add(20);
list.add(30);

// 使用 for-each 循环遍历 List
for (item in list) {
    print(item); // 输出 10, 20, 30
}

// 定义一个 Map 并遍历
Map<string, object> map = new Map<string, object>();
map.put("name", "Alice");
map.put("age", 18);

for (key, value in map) {
    print(key + ": " + value); // 输出 "name: Alice" 和 "age: 18"
}

// 使用三元运算符
int age = 18;
string result = age >= 18 ? "Adult" : "Minor";
print(result);  // 输出 "Adult"

// 使用元组返回多个值
public Tuple getPersonInfo() {
    return ("Alice", 18);
}

Tuple info = getPersonInfo();
string name = info.name;
int personAge = info.age;
print(name + " is " + personAge + " years old.");  // 输出 "Alice is 18 years old."
```

### 错误处理与异常捕获

```collie
// 使用 try-catch 处理异常
try {
    int x = 10 / 0;  // 会抛出异常
} catch (Exception e) {
    print("Error: " + e.message);  // 输出 "Error: Division by zero"
} finally {
    print("Cleanup completed");  // 输出 "Cleanup completed"
}

// 使用 try-with-resources
try (stream = openFile("file.txt")) {
    // 使用文件流处理文件
    string content = stream.readAll();
    print(content);
} catch (Exception e) {
    print("Error while reading file: " + e.message);
} finally {
    print("File processing finished");
}
```

### 类与继承

```collie
// 定义父类 Animal
class Animal {
    public string name;
    public int age;

    public Animal(string name, int age) {
        this.name = name;
        this.age = age;
    }

    public void speak() {
        print(name + " makes a sound.");
    }
}

// 定义子类 Dog，继承自 Animal
class Dog extends Animal {
    public Dog(string name, int age) : base(name, age) {}

    public void bark() {
        print(name + " barks!");
    }
}

// 创建对象并调用方法
Dog dog = new Dog("Buddy", 3);
dog.speak();  // 输出 "Buddy makes a sound."
dog.bark();   // 输出 "Buddy barks!"
```

### 元组与交换变量

```collie
// 使用元组返回多个值
public Tuple getCoordinates() {
    return (10, 20);
}

Tuple coords = getCoordinates();
int x = coords.Item1;
int y = coords.Item2;
print("X: " + x + ", Y: " + y);  // 输出 "X: 10, Y: 20"

// 交换变量值
int a = 5, b = 10;
a, b = b, a;
print("a: " + a + ", b: " + b);  // 输出 "a: 10, b: 5"
```

### 集合与字典

```collie
// 创建一个 List 并添加元素
List<object> numbers = new List<object>();
numbers.add(1);
numbers.add(2);
numbers.add(3);

// 使用 last() 方法获取最后一个元素
print(numbers.last());  // 输出 3

// 创建一个 Map，并添加键值对
Map<string, object> person = new Map<string, object>();
person.put("name", "Alice");
person.put("age", 25);

// 使用 for-each 遍历 Map
for (key, value in person) {
    print(key + ": " + value);  // 输出 "name: Alice" 和 "age: 25"
}

// 转换 List 为 Map
List<object> list = new List<object>();
list.add("name");
list.add("Alice");
list.add("age");
list.add(25);

Map<string, object> mapped = list.toMap();
for (key, value in mapped) {
    print(key + ": " + value);  // 输出 "name: Alice" 和 "age: 25"
}
```

### 类型判断与模式匹配

```collie
// 类型判断使用 instanceof 或 is
class Animal {}
class Dog extends Animal {}

Dog dog = new Dog();
if (dog instanceof Animal) {
    print("dog is an Animal");  // 输出 "dog is an Animal"
}

if (dog is Dog) {
    print("dog is a Dog");  // 输出 "dog is a Dog"
}

if (dog is Cat) {
    print("dog is a Cat");  // 不会执行
}
```

### 注解使用

```collie
// 使用 @deprecated 标记一个已废弃的方法
@deprecated
public void oldMethod() {
    // 旧方法实现
}

// 使用 @override 标记重写父类方法
@override
public void speak() {
    print("Dog is barking!");
}
```

### 交换变量和异名函数的使用

```collie
// 定义一个异名函数
public number add(int delta1)AndThenMinus(int delta2) {
    return this.num + delta1 - delta2;
}

NumberObject no = new NumberObject();
no.add(10)AndThenMinus(20);  // 执行 add 和 AndThenMinus 方法

// 使用异名函数参数形式
public number sum(...int num1, int lastNum)AndThenMinus(int delta) {
    return this.num + num1.sum() + lastNum - delta;
}

NumberObject no2 = new NumberObject();
no2.add(10, 11, 12)AndThenMinus(20);  // 执行 sum 和 AndThenMinus 方法
```

### 字典与集合操作

```collie
// 创建 Map 对象
Map<string, object> employee = new Map<string, object>();
employee.put("name", "Alice");
employee.put("position", "Engineer");

// 获取 Map 的所有键值对
for (key, value in employee) {
    print(key + ": " + value);  // 输出 "name: Alice" 和 "position: Engineer"
}
```

---

## 常用模块

Collie 提供了基础与金融模块，以满足常见应用场景。

### 基础模块
- **Date**：用于日期操作，支持常见的日期格式和计算。
- **Time**：用于时间处理，支持时分秒操作。
- **TimeSpan**：表示时间间隔，支持加减操作。
- **Timestamp**：表示时间戳，支持与 Date、Time 的转换。

### 金融模块
- **Money**：用于财务计算，支持货币的加减、汇率转换等操作。

---

## 附加功能

### 自定义类型与类型推断

```collie
// 使用 var 进行类型推断
var number = 10;  // 编译时推断为 int 类型
var name = "Alice";  // 编译时推断为 string 类型
```

### 代理对象

```collie
// 创建代理对象，允许在访问或修改对象时自定义行为
Proxy<Animal> proxyAnimal = new Proxy<Animal>(new Animal("Dog", 5));
proxyAnimal.someMethod = (object) => {
    print("Intercepting method call");
    return "Modified";
};
```

---

## 总结

Collie 编程语言设计简洁，易于使用，同时具备强大的类型系统与高效的运行性能。其语法灵活且易于扩展，支持面向对象编程、元组、异名函数、注解等先进特性，旨在提升开发效率并保证代码的清晰与可维护性。

