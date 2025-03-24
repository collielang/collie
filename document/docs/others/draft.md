
```collie
class AppleTree {

    int count;

    // export 类似其他语言的 public 函数
    export function getAppleCount() : int {
        return this.count
    }

    export function getOne() {
        function innerFunc(refrence int count) {
            count -= 1
        }
        return innerFunc(count)
    }
}
```

```collie
class Sutdent {
    val name: string
    const sex: '男' | '女' // 这里的枚举需要是同一类型，比如这样就不行：'man' | 18
}
```

----



常量：Math.PI
Math.e
黄金分割比

科学计数法

// 整除
% 取余

向量运算

为了清晰，函数定义中间如果是 xxx(xx,xx)xx(xx)的形式，参数不允许在这里写表达式

获取对象类型
Objects.instance()
Objects.type(123) // 'number'

对象深拷贝 deepCopy ，对象浅拷贝 shallowCopy

== 强类型比较
~= 弱类型比较

a.popFirst()
a.popLast()
a.popFromIndex(index)

a.addToFirst()
a.addToLast()
a.addToIndex(index)

解构赋值
```collie
const [a,b,c] =[1,2,3]
```

未初始化 unset (需要跟三态布尔类型区分一下)

注释到文件末尾
/...

从此行之前全部都是注释
.../

位运算
```collie
<<
>>
&
^
|
```

改写赋值：`+= -= *= /= %= &= |= ^= <<= >>=`

运算符优先级

字符串插值

```collie
[1, 2, 3][-1] == 3
[1, 2, 3][-4] // ❌ 数组越界

a = [1, 2, 3]
b = a[:]   // result: [1, 2, 3] 数组浅拷贝
c = [...a] // result: [1, 2, 3] 数组浅拷贝
a[1:-1] == [2]
a += [4, 5] // result: [1, 2, 3, 4, 5]

[].slice
[].flat
```

listToMap
mapToList

sort

Lambda 匿名函数

泛型 （类似 Java 的 `ArrayList<String>`）

模块化导入 import

异步操作 / 同步操作

抛出异常 `throw XxxError("");`

```collie
if (1 <= i < 4) {} // 等价于: i >=1 && i < 4
```

```collie
match(value) {
   >= 1 && <= 4 { // 等价于: value >=1 && value <= 4

   }
   otherwise {

   }
}
```

获取系统全局变量

```collie
定义变量
number a, b = 1, 2;
number a, number b = 1, 2;
bool a, number b = false, 2;
交换变量
a, b = b, a
a, b, c = c, a, b

a, b, a = b, a, b
// 本质上是依次记录右侧的引用，然后将左侧的变量引用依次按照右侧列表进行更新

a, b = b,a,c // ❌ 不允许的写法，参数个数不对应

a, b = a, b+1 // 本质上是先计算出 b+1， 然后再变成引用赋值

可以简单的想象成，先将右侧表达式求值组成一个临时的元组，然后解构，逐一对左侧变量进行赋值

数组解包
a = [1, 2, 3]
b = [...a]
```

函数注释

```collie
[].concat([])
```

图像处理：BitMap

颜色：Color

系统IO：读写文件，等等

栈（stack）、队列（queue）、列表（list）和哈希表（hash table）的支持
(参考C#)

数组链式调用 .distinct()

断点调试

collie：传参，数组定义支持最后一个元素后面带逗号

遍历、迭代器中删除元素（或插入元素）

# CollieLang 语法初稿

请帮我将以下信息整理成 Collie 编程语言的官方文档，需要保留所有信息，不得遗漏或者擅自添加，同时需要按照规范的结构进行整理：

我希望自己开发一个编程语言，
我设计的语法如下：
我希望我设计出的语言不用像java那样导出判断空指针
我不确定三元：triple的设计是否是好的设计。
我不确定数字类型是否需要这么多，我有希望能保留运算效率，有希望能够不要增加太多类型以加重程序员负担。
我希望设计成像java那样的先编译后运行的语言，支持注解。
对于可能丢失精度的情况，不支持不同数字类型的隐式转换，必须显式转换。
不区分数组Array和列表List, 可以在 List 初始化函数通过构造函数进行区分。

```
整数：short(16位整数), int(32位整数), long(64位整数) => 负整数: -short 正整数: +short
小数：float(32位浮动小数), double(64位浮动小数), decimal(高精度小数)
数字：number(支持保存整数和小数，方便使用，但是运算效率不如整数和小数)
字符：char(字符), string(字符串)  => string 等价于 char[] 等价于 [char]
布尔：bool(true, false; default: false)
三元：triple(true, false, unset; default: unset)
位：byte(1位正整数0-255), stream()
父类型：object
数组：[object](1维数组), [[object]](2维数组) 等价于 object[](1维数组), object[][](2维数组)
List<object> array = new List<object>();
array.add(1);
array.add(2);
```

可以为空的类型：object?

自定类型推断：var

代理对象：`Proxy<object>` (代理对象可以在访问对象，修改对象中值的时候，做一些自定义操作)

数组：`List<object>`, `Set<object>` => 父类为 `Collection<object>`
字典：`Map<object>`
提供：取最后一个元素的方法：last()，提供List->Map转换方法toMap()

元组：主要用于函数返回，或者函数调用的时候，需要将多个变量打包传递的情况
```collie
Tuple a = (name: "Alice", age: 18);
string name = a.name;
int age = a.age;
string name, int age = a.get();

交换变量
int a = 0, b =0;
a,b = b,a;

静态类型：
const int MAX_SIZE = 100;
const string NAME = "Alice";
...
```

函数：
```collie
<Access Specifier> <Return Type1>,<Return Type2> <Method Name>(Parameter List)
{
   Method Body
   return <Object Type1>, <Object Type2>;
}
```
多返回类型函数，编译时隐式转换为元组的方式进行返回

函数也可以像js那样当作变量进行传递（像js那样）

例如：
```collie
public int,string getAge() {
   return 18, "Alice";
}
```

可以像类似 python 那样，通过元组调用函数
```collie
private void fooBar(int? age, string? name) {
    // ...
}
fooBar(18, "Alice"); 或 fooBar(18, null); 或 fooBar(null, "Alice");
fooBar(age: 18, name: "Alice"); 或 fooBar(name: "Alice", age: 18); 或 fooBar(age: 18); 或 fooBar(name: "Alice");
```

异名函数：
```collie
public number add(int delta1)AndThenMinus(int delta2) {
   return this.num + delta1 - delta2;
}
```
调用时，
NumberObject no = new NumberObject();
no.add(10)AndThenMinus(20);
编译时优化为：`add{int[1]}AndThenMinus{int[1]}(int delta1, int delta2)`

```collie
public number sum(...int num1, int lastNum)AndThenMinus(int delta) {
    // num1 为 int[]
   return this.num + delta1.sum() + lastNum - delta;
}
```
调用时，
```collie
NumberObject no = new NumberObject();
no.add(10, 11, 12)AndThenMinus(20);
```
编译时优化为：`add{int[n],int[1]}AndThenMinus{int[1]}(int delta1, int delta2)`

创建对象：
Animal a = new Animal();

类：
```collie
class <Class Name> {
   <Access Specifier> <Return Type1>,<Return Type2> <Method Name>(Parameter List)
   {
      Method Body
      return <Object Type1>, <Object Type2>;
   }
}
```

枚举：
```collie
enum <Enum Name> {
   <Value1>, <Value2>, <Value3>
}
```

注解：
@deprecated
@override

运算符：
```
+ - * / %
i++ i-- ++i --i
i+=1 i*=1 i/=1 i%=1
== != > < >= <= !
&& ||
```

三目运算符：condition ? value1 : value2

条件判断：

```collie
if (条件1 && (条件2 || 条件3)) {
   // 执行代码块 1
} else if (条件2) {
   // 执行代码块 2
} else {
   // 执行代码块 3
}
```

循环：

```collie
for (初始化; 条件; 更新) {
   // 执行代码块
}

for (item : ArrayOrList) { // 或 for (item in ArrayOrList)
   // 执行代码块
}
for (key, value : map) {
   // 执行代码块
}
for (entry : map) {
   var key = entry.key;
   var value = entry.value;
}

while (条件) {
   // 执行代码块
}

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

do {
   // 执行代码块
} while (条件)

类型判断：

if (对象,对象 instanceof 类型) {
   // 执行代码块
}
if (对象 is 类型) {
   // 执行代码块
}
```

举例：
class Animal
class Dog extends Animal
class Cat extends Animal

Dog dog = new Dog();
Cat cat = new Cat();

dog installof Animal; // true
dog installof Dog; // true
dog installof Cat; // false
cat installof Animal; // true
cat installof Dog; // false
dog,cat installof Dog; // false
dog,cat installof Animal; // true
dog,cat installof object; // true

错误处理：
try {
   // 执行代码块
} catch (Exception e) {
   // 处理异常
} finally {
   // 执行清理代码
}

try (try-with-resources) {
   // 执行代码块
} catch (Exception e) {
   // 处理异常
} finally {
   // 执行清理代码
}


for-else：对于每一个循环，如果没有被 break 中断，则执行 else 代码块。
for-in：用于遍历数组、集合、字典等可迭代对象。

```collie
List<object> array = new List<object>();
for (item in array) {
   // 执行代码块
}
```

```collie
Map<string, object> array = new Map<string, object>();
for (item in array) {
   // 执行代码块
}
```

# 常用模块

标准库：开发一个标准库，提供常用的函数和数据结构。

基础
Date -> 日期
Time -> 时间
TimeSpan -> 时间间隔
Timestamp -> 时间戳

金融
Money -> 财务


我遇到一些问题，不知道
1. byte是1位（比如 0xAB），那双字（比如 0xABCD）、四字（比如 0xABCDEFGH）怎么表示，byte[] 用 stream 是否合适？
2. 我这样设计是否会太复杂了，对于编译过程来说的话，是否好实现？
3. 数字类型同时设计了基础类型和number类型，number类型是为了方便使用，但是还可能出现类型转换的问题，我不确定这样设计是否是好的设计。
4. 函数支持多返回是一个特性，但是对于运行时来说，是否会有性能问题？语法编译器角度来看，是否会有问题？
5. 对于AOT的语言来说，函数也可以像js那样当作变量进行传递（像js那样）的设计，是否能够实现？（或者还是说，只能像C#那样通过代理的方式来实现，但是我觉得c#的代理写起来有些麻烦）
错误处理：添加错误处理机制，以便在程序出现错误时能够提供有用的反馈。
6. 标准库是否还有更多的模块？
7. 我想实现 num.add(123)AndThen(123) 这样异名函数 public number add()AndThen() {}，是否好实现呢。


## 下一步计划

8. 三方依赖：Java有Maven, JS 有 npm, Python 有 pip, 我希望设计出来的语言也有类似的功能。
面向对象编程：引入类、对象、继承等面向对象编程的概念。
模块和包：支持将代码组织成模块和包，以便更好地管理和复用代码。


编译器或解释器：根据语言的设计选择合适的实现方式，编写编译器或解释器。






# 语法

## 变量声明与赋值

使用 var 关键字声明变量。

# 关键字

关键字和标识符

# 语法

## 语句

必须以 ; 结尾。

## 变量声明与赋值

使用 var 关键字声明变量。
变量名可以包含字母、数字和下划线，但不能以数字开头。
使用 = 进行赋值。

示例：

```co
```

## 数据类型

基本数据类型包括：

整数：int, long
小数：decimal, float, double      浮点数
字符：char(字符), string(字符串)
布尔：bool(true, false)
三元：triple(true, false, unset)

包装对象：
Number：自适应整数和小数，可进行数学运算     整数精度问题，小数精度问题


示例：

```co
var age = 25; // int
var price = 19.99; // float
var name = "Alice"; // string
var isStudent = true; // bool
```

## 算术运算

支持加法（+）、减法（-）、乘法（*）、除法（/）和取模（%）。

示例：

```co
var result = x + y; // 30
var quotient = y / x; // 2
var remainder = y % x; // 0
```

## 控制流

支持 if-else 语句进行条件判断。
支持 while 循环进行重复执行。

示例：

```co
if (x > y) {
    print("x is greater than y");
} else {
    print("y is greater than or equal to x");
}

var i = 0;
while (i < 5) {
    print(i);
    i = i + 1;
}
```

## 函数定义与调用

使用 function 关键字定义函数。
函数可以有参数和返回值。

示例：

```co
function add(a, b) {
    return a + b;
}

var sum = add(x, y); // 30
```

## 输入输出

使用 print 函数进行输出。
使用 input 函数进行输入。

示例：

```co
var name = input("What is your name? ");
print("Hello, " + name);
```

# 常用模块

基础
Date -> 日期
Time -> 时间
TimeSpan -> 时间间隔
Timestamp -> 时间戳

金融
Money -> 财务

## 下一步计划

这只是一个非常简单的初稿，你可以根据自己的需求进一步扩展和完善这个语言。以下是一些可能的下一步计划：

错误处理：添加错误处理机制，以便在程序出现错误时能够提供有用的反馈。
数据结构：支持数组、列表、字典等更复杂的数据结构。
面向对象编程：引入类、对象、继承等面向对象编程的概念。
模块和包：支持将代码组织成模块和包，以便更好地管理和复用代码。
标准库：开发一个标准库，提供常用的函数和数据结构。
编译器或解释器：根据语言的设计选择合适的实现方式，编写编译器或解释器。
希望这个初稿对你有所帮助！如果你有任何具体的问题或需要进一步的帮助，请随时告诉我。