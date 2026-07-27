# a16 · 位类型与位运算

演示 `bit/byte/word/dword` 位类型、二进制字面量 `0b...`、按位与/或/异或/取反、移位、`getHighByte()/getLowByte()`、位提取/位设置工具函数、位复合赋值。

## 状态：⛔ 等待特性

整例按规范目标语法书写，位类型与位运算落地后应可直接运行。

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| **`bit/byte/word/dword` 类型** | 06-bitwise.md | ⛔（关键字已保留，见下） |
| **二进制字面量 `0b10101010`** | 06-bitwise.md | ⛔ 词法层不识别 |
| **按位运算符 `& \| ^ ~ << >>`** | 06-bitwise.md | ⛔ |
| **位复合赋值 `&= \|= ^= <<=`** | 06-bitwise.md（推导） | ⛔ |
| **`getHighByte()/getLowByte()` 等内建方法** | 06-bitwise.md | ⛔ |
| 十六进制字面量 `0xFF00` | 06-bitwise.md | ⛔（同样断裂） |

## 陷阱备忘

- `byte`、`word`、`dword`、`bit` 均已是保留关键字，**不能用作变量名**（a04 已实测 `word` 陷阱）。

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a16-bitwise\main.collie
```

## 当前实际行为（实测）

35 个语法错误，退出码 1。代表性报错：

```
Parse error at line 7, column 11: Expect ';' after variable declaration.   ← byte a = 0b10101010（0b 不识别）
Parse error at line 13, column 18: Expect expression.                      ← ~a（取反运算符不存在）
Parse error at line 38, column 46: Expect function return type.            ← 返回类型 bit
Parse error at line 42, column 53: Expect parameter type.                  ← 参数类型 bit
Parse error at line 57, column 6: Expect ';' after expression.             ← mask &= ...（位复合赋值）
```

## 诊断观察（排错线索）

`byte a` 能被识别为「类型 + 变量名」的声明开头（说明 byte/word/dword 关键字已在词法层预留），断裂点全部发生在 `0b`/`0x` 字面量与位运算符上——位特性开发可从词法层字面量入手，本例即为回归基准。但 `bit` 尚不能作为函数返回类型/参数类型（38/42 行），说明类型系统侧还需单独接入。
