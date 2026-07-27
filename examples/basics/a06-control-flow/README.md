# a06 · 流程控制

演示 if/else if/else、switch（Collie 特色：值直接跟代码块、无 case/break、无 fallthrough、多值匹配）、while、do-while、break/continue。

## 状态：✅ 可运行

## 依赖特性

| 特性 | 规范出处 | 实现状态 |
|------|----------|:--:|
| if / else if / else | grammer/control-flow.md | ✅ |
| switch（多值/default/字符串匹配） | control-flow.md | ✅（t19） |
| while / do-while | control-flow.md | ✅（M4/t18） |
| break / continue（循环上下文由语义层校验） | control-flow.md | ✅（D9） |
| 字符串插值（输出用） | 03-character.md | ✅（t32） |

## 运行

```bat
compiler\build\Release\collie.exe examples\basics\a06-control-flow\main.collie
```

## 预期输出（实测）

```
舒适
周末
shepherd
while 第 0 次
while 第 1 次
while 第 2 次
do-while 至少执行一次（j = 10）
奇数和 = 25
```
