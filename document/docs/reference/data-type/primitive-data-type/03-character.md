---
sidebar_label: Character Type
---

# Character Type

## 🐳Type Overview {/* #intro */}

|    Type     | Size<br />(bytes) | Description                                                  |
| :---------: | :---------------: | ------------------------------------------------------------ |
|   `char`    |      2 byte       | A single character. Mainly used for low-level logic in frameworks; **directly using the `char` type is generally not recommended**. |
| `character` |    2 / 4 byte     | A single grapheme, with respect to each item in a string. One `character` is one `char` or one **surrogate pair** (for details, see [UTF-16 encoding](../../../implementation-details/unicode.md#utf-16)). |
|  `string`   |     dynamic       | A string. Equivalent to `character[]`, `[character]`.<br />In other words, a one-dimensional array of `character` natively supports all `string` operations, and you can treat it entirely as a `string` object. |

> Notes:
> - The [Unicode encoding standard](../../../implementation-details/unicode.md) used by Collie is [**UTF-16 encoding**](../../../implementation-details/unicode.md#utf-16).
> - The [default encoding differs across languages and operating systems](../../../implementation-details/encoding-used-by-different-languages-or-operating-systems.md). Therefore, the size occupied by the `char` type is not exactly the same everywhere.

## 🏅Syntax Examples {/* #syntax-example */}

- Define a character / string:

```collie
char foo = 'm';
foo.toString() // result: "m"

string bar = "Hello world!"; // or: string bar = ['h','e','l','l','o'];
bar.length // result: 12
```

- Converting between characters and numbers:

```collie
number('A') // result: 65
number('a') // result: 97
```

- String concatenation:

```collie
"he" + 'l' * 2 + char(111) // result: "hello";
```

- Converting between character arrays and strings:

```collie
char[] foo = "hello";
string bar = ['h','e','l','l','o'].toString();
[char] _foo = "he" + 'l' * 2 + char(111);

foo == bar // result: true
foo[2] == bar[3] == 'l' // result: true
```

- Repeating a character / string:

```collie
"hello collie!" * 2 // result: "hello collie!hello collie!"
"hello collie!" * 0 // result: ""

'm' * 5             // result:  "mmmmm"
'm' * 0             // result:  ""
```

- Multi-line strings: multi-line strings are wrapped in `"""`. The opening and closing delimiters must be aligned, and the string content must also be padded with leading spaces to align with the delimiters.

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
// ❌ This is a wrong example: the opening and closing delimiters are not aligned

// const text = """
// This is a wrong example!
//              """
// ❌ This is a wrong example: the string content is not aligned with the delimiters
```

- Add a prefix to each line of a string:

```collie
"""
Hello,
Collie.
""".indent('    '); // or `.indent(' ' * 4)` or `.indent(4)` (4 means 4 spaces)
// result: "    Hello,\n    Collie."
```

- Remove the leading whitespace (and Tab characters) from each line of a string:

```collie
string str = "\nHello,\n    Collie\n\tLang.\n";
str.dedent(); // result: "\nHello,\nCollie\nLang.\n";
```

- Substrings:

```collie
// .subString(startIndex[, endIndex = string.length])
// When endIndex is omitted or -1, NaN is passed
string str = "hello world"
str.subString(6); // "llo world"
str.subString(0, 2); // "he"
str[5:-1] // " world"
str[:] // "hello world"
```

- Remove leading and trailing whitespace from a string:

```collie
// Whitespace characters include: space; Tab character
str.trimLeft();  // Remove leading whitespace from the string
str.trimRight(); // Remove trailing whitespace from the string
str.trim();      // Remove both leading and trailing whitespace (equivalent to `str.trimLeft().trimRight()`)
```

- Iterate over each character in a string:

```collie
string str = "123𐍈";
for (character c : str) { // equivalent to: for (character c : str.toCharacterArray()) {
    // c is, in turn, '1' '2' '3' '𐍈'
}

// [Not recommended] For low-level implementations, you can iterate over its char array
for (char char : str.toNotRecommendedCharArray()) {
    // char is, in turn, '1' '2' '3' '\uD800' '\uDF48'
}
```

- String interpolation:

```collie
string name = "Lily";
number age = 18;
string sex = "girl";
string str = @"{name} is {age}-year-old {sex}." // result: "Lily is 18-year-old girl."
```
