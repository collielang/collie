---
title: Default Encoding
hide_table_of_contents: true
---

# Default Encoding in Different Programming Languages / Operating Systems

| Programming Language / Operating System                     | Default Unicode Encoding | Space Occupancy <br />(Bytes) | Description                                                  |
| ---------------------------------------------------------- | ----------------------- | :----------------------------: | ------------------------------------------------------------ |
| Python 3                                                   | UTF-32                  |          4 byte                | Fixed-length encoding                                        |
| Java, JavaScript, C#, Swift<br />Windows NT                | UTF-16                  |        2 / 4 byte              | Variable-length encoding.<br />Uses **2 bytes** for BMP characters,<br />and **4-byte surrogate pairs** for characters outside the BMP. |
| Node.js, Go, PHP, Rust, Ruby, Perl<br />Unix, Linux, macOS | UTF-8                   |        1 - 4 byte              | Variable-length encoding                                     |
| C / C++                                                    | No fixed standard       |              -                 | Can use UTF-8, UTF-16, or UTF-32                            |

:::note
Python 3 uses an internal encoding for storage and only converts to Unicode encoding during input/output. Therefore, strictly speaking, Python 3's default Unicode encoding should be **"UTF-32 + internal encoding"**.
:::
