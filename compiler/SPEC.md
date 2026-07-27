# Collie 语言规范草稿（以实际实现为准）

> **地位与目的**：本文档是 Collie 语言的**实现规范草稿**（M5/t46），内容以 `compiler/` 目录下
> 编译器（Lexer → Parser → Semantic → 树遍历解释器）的**实际行为**为准，逐条经代码核实。
> `document/docs/` 下的设计文档描述的是**设计愿景**，与本文冲突时，本文反映当前实现现状，
> 设计文档反映目标方向；差距登记在文末[「已知实现缺口」](#十已知实现缺口)。
>
> 版本基线：2026-07-26（t45 tuple 闭环后）。随实现演进持续更新。

---

## 一、源文件与词法结构

### 1.1 源文件

- 后缀：主 `.collie`，别名 `.col`（D8）。
- 编码：**UTF-8 字节流**（D3）。文件开头的 UTF-8 BOM（`EF BB BF`）会被自动剥离。
- 标识符：以 ASCII 字母、`_` 或任意非 ASCII UTF-8 码点开头（**支持中文等 Unicode 标识符**），
  后续字符同前并可含数字。

### 1.2 注释

| 形式 | 语法 | 说明 |
|------|------|------|
| 单行注释 | `// ...` | 到行尾 |
| 块注释 | `/* ... */` | **支持嵌套**（嵌套计数器实现） |

### 1.3 关键字

lexer 实际注册的关键字（区分大小写）：

- **类型**：`object` `none` `char` `character` `string` `number` `integer` `decimal`
  `bool` `tribool` `bit` `byte` `word` `dword` `array` `Tuple`（注意大写 T，见 03-tuple.md）
- **控制流**：`if` `else` `switch` `default` `for` `while` `do` `break` `continue`
- **声明与 OOP**：`class` `const` `public` `private` `protected` `function` `return`
  `void` `new` `this` `extends` `base`
- **字面量**：`null` `true` `false` `unset`
- **特殊数值**：`Infinity` `NaN`（词法层直接归为数字字面量 token，大小写敏感）

保留但未启用：`var`（枚举 `KW_VAR` 存在，keywords 表未注册，写 `var` 会被当作普通标识符）。

### 1.4 运算符与分隔符

词法层识别的全部符号：

- 算术：`+ - * / %`；复合赋值：`+= -= *= /= %=`
- 比较：`== != < <= > >=`
- 逻辑：`&& || !`
- 位运算（t47 起完整支持）：`& | ^ ~ << >>`
- 特殊：`= ? : ==?`（多路匹配）
- 分隔符：`( ) [ ] { } , ; .`
- 前缀符号：`@"..."` 开启插值字符串；`@名字` 产生注解 token（如 `@override`）

---

## 二、字面量

### 2.1 数字字面量

统一为 `LITERAL_NUMBER` token，**由 lexeme 是否含 `.`/`e`/`f` 决定是整数还是小数字面量**（t42）：

| 形式 | 示例 | 类别 |
|------|------|------|
| 整数 | `42` | integer |
| 小数 | `3.14`（点后必须有数字） | decimal |
| 前导点小数 | `.5` | decimal |
| 科学计数法 | `1e3`、`2.5E-2`（e 后可带符号，必须跟数字） | decimal |
| f 后缀 | `2f`（与 `2.0` 等价；f 后是标识符字符则不视为后缀） | decimal |
| 特殊值 | `Infinity`、`NaN`（大小写敏感） | decimal |
| 十六进制 | `0xFF`、`0X1f`（t47；lexeme 保留前缀原文，含 E/f 十六进制位不被误判小数；不支持小数/科学计数法/f 后缀；`0x` 后缺十六进制位报错） | integer |

**不支持** `0b`/`0o` 进制前缀、无数字分隔符 `_`。

### 2.2 字符串字面量

- **单行字符串** `"..."`：转义序列仅 5 种——`\"` `\\` `\n` `\t` `\r`，其余转义报
  `Invalid escape sequence`。多字节 UTF-8 码点校验后原样保留。
- **多行字符串** `"""..."""`：开头三引号后紧跟的换行不计入内容；**以首行缩进为基准剥离
  各行公共前导缩进**（非空行缩进小于基准报错）；每行末尾补 `\n`。
- **插值字符串** `@"...{expr}..."`：不允许跨行；`{expr}` 段由 parser 脱糖为
  `"前段" + toString(expr) + "后段"` 的拼接表达式（AST/语义/解释器无插值概念）；
  `\{` `\}` 表示字面花括号；插值段内可再写字符串字面量。

### 2.3 字符字面量

`'x'` 单引号包一个字符：ASCII 单字节产出 `LITERAL_CHAR`，多字节 UTF-8 码点（如 `'中'`）
产出 `LITERAL_CHARACTER`。转义同字符串的 5 种（`\'` 替代 `\"`）。
t47 起 char 字面量可入表达式（推导为 char 类型，可参与比较）；但运行期 Value 层
仍无专属表示（落为 string），见缺口 G2。

### 2.4 其他字面量

| 字面量 | 类型 |
|--------|------|
| `true` / `false` | bool |
| `unset` | tribool（三态中的「未设置」） |
| `[e1, e2, ...]`（允许尾逗号） | array |
| `(e1, e2)` / `(name: e1, age: e2)` / `()` | Tuple（见 §5.8） |
| `null` | 关键字已注册，暂无求值语义（见缺口 G8） |

---

## 三、类型系统

### 3.1 类型全集与可用位置

语义层认可的类型（`is_valid_type`）：`number` `integer` `decimal` `string` `bool` `tribool`
`char` `character` `bit` `byte` `word` `dword` `none` `object` `array` `Tuple`，
以及用户定义的**类名**（作变量类型时按 object 动态处理）。

**位置差异**（实现缺口 G3）：变量声明可用上述全部类型；但**函数形参与返回类型**
（`consume_type_token`）目前只接受 `number` `string` `bool` `none` `void` `char` `character`
`byte` `word` `object` `array` `Tuple` 与标识符——**缺 `integer` `decimal` `tribool` `bit` `dword`**。

### 3.2 核心类型语义

| 类型 | 语义 |
|------|------|
| `number` | 数值超类型，可持有整数或小数表示 |
| `integer` | **任意精度整数**（BigInt，base 2³²，自动扩容不溢出，Python 式，t42） |
| `decimal` | IEEE 754 double 小数 |
| `bool` | 二值布尔 |
| `tribool` | 三态布尔：`true` / `false` / `unset`（Kleene 三值逻辑，t43） |
| `string` | 不可变 UTF-8 字符串，索引/长度按**码点**计 |
| `array` | 动态数组，**引用语义**（赋值/传参共享底层存储），元素类型不追踪（动态） |
| `Tuple` | **不可变**元组，引用语义，支持无名/命名元素混合（t45） |
| `object` | 动态类型：语义层双向放行，运行期不校验 |
| `none` / `void` | 空类型 / 无返回值（函数返回类型用） |
| 类名 | `new` 出的实例，引用语义（t34） |

### 3.3 编译期隐式转换（`can_implicit_convert`）

- 同类型恒可转；`object` 与任意类型**双向**放行（运行期再查）。
- 数值：`byte→number`、`word→number`、`integer→number`、`decimal→number`、
  `integer→decimal`、`number→decimal`、`number→integer`（运行期窄化校验兜底）、
  `byte→word`、`integer→byte/word`（t47：整数精确可窄化，运行期范围校验
  byte 0-255、word 0-65535；number/decimal 不可隐式窄化到位类型）。
- 字符：`char→character`、`char→string`、`character→string`。
- 布尔：`bool→tribool` 单向加宽（反向拒绝）。
- 任何可字符串化类型 `→string`。

二元运算的公共类型（`common_type`）取宽规则：`number > decimal > integer > word > byte`；
`integer` 与 `decimal` 混合得 `decimal`。

### 3.4 运行期声明类型校验（`coerce_to_declared`，t35–t37）

赋值、变量初始化、函数/方法传参、类字段写入、函数返回值**五处**统一走该校验：

| 声明类型 | 接受的运行期值 |
|----------|----------------|
| `number` | 任意数值（保持原表示） |
| `integer` | 仅整数表示；**decimal 值不可隐式窄化**（报 Type mismatch） |
| `decimal` | 任意数值；整数值加宽为小数表示 |
| `bool` | 仅 bool |
| `tribool` | tribool 或 bool（加宽）；反向不允许 |
| `array` / `Tuple` | 严格匹配 |
| `string` | string，或 number/bool（**转字符串落地**，如 `string s = 42;` 得 `"42"`） |
| `object` / 类名等 | 动态放行，不校验 |

---

## 四、表达式与运算符

### 4.1 优先级（低 → 高，递归下降层次）

1. 赋值 `=` 与复合赋值 `+= -= *= /= %=`（右结合；复合赋值脱糖为 `a = a op b`）
2. 三元 `?:`（右结合，含三分支形式）与多路匹配 `==?`（同层）
3. 逻辑或 `||`
4. 逻辑与 `&&`
5. 按位或 `|`（t47，与 C 家族一致）
6. 按位异或 `^`
7. 按位与 `&`
8. 相等 `== !=`
9. 比较 `< <= > >=`
10. 移位 `<< >>`（t47）
11. 加减 `+ -`
12. 乘除模 `* / %`
13. 一元 `- ! ~`
14. 后缀链：索引 `a[i]`、方法调用 `x.m(args)`、属性 `x.p`（可任意混合链式）
15. primary：字面量（含 char 字面量，t47）、标识符、`(expr)` 分组、元组字面量、
    `new 类名(args)`、`this`、`base.method(args)`、数组字面量

### 4.2 算术

- `+ - * %`：两操作数均为整数表示时走 **BigInt 精确路径**（自动扩容，永不溢出）；
  任一为小数则走 double 路径。
- `/`：**恒产小数**（Python 式 true division），`4 / 2` 得 `2`（小数表示，打印无小数点尾零）。
- `%`：**floor 取模**（Python 风格，结果符号与除数一致）：`-1 % 5 == 4`（t26）。
- 除零遵循 **IEEE 754**（t33）：`1/0 → +Infinity`、`-1/0 → -Infinity`、`0/0 → NaN`、
  `x % 0 → NaN`。**不报运行时错误。**
- 一元 `-`：整数表示走 BigInt 精确取负；仅接受数值。

### 4.3 字符串拼接

`+` 在**任一侧为 string** 时表示拼接，另一侧任意类型自动 `to_string`：
`"n = " + 42` 合法。其余算术运算要求两侧均为数值。

### 4.4 相等 `==` / `!=`（`values_equal`）

- tribool 参与时：仅可与 tribool/bool 比较，**三态一致才相等**（`unset` 与 `true`/`false`
  均不等）；与其他类型恒不等。
- 其余：类型（Kind）不同恒不等；`none == none` 为 true；数值双整数走 BigInt 精确比较，
  混合表示按 double 视图（`5 == 5.0` 为 true）；string 按内容；array 逐元素深比较；
  Tuple 元素**与名字表**都一致才相等（`(a: 1) == (b: 1)` 为 false）。
- function/instance 等未列类型比较结果为 false。

### 4.5 逻辑运算（Kleene 三值逻辑，t43）

三态编码 `False=0 < Unset=1 < True=2`，`&&` 取 min、`||` 取 max：

- `!unset == unset`；`unset && false == false`；`unset && true == unset`；
  `unset || true == true`；`unset || false == unset`。
- 短路保留：`&&` 左侧为确定 false、`||` 左侧为确定 true 时右侧**不求值**。
- 任一操作数为 tribool → 结果为 tribool；否则保持 bool。

### 4.6 条件门禁

`if` / `while` / `for` / `do-while` 的条件**必须是 bool**：tribool 不能直接作条件
（语义层拦截静态可知情况 + 运行期 `condition_truthy` 防御动态路径），需显式写
`t.isTrue()` / `t.isFalse()` / `t.isUnset()` 或 `t == true/false/unset`。

### 4.7 三元运算符

- 二分支：`cond ? a : b`（cond 须 bool；右结合）。
- **三分支**（t43）：`t ? a : b : c`（t 须 tribool；true→a、false→b、unset→c）。
  二分支形式下 tribool 条件的 `unset` 走 false 分支（见文档约定）。
- 分支惰性求值。

### 4.8 `==?` 多路匹配（t44）

```collie
result = x ==? 1, 2: "small", 3: "three", "other";
```

- 文法：`target ==? 候选组: 结果, 候选组: 结果, ..., [默认结果]`。
- **末尾裸表达式 = 默认分支，且只能在末尾**；其余裸值一律与后面最近的「值: 结果」归组
  （`1, 2: r` 表示 1 或 2 都命中 r）。非末尾未归组裸值是语法错误。
- 语义约束：候选值须与目标可 `==` 比较（object 动态放行）；**tribool 目标无默认分支时
  必须字面量穷尽三态**；其他类型必须有默认分支；结果类型取首分支，各分支须兼容。
- 求值：按书写序比较候选（`values_equal`），命中第一个匹配分支即返回；
  未命中的分支结果与剩余候选**均不求值**。

### 4.9 索引 `a[i]`

- 适用：array（读写）、string（只读，按 UTF-8 码点返回单字符子串）、Tuple（只读）。
- **负索引**：`-1` 为最后一个元素（Python 风格）；越界报运行时错误。
- 赋值：仅 array 可 `a[i] = v`；string / Tuple 不可变，索引赋值在语义层与运行期双拦截。

### 4.10 位运算（t47）

- **类型规则**（语义层）：`& | ^` 两侧须为位类型（`is_bit_type`：byte/word/integer，
  hex 字面量推导为 integer）且兼容，结果为公共类型；`<< >>` 左操作数须位类型、
  右操作数须数值，结果保持左操作数类型；`~` 操作数须位类型，结果保持原类型。
- **求值**（解释器）：`~` 走 BigInt 精确取反（`~x = -x-1`，任意精度）；
  `& | ^ << >>` 要求两侧均为整数值，在 **int64 域**求值（超 64 位报运行时错误
  不静默截断；移位数限 0-63，越界报错；`>>` 为算术右移）。
- **位类型运行期范围**：赋给 `byte`/`word` 声明时由 `coerce_to_declared` 校验
  （byte 0-255、word 0-65535，须整数值），超范围报运行时错误。

---

## 五、语句

程序 = 顶层语句序列，自上而下直接执行（**无 `main` 函数概念**）。

### 5.1 变量声明

```collie
number x = 42;          // 类型 名字 [= 初始化];
const string s = "hi";  // const 必须有初始化（语义层强制）
Point p = new Point();  // 类名作类型
```

const 变量重赋值在语义层与运行期双拦截。同层重复声明报错，内层作用域可遮蔽外层。

### 5.2 控制流

| 语句 | 形式 | 说明 |
|------|------|------|
| if | `if (cond) stmt [else stmt]` | cond 须 bool |
| while | `while (cond) stmt` | |
| do-while | `do { ... } while (cond);` | 体必须是块 |
| for | `for (init; cond; incr) stmt` | C 风格三段，三段均可省略 |
| switch | `switch (expr) { v1, v2 { ... } default { ... } }` | Collie 风格：**无 case 关键字、无 fallthrough、无 break**；候选值逗号分隔，等值匹配（`values_equal`）；default 至多一个 |
| break / continue | `break;` `continue;` | 循环外出现由**语义层**报错（D9）；不作用于 switch |
| return | `return [expr];` | 仅函数体内 |
| 块 | `{ ... }` | 独立作用域 |

### 5.3 函数

```collie
function add(a number, b number) number { return a + b; }
```

- 文法：`function 名字(参数名 类型, ...) 返回类型 { ... }`——**参数是「名字在前、类型在后」**，
  返回类型在参数表后。参数上限 255。
- 支持递归；函数须先声明后调用（顶层按顺序执行）。
- 语义层支持**重载打分**选择最佳匹配；但运行期按名字单槽登记，**无重载分发**（缺口 G4）。
- 返回值经 `coerce_to_declared` 运行期校验（t37）；无 `return` 或 `return;` 得 none。

### 5.4 类（t34/t38–t40）

```collie
class Dog extends Animal {
    @override
    public function speak() string { return base.speak() + "!"; }
    private number age = 0;
    Dog(name string) : base(name) { this.age = 1; }
}
```

- 单继承 `extends`；成员 = 注解* + 可选访问修饰符（`public`/`private`，缺省 public）+
  字段/方法/构造器。`protected` 关键字已注册但成员文法不接受。
- 字段：`类型 名字 [= 初始化];`，初始化沿继承链 **base-first** 执行；构造器**不继承**。
- 构造器：与类名同名、无 `function` 前缀无返回类型；可选 `: base(args)` 委托
  （脱糖为构造器体首条语句）。
- 方法沿继承链查找与覆写；`base.method(args)` 从**定义当前方法的类**的父类链查找，
  绕过子类覆写；`base` 非一等值，必须紧跟方法调用。
- 注解：`@override` 仅可标注方法，语义层校验父类链确有同名方法；`@deprecated`
  接受但不生效；其他注解名报错。
- `new 类名(args)` 创建实例（引用语义）；`this` 仅类方法/构造器内合法。
- 访问修饰符**仅解析记录，尚未强制访问控制**（缺口 G5）。

### 5.5 表达式语句

任意表达式加 `;` 构成语句（如 `print(x);`、`x += 1;`）。

### 5.6 数组

`array a = [1, "two", true];`——元素类型可异构（动态）；引用语义；
读写 `a[i]`、`a.length`、`len(a)`。

### 5.7 字符串操作

索引 `s[i]`（码点，只读）、`s.length`（码点数）、方法见 §6.2。

### 5.8 元组（t45）

```collie
Tuple t = (1, 2, 3);                    // 无名元组
Tuple p = (name: "Alice", age: 18);     // 命名元组（IDENTIFIER : 前瞻消歧）
Tuple s = (name: "x");                  // 单命名元素无逗号也是元组
Tuple e = ();                           // 空元组
(1 + 2) * 3                             // 无名单元素括号仍是分组，不是元组
```

- 访问：`t[0]`（0 起始，负索引支持）、`t.name`（命名字段）、`t.get("key")`（动态键，
  键须 string，未命中报 `Undefined tuple field`）、`t.length`。
- **不可变**：索引赋值语义层 + 运行期双拦截。
- 可混合无名与命名元素：`(1, label: "x")`。
- `toString`：`(1, 2, 3)` / `(name: Alice, age: 18)`。
- 旧设计的 Rust 风格 `.0` 下标与 C# 风格 `Item1` **均不支持**（与前导点小数 `.5` 词法冲突，
  经作者确认废弃）。

---

## 六、内建函数与内建方法

### 6.1 内建函数（自由函数）

| 函数 | 参数 | 返回 | 说明 |
|------|------|------|------|
| `print(...)` | 任意个任意类型 | none | 各参数 `to_string` 后输出，空格分隔，末尾换行 |
| `len(x)` | string/array（object 放行） | integer | string 按码点数 |
| `toString(x)` | 任意 1 个 | string | |
| `toNumber(x)` | string/bool/number | number | 字符串严格匹配（含 `Infinity`/`NaN` 特殊形式），不可解析返回 `NaN`；bool 得 0/1（整数表示） |

### 6.2 内建方法（`recv.method(args)`）

| 接收者 | 方法 | 返回 | 说明 |
|--------|------|------|------|
| 任意 | `toString()` | string | |
| string/bool/数值 | `toNumber()` | number | 同内建函数 |
| 数值 | `abs()` `integerPart()` `decimalPart()` | number | |
| 数值 | `isInteger()` `isDecimal()` `isNaN()` `isInfinity()` `isFinite()` `isPositive()` `isNegative()` | bool | |
| string | `trim()` `trimLeft()` `trimRight()` | string | |
| string | `subString(start[, end])` | string | 码点区间；end 缺省/`-1`/`NaN` 取 length |
| tribool | `isTrue()` `isFalse()` `isUnset()` | bool | 条件语境的显式判断 |
| Tuple | `get(key)` | object（动态） | key 须 string，恰 1 参 |

接收者为 `object`（动态类型）时语义层放行，运行期再按实际类型分发/报错。

### 6.3 `length` 属性（无括号）

适用 string（码点数）、array（元素数）、Tuple（元素数），返回 integer。

---

## 七、执行模型与 CLI

```
collie [-v|--verbose] <source_file>
```

处理流程与门禁（`main.cpp`）：

1. 二进制读文件 → 剥离 UTF-8 BOM（Windows 下设置控制台 UTF-8 输出）。
2. **词法**：异常 → stderr 报错，退出码 1。
3. **语法**：parser 采用 panic-mode 错误恢复（记录错误、跳到同步点继续），返回部分 AST；
   **只要错误列表非空即打印错误数并退出 1，绝不带着部分 AST 继续执行**（语法错误门禁）。
4. **语义**：错误逐条打印，退出 1。
5. **解释执行**：顶层语句直行；运行时错误打印行列号，退出 1。
6. 成功退出 0。

默认安静模式（stdout 仅程序 `print` 输出）；`-v` 打印版本、源码、token 流等诊断信息。

---

## 八、错误处理约定

- **词法错误**：非法字符/非法转义/未闭合字符串等，产出错误 token 或抛 LexError。
- **语法错误**：panic-mode 恢复（同步点：语句边界关键字、分号），继续解析收集多个错误；
  驱动循环有进度守卫防死循环。
- **语义错误**：收集式（不中断，`has_errors()`/`get_errors()` 汇总），panic-mode 同步；
  级联错误可能使计数大于直觉值。
- **运行时错误**：`RuntimeError` 携带行列号；类型不匹配、索引越界、未定义变量/字段、
  const 重赋值、tribool 作条件等。

---

## 九、标准输出格式（`to_string`）

| 值 | 输出 |
|----|------|
| 整数表示 | `42`（无小数点） |
| 小数表示 | 整数值按整数打印（`4 / 2` 输出 `2`）；否则 iostream 默认格式（6 位有效数字）；`+Infinity` / `-Infinity` / `NaN` |
| bool | `true` / `false` |
| tribool | `true` / `false` / `unset` |
| string | 原样（print 不加引号） |
| none | `none` |
| array | `[1, 2, 3]` |
| Tuple | `(1, 2)` / `(name: Alice, age: 18)` |
| 实例 | 类名相关表示 |

---

## 十、已知实现缺口

> 与设计愿景（document/docs/）或本规范上文的偏差，编号供后续任务引用。

| 编号 | 缺口 | 现状 | 计划 |
|------|------|------|------|
| G2 | **char/character/bit/dword 类型不完整**：可声明（语义放行），但解释器 Value 层无专属表示，字符字面量运行期落为 string（t47 已补：char 字面量可入表达式/比较，byte/word 有运行期范围校验） | char 运行期按字符串处理 | 待补（Value 层专属表示） |
| G3 | **函数形参/返回类型缺 integer/decimal/tribool/bit/dword**（`consume_type_token` 未收录） | 形参写这些类型报语法错误 | 待补 |
| G4 | **运行期无函数重载分发**：语义层有重载打分，解释器按名字单槽登记（后声明覆盖先声明） | 同名函数只有最后一个生效 | 类型系统闭环后 |
| G5 | **访问修饰符不强制**：`public`/`private` 仅解析记录，语义/运行期均不拦截私有成员访问；`protected` 成员文法不接受 | — | 待补 |
| G6 | **数组/元组元素类型不追踪**：`array`/`Tuple` 无泛型参数，元素访问结果按 object 动态放行 | 元素类型错误延迟到运行期 | LLVM 前收紧 |
| G7 | **`var` 关键字未启用**：枚举存在但词法表未注册 | 写 `var` 当作标识符 | 待定（是否要类型推导声明） |
| G8 | **`null` 无完整语义**：关键字已注册，解释器有兜底（求值为 none），但 parser primary 层不接受 `null` 字面量 | 表达式中写 `null` 报语法错误 | 待定（与 none/unset 的关系需设计定夺） |
| G9 | **switch 不支持 tribool 穷尽检查**（`==?` 有，switch 语句无） | — | 待定 |
| G10 | **注解 `@deprecated` 不生效**：接受但无调用处告警 | — | 待补 |

---

## 附：规范维护约定

- 每个改变语言可观测行为的任务（tNN）合入时，**同步更新本文对应章节**；
- 缺口修复后从第十节移除并在变更处标注任务号；
- 与设计文档的冲突消解结果记入 `PROGRESS.md`「待与作者确认的语言设计问题」一节。
