# b07 · 银行账户

类的实战：封装余额/交易计数、业务规则校验（拒绝非正金额/余额不足）、bool 返回值表达业务成败、两账户转账协作、实例隔离。

## 状态：✅ 可运行（绕过两处语义限制后）

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| class 字段/构造器/方法/this | uncategorized.md 附录 | ✅（t34） |
| private 字段 + 方法内维护 | uncategorized.md | ✅（运行期不强制访问控制） |
| 方法返回 bool / none | function.md | ✅ |

## ⚠ 语义分析限制（本例实测发现，排错素材）

1. **类类型不能作函数参数**：`function transfer(from BankAccount, ...)` 报
   `Unknown method 'withdraw'` / `Unknown property 'owner'`（且 owner 报错行号错乱为 Line 1）——
   语义分析器不解析自定义类形参的成员。跨对象协作逻辑只能写在顶层或类方法内。
2. **方法调用不能直接作 if 条件**：`if (alice.withdraw(70))` 报
   `If condition must be a boolean expression`——方法调用返回类型未参与条件类型推导。
   需先 `bool ok = alice.withdraw(70); if (ok) ...`。

两处限制解除后，本例可回填为更自然的 `transfer()` 函数写法。

## 运行

```bat
compiler\build\Release\collie.exe examples\practical\b07-bank-account\main.collie
```

## 预期输出（实测）

```
Alice：余额 270，交易 2 笔
[Alice] 存款金额必须为正：-5
[Alice] 余额不足：需要 10000，仅有 270
Alice：余额 270，交易 2 笔
转账成功：Alice -> Bob，金额 70
Alice：余额 200，交易 3 笔
Bob：余额 120，交易 1 笔
[Bob] 余额不足：需要 9999，仅有 120
转账失败：Bob -> Alice，金额 9999
Bob：余额 120，交易 1 笔
200
120
```
