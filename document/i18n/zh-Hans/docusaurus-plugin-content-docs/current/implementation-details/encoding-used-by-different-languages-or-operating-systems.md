---
title: 默认编码
hide_table_of_contents: true
---

# 不同编程语言 / 操作系统的默认编码

| 编程语言 / 操作系统                                        | 默认 Unicode 编码 | 占用空间 <br />（字节） | 描述                                                         |
| ---------------------------------------------------------- | ----------------- | :---------------------: | ------------------------------------------------------------ |
| Python 3                                                   | UTF-32            |         4 byte          | 固定长度编码                                                 |
| Java, JavaScript, C#, Swift<br />Windows NT                | UTF-16            |       2 / 4 byte        | 变长编码。<br />使用 **2 字节** 表示 BMP 字符，<br />使用 **4 字节代理对** 表示超出 BMP 的字符 |
| Node.js, Go, PHP, Rust, Ruby, Perl<br />Unix, Linux, macOS | UTF-8             |       1 - 4 byte        | 变长编码                                                     |
| C / C++                                                    | 无固定标准        |            -            | 可用 UTF-8、UTF-16、UTF-32                                   |

:::note 备注
Python 3 存储时使用内部编码，只有输入输出时，才转为 Unicode 编码。所以准确来说，Python 3 的默认 Unicode 编码应该是 **“UTF-32 + 内部编码”**。
:::
