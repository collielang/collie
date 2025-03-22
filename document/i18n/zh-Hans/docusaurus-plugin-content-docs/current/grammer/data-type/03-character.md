---
sidebar_position: 3
sidebar_label: 字符类型（Character Type）
---

# 字符类型（Character Type）

:::info

字符类型为[不可变类型](./#immutable-data-type)。

:::

## 🐳类型简介 {#intro}

|    类型     | 占用空间<br />（字节） | 描述                                                         |
| :---------: | :--------------------: | ------------------------------------------------------------ |
|   `char`    |         2 byte         | 单个字符。主要用作框架的底层逻辑实现等，**一般不建议直接使用 `char` 类型**。 |
| `character` |       2 / 4 byte       | 单字，对字符串中的每一项来说。1 个 `character` 为 1 个 `char` 或 1 个 **代理对**（具体细节可参考 [UTF-16 编码](../../implementation-details/unicode.md#utf-16) ） |
|  `string`   |        动态调整        | 字符串。等价于 `character[]`, `[character]`<br />也就是说，`character` 组成的一维数组，天然支持所有 `string` 的操作方式，你可完全将其当作 `string` 对象使用。 |

> 注意：
> - Collie 使用的 [Unicode 编码标准](../../implementation-details/unicode.md) 为 [**UTF-16 编码**](../../implementation-details/unicode.md#utf-16)。
> - 在[不同语言和操作系统中，所采用的默认编码格式不同](../../implementation-details/encoding-used-by-different-languages-or-operating-systems.md)。因此，`char` 类型占用空间不完全相同。

## 🏅语法示例 {#syntax-example}

- 定义一个字符 / 字符串：

```collie
char foo = 'm';
foo.toString() // result: "m"

string bar = "Hello world!"; // 或 string bar = ['h','e','l','l','o'];
bar.length // result: 12
```

- 字符与数字转换：

```collie
number('A') // result: 65
number('a') // result: 113
```

- 字符串拼接：

```collie
"he" + 'l' * 2 + char(111) // result: "hello";
```

- 字符数组与字符串互转：

```collie
char[] foo = "hello";
string bar = ['h','e','l','l','o'].toString();
[char] _foo = "he" + 'l' * 2 + char(111);

foo == bar // result: true
foo[2] == bar[3] == 'l' // result: true
```

- 重复字符 / 字符串：

```collie
"hello collie!" * 2 // result: "hello collie!hello collie!"
"hello collie!" * 0 // result: ""

'm' * 5             // result:  "mmmmm"
'm' * 0             // result:  ""
```

- 多行字符串：多行字符串使用 `"""` 包裹，起止符号需要对齐，其中的字符串内容，前面也需要填充空格对齐起止符号。

```collie
const foo = """
            Hello,
            collie!
            """
// result: "Hello,\ncollie!"

const bar =
    """
    Hello,
    collie!
    """
// result: "Hello,\ncollie!"

const withIndent =
    """
        Hello,
            world!
    """
// result: "    Hello,\n        world!"

// const text = """
// This is a wrong example!
// """
// ❌ 这是一个错误的示范，起止符号没有对齐

// const text = """
// This is a wrong example!
//              """
// ❌ 这是一个错误的示范，字符串内容没有与起止符号对齐
```

- 对字符串中的每一行添加前缀

```collie
"""
Hello,
Collie.
""".indent('    '); // 或 `.indent(' ' * 4)` 或 `.indent(4)` (4代表4个空格)
// result: "    Hello,\n    Collie."
```

- 移除字符串每一行的前导空格（及 Tab 制表符）

```collie
string str = "\nHello,\n    Collie\n\tLang.\n";
str.dedent(); // result: "\nHello,\nCollie\nLang.\n";
```

- 字符串子串：

```collie
// .subString(startIndex[, endIndex = string.length])
// 不传入 endIndex 或传入 -1, NaN 时
string str = "hello world"
str.subString(6); // "llo world"
str.subString(0, 2); // "he"
str[5:-1] // " world"
str[:] // "hello world"
```

- 移除字符串的前导、尾随空白字符

```collie
// 空白字符包含以下字符：空格; Tab 制表符
str.trimLeft();  // 移除字符串的前导空白字符
str.trimRight(); // 移除字符串的尾随空白字符
str.trim();      // 移除字符串的前导及尾随空白字符（等价于 `str.trimLeft().trimRight()`）
```

- 遍历字符串中的每个字符：

```collie
string str = "123𐍈";
for (character c : str) { // 等价于 for (character c : str.toCharacterArray()) {
    // c 依次为 '1' '2' '3' '𐍈'
}

// [不推荐] 对于底层实现，可以遍历其 char 数组
for (char char : str.toNotRecommendedCharArray()) {
    // char 依次为 '1' '2' '3' '\uD800' '\uDF48'
}
```

- 字符串插值

```collie
string name = "Lily";
number age = 18;
string sex = "girl";
string str = @"{name} is {age}-year-old {sex}." // result: "Lily is 18-year-old gril."
```