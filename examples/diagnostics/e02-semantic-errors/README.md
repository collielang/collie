# e02 · 语义错误集（预期失败）

语法全对但语义非法的 6 类典型错误，验证语义分析器的检出面、批量报错能力与行列号精度。**预期非零退出，跑通反而是 BUG。**

## 状态：⛔ 预期失败（退出码 1）

## 植入错误与检出结果：6/6 全检出 ✅

| # | 错误类型 | 实测报错 | 行列精度 |
|---|---|---|---|
| 1 | 未定义变量 | `Undefined variable 'undefinedVar'` | 精确 |
| 2 | number 接字符串 | `Cannot initialize variable of type 'KW_NUMBER' with value of type 'KW_STRING'` | 精确 |
| 3 | const 重新赋值 | `Cannot assign to constant 'LIMIT'` | 精确 |
| 4 | 同作用域重复声明 | `Variable 'dup' is already defined in this scope` | 精确 |
| 5 | 实参个数错误 | `No matching overload for function 'add'` | 精确 |
| 6 | if 条件放 number | `If condition must be a boolean expression` | 精确 |

## 运行

```bat
cmd /c compiler\build\Release\collie.exe examples\diagnostics\e02-semantic-errors\main.collie
```

## 实测输出

```
Found 6 semantic errors:
  Line 5, Column 7: Undefined variable 'undefinedVar'
  Line 8, Column 8: Cannot initialize variable of type 'KW_NUMBER' with value of type 'KW_STRING'
  Line 12, Column 1: Cannot assign to constant 'LIMIT'
  Line 16, Column 8: Variable 'dup' is already defined in this scope
  Line 22, Column 17: No matching overload for function 'add'
  Line 25, Column 1: If condition must be a boolean expression
```

## 诊断质量观察（排错素材）

- **一次报全 6 处、不首错即停**，且互不掩盖（对比 e01 解析级的恢复丢失）——语义分析是当前三阶段（词法/语法/语义）中诊断质量最好的一环。
- 报错文案暴露内部 token 名（`KW_NUMBER`/`KW_STRING`），面向用户宜显示 `number`/`string`。
- 实参个数错误报 `No matching overload`——按重载机制统一处理，单函数场景若直接说"期望 2 个参数、实得 1 个"会更友好。
- 注意与 b03/b07/b10 的**误报**（false positive：数组元素比较、方法调用作条件、字段比较被拒）相对照：本例全是**真阳性**；语义分析当前问题不在漏报而在过严。
