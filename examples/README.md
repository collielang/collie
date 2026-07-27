# Collie 示例库

按**完整语言规范**书写的系统性示例集，共 **40 个示例目录 / 51 个 `.collie` 文件**。当前能跑的立即可跑；用到未实现特性的按规范目标语法写好并记录真实报错基线——随着编译器完善，⛔ 会逐个变成 ✅，本目录同时充当**回归测试集**与**特性落地进度表**。

## 状态约定

| 标记 | 含义 |
|:--:|---|
| ✅ | 当前版本可正常运行，README 中的输出为**实测**结果 |
| 🟡 | 部分可跑：`main.collie` 为可运行子集，`future.collie` 为规范目标版（当前报错） |
| ⛔ | 等待特性：整例按规范书写，README 记录当前真实报错基线 |

## 批量验证

```bat
powershell -ExecutionPolicy Bypass -File examples\run_all.ps1
```

- 51 个文件全部纳管：34 个应通过（PASS）+ 17 个应失败（XFAIL）。
- 出现 `BROKEN` = 回归；出现 `UPGRADE` = 新特性落地，请更新对应 README 与本看板。
- 最近一次全量验证：**34/34 PASS，17/17 XFAIL**（2026-07）。

单个运行：`compiler\build\Release\collie.exe examples\<系列>\<示例>\main.collie`
压测计时：`examples\stress\time_run.bat examples\stress\<示例>\main.collie`

## A · basics —— 语言特性逐个击破（16）

| 示例 | 状态 | 主题 / 阻塞特性 |
|---|:--:|---|
| [a01-hello-world](basics/a01-hello-world/) | ✅ | print、注释、中文字符串 |
| [a02-variables-types](basics/a02-variables-types/) | 🟡 | 基础类型；future：char/tribool 等扩展类型 |
| [a03-numeric](basics/a03-numeric/) | ✅ | 算术、floor 取模、IEEE 754、number 方法 |
| [a04-strings](basics/a04-strings/) | ✅ | 插值、UTF-8 码点索引、trim/subString |
| [a05-logic-tribool](basics/a05-logic-tribool/) | 🟡 | bool 逻辑；future：tribool 三值逻辑（Kleene 语义已定夺，见 a05 README） |
| [a06-control-flow](basics/a06-control-flow/) | ✅ | if/switch（值跟块、多值逗号）、三目 |
| [a07-for-variants](basics/a07-for-variants/) | 🟡 | 经典 for/while/do-while；future：for-in 等变体 |
| [a08-labels](basics/a08-labels/) | ⛔ | label 跳转（break/continue label） |
| [a09-functions](basics/a09-functions/) | 🟡 | function 过渡文法、递归、重载；future：规范目标签名 |
| [a10-arrays-collections](basics/a10-arrays-collections/) | 🟡 | 数组字面量/负索引/深度相等；future：map/set/push |
| [a11-classes](basics/a11-classes/) | ✅ | class、构造器、this、public/private |
| [a12-inheritance](basics/a12-inheritance/) | 🟡 | extends、`: base(args)`；future：base.method() 等 |
| [a13-enum](basics/a13-enum/) | ⛔ | enum（当前 7 个语法错误） |
| [a14-tuple-destructuring](basics/a14-tuple-destructuring/) | ⛔ | 元组与解构（13 错） |
| [a15-error-handling](basics/a15-error-handling/) | ⛔ | try-catch-finally（61 错，级联最严重） |
| [a16-bitwise](basics/a16-bitwise/) | ⛔ | 位运算与 0b/0x 字面量（35 错） |

## B · practical —— 日常开发逻辑实战（10，全 ✅）

| 示例 | 主题 | 顺带验证的排错素材 |
|---|---|---|
| [b01-fizzbuzz](practical/b01-fizzbuzz/) | 经典 FizzBuzz | — |
| [b02-sorting](practical/b02-sorting/) | 冒泡/选择排序 | 数组引用语义 |
| [b03-binary-search](practical/b03-binary-search/) | 二分查找 | **数组元素 `==` 被语义拒，需先落变量**（`<`/`>` 却可以） |
| [b04-string-toolkit](practical/b04-string-toolkit/) | 回文/反转/统计 | 字符串索引 `==` 可以（与数组不对称） |
| [b05-number-utils](practical/b05-number-utils/) | 素数/GCD/进制 | — |
| [b06-statistics](practical/b06-statistics/) | 均值/方差/极值 | — |
| [b07-bank-account](practical/b07-bank-account/) | 类封装业务规则 | **类类型不能作函数参数；方法调用不能直接作 if 条件** |
| [b08-state-machine](practical/b08-state-machine/) | 状态机 | switch case 必须是字面量 |
| [b09-matrix](practical/b09-matrix/) | 二维数组运算 | `m[i][j]` 读写 |
| [b10-json-builder](practical/b10-json-builder/) | 字符串构建 JSON | **`this.field != ""` 被拒，需先落局部变量** |

## C · edge-cases —— 边界与极端情况（5，全 ✅）

| 示例 | 主题 | 关键发现 |
|---|---|---|
| [c01-numeric-limits](edge-cases/c01-numeric-limits/) | IEEE 754 边界 | `1/0` → `+Infinity`（带加号）；显示约 6 位有效数字（`0.1+0.2` 显示 `0.3` 但 `== 0.3` 为 false） |
| [c02-string-edge](edge-cases/c02-string-edge/) | 空串/emoji/长插值 | emoji 码点正确；**插值不可嵌套**（词法报错） |
| [c03-array-edge](edge-cases/c03-array-edge/) | 空数组/负索引/越界 | 越界诊断精确（附 oob.collie 预期失败件） |
| [c04-deep-recursion](edge-cases/c04-deep-recursion/) | 递归极限 | **6500 层存活、8000 层栈溢出**（退出码 3221225725，无诊断且输出全丢）；**相互递归不可能**（单遍语义分析无前向引用） |
| [c05-scope-shadowing](edge-cases/c05-scope-shadowing/) | 作用域遮蔽 | 全部符合词法作用域预期 |

## D · stress —— 性能压测（6，全 ✅，附实测量级）

| 示例 | 负载 | 实测 |
|---|---|---|
| [d01-loop-throughput](stress/d01-loop-throughput/) | 210 万次循环 | ≈1.1s，**约 200 万次迭代/秒**（基准） |
| [d02-string-concat](stress/d02-string-concat/) | 2.6 万次拼接 + 2 万字符长串 | <0.1s |
| [d03-array-churn](stress/d03-array-churn/) | 10 万次数组读写 + 1000 次深度相等 | ≈0.12s |
| [d04-function-call-overhead](stress/d04-function-call-overhead/) | 40 万次调用 + 5000 层递归 | ≈1.5s，**约 27 万次调用/秒 ≈ 7 倍循环体开销**（首要优化点） |
| [d05-object-churn](stress/d05-object-churn/) | 5 万 new + 10 万字段读写 + 8 万方法调用 | ≈0.4s，无泄漏劣化 |
| [d06-nested-loops](stress/d06-nested-loops/) | O(n²)/O(n³) 共 170 万次内层体 | ≈1.2s，与总迭代数线性相关 |

## E · diagnostics —— 报错质量验收（3，全 ⛔ 预期失败）

| 示例 | 验证对象 | 结论 |
|---|---|---|
| [e01-syntax-errors](diagnostics/e01-syntax-errors/) | 解析器报错与恢复 | 4 处错误只报 2 处：缺分号报在后继行、恢复吞掉同行错误、括号抵消零报错；词法错误无行列号 |
| [e02-semantic-errors](diagnostics/e02-semantic-errors/) | 语义分析检出面 | **6/6 全检出、批量报错、行列精确**——三阶段中质量最好 |
| [e03-runtime-errors](diagnostics/e03-runtime-errors/) | 运行时诊断 | 越界诊断精确且缓冲保留；`toNumber` 非法串静默返 `NaN`（退出码 0，业务须自行 `x != x` 判 NaN） |

## 高频陷阱速查（写 Collie 代码前必读）

1. **数组元素/对象字段不能直接参与 `==`/`!=` 比较或作 if 条件**——先落局部变量（b03/b07/b10）。
2. **方法声明必须用 `function` 过渡文法**：`public function f() number {}`（d05）。
3. **保留字**：`word`、`base`、`byte`、`bit`、`dword` 不能作变量名。
4. **递归别超 5000 层**——6500~8000 层之间栈溢出，且崩溃时输出全丢（c04）。
5. **switch case 只接受字面量**，不接受常量表达式（b08）。
6. **插值不可嵌套**；`@"{...}"` 内再写 `@"..."` 是词法错误（c02）。
7. number 显示约 6 位有效数字，**显示相等 ≠ 值相等**（c01）。
