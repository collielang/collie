# VSCode 扩展测试指南

本文档介绍如何运行和编写 Collie 语言 VSCode 扩展的语法高亮测试。

## 环境准备

1. 确保已安装 Node.js（版本 >= 14）
2. 确保已安装 npm（通常随 Node.js 一起安装）
3. 确保已安装 Visual Studio Code

## 安装依赖

在扩展目录下运行：

```bash
cd editor-support/vscode/collie-language-support
npm install
```

## 运行测试

在扩展目录下运行：

```bash
npm test
```

测试脚本会：
1. 自动扫描 `tests/syntax/` 目录下的所有 `.test.collie` 文件
2. 对每个测试文件进行语法解析
3. 验证每个断言是否符合预期
4. 输出测试结果统计

测试输出示例：
```
Testing basic.test.collie:
  ✓ Line 4: "// 这是一个注释" has scope comment.line.double-slash.collie
  ✓ Line 11: "number" has scope storage.type.primitive.collie
  ...

Test Results:
  Passed: 42
  Failed: 0
  Total: 42
```

如果需要手动运行测试命令，可以使用：

```bash
vscode-tmgrammar-snap syntaxes/collie.tmLanguage.json tests/**/*.test.collie -g source.collie
```

参数说明：
- 第一个参数：语法规则文件路径
- 第二个参数：测试文件的 glob 模式
- `-g`: 指定语法作用域

第一次运行测试时，会生成快照文件。后续运行时，会将新的测试结果与快照进行比较。
如果需要更新快照，可以添加 `-u` 参数：

```bash
npm test -- -u
```

## 测试文件结构

测试文件位于 `tests/syntax/` 目录下，使用 `.test.collie` 扩展名。每个测试文件的结构如下：

```collie
# SYNTAX TEST "source.collie"

// 测试代码
# ^ 期望的语法高亮作用域

/* 更多测试代码 */
# ^ 更多期望的语法高亮作用域
```

## 编写测试用例

1. 在 `tests/syntax/` 目录下创建新的 `.test.collie` 文件
2. 文件第一行必须是：`# SYNTAX TEST "source.collie"`
3. 编写测试代码，每个测试用例包含：
   - 实际的 Collie 代码
   - 以 `#` 开头的断言行，指定期望的语法高亮作用域

### 断言语法

- `#` 表示断言行的开始
- `^` 指向上一行中要测试的位置
- 空格用于对齐 `^` 与上一行的对应位置
- 在 `^` 后面写出期望的语法作用域

### 示例

```collie
# SYNTAX TEST "source.collie"

// 这是一个注释
# ^^^^^^^^^^^^^^^^ comment.line.double-slash.collie

number value = 42;
# ^^^^^ storage.type.primitive.collie
#       ^^^^^ variable.other.collie
#             ^ keyword.operator.assignment.collie
#               ^^ constant.numeric.integer.collie
```

## 支持的语法作用域

主要的语法作用域包括：

1. 注释
   - `comment.line.double-slash.collie`: 单行注释
   - `comment.block.collie`: 多行注释

2. 关键字
   - `keyword.control.collie`: 控制流关键字
   - `keyword.other.collie`: 其他关键字
   - `storage.modifier.collie`: 修饰符

3. 类型
   - `storage.type.primitive.collie`: 基本类型
   - `storage.type.collection.collie`: 集合类型

4. 声明
   - `entity.name.function.collie`: 函数名
   - `entity.name.type.class.collie`: 类名
   - `variable.parameter.collie`: 函数参数
   - `variable.other.collie`: 变量名

5. 字符串
   - `string.quoted.double.collie`: 双引号字符串
   - `string.quoted.triple.collie`: 三引号字符串
   - `string.interpolated.collie`: 字符串插值
   - `string.quoted.single.collie`: 字符字面量

6. 数字
   - `constant.numeric.hex.collie`: 十六进制数
   - `constant.numeric.binary.collie`: 二进制数
   - `constant.numeric.decimal.collie`: 小数
   - `constant.numeric.integer.collie`: 整数

7. 运算符
   - `keyword.operator.arithmetic.collie`: 算术运算符
   - `keyword.operator.assignment.collie`: 赋值运算符
   - `keyword.operator.comparison.collie`: 比较运算符
   - `keyword.operator.logical.collie`: 逻辑运算符
   - `keyword.operator.bitwise.collie`: 位运算符

## 调试测试

如果测试失败，你可以：

1. 检查语法配置文件 (`syntaxes/collie.tmLanguage.json`)
2. 检查测试文件中的断言是否正确
3. 使用 VSCode 的 "Developer: Inspect Editor Tokens and Scopes" 命令来查看实际的语法高亮作用域

## 添加新的测试用例

建议按照以下功能分类添加测试用例：

1. `basic.test.collie`: 基本语法测试
2. `types.test.collie`: 类型系统测试
3. `functions.test.collie`: 函数相关测试
4. `classes.test.collie`: 类相关测试
5. `strings.test.collie`: 字符串相关测试
6. `operators.test.collie`: 运算符相关测试
7. `generics.test.collie`: 泛型相关测试

每个测试文件应该专注于测试一个特定的语言特性，并包含充分的测试用例。
