# e01 · 语法错误集（预期失败）

故意写错的源码，验证解析器报错的行列号精度、错误恢复能力与退出码。**本目录所有文件预期非零退出，跑通反而是 BUG。**

## 状态：⛔ 预期失败（退出码 1）

## 文件

| 文件 | 植入错误 | 实测结果 |
|---|---|---|
| `main.collie` | 缺分号 / 括号不匹配 / if 不闭合+游离 `}` / 保留字作变量名 | 退出码 1，报 2 个语法错误 |
| `lexer_fatal.collie` | `@` 后不跟字符串（词法级） | 退出码 1，1 行词法错误 |

## 运行

```bat
cmd /c compiler\build\Release\collie.exe examples\diagnostics\e01-syntax-errors\main.collie
cmd /c compiler\build\Release\collie.exe examples\diagnostics\e01-syntax-errors\lexer_fatal.collie
```

## 实测输出

`main.collie`：

```
Parse error at line 8, column 1: Expect ';' after variable declaration.
Parse error at line 18, column 8: Expect variable name.
Found 2 syntax errors.
```

`lexer_fatal.collie`：

```
Error during tokenization: Unexpected character
```

## 诊断质量观察（排错素材）

植入 4 处解析级错误只报出 2 处，逐条分析：

1. **缺分号（第 5 行）报在第 8 行第 1 列**——解析器在"下一条语句开头"才发现缺分号，行号指向的不是错误行而是后继行。排错时看到 `Expect ';'` 应**向上找上一条语句**。
2. **括号不匹配（第 8 行 `(2 + 3;`）完全被掩盖**——错误恢复（同步到语句边界）时把第 8 行整行跳过，行内第二个错误丢失。一次修复后重跑可能"冒出新错误"即此原因。
3. **if 不闭合 + 后文游离 `}` 相互抵消，零报错**——花括号只做配对计数，不校验语义归属。块结构错乱时可能完全静默，靠缩进审查而非依赖编译器。
4. **保留字 `word` 作变量名**：`Expect variable name`（第 18 行第 8 列，精确）——但报错文案未提示"这是保留字"，初学者易困惑。
5. **词法错误最弱**：无行列号、无字符回显（哪个字符 Unexpected 不知道），且词法阶段中止后解析级错误全部不可见。

## 改进建议（给编译器开发）

- 词法错误补行列号 + 违规字符回显。
- 缺分号类错误行号回退到上一 token 行尾。
- 保留字用作标识符时给出专门提示。
