# Collie 语法分析器

## 概述

语法分析器负责将词法分析器生成的 token 序列转换为抽象语法树（AST）。主要功能包括：

1. **语法分析**
   - 递归下降解析
   - 运算符优先级处理
   - 语法错误检测和恢复

2. **AST 构建**
   - 表达式节点
   - 语句节点
   - 类型节点
   - 声明节点

## 代码结构

```
parser/
├── README.md          # 本文档
├── ast.h             # AST 节点定义
├── ast.cpp           # AST 节点实现
├── parser.h          # 语法分析器接口
└── parser.cpp        # 语法分析器实现
```

## AST 节点类型

### 表达式节点
1. **字面量表达式** (LiteralExpr)
   - 数字字面量
   - 字符串字面量
   - 字符字面量
   - 布尔字面量

2. **标识符表达式** (IdentifierExpr)
   - 变量引用
   - 函数引用

3. **二元表达式** (BinaryExpr)
   - 算术运算：+, -, *, /, %
   - 比较运算：==, !=, >, <, >=, <=
   - 逻辑运算：&&, ||
   - 位运算：&, |, ^, <<, >>

### 语句节点（待实现）
1. **声明语句**
   - 变量声明
   - 函数声明
   - 类声明

2. **控制流语句**
   - if-else 语句
   - while 循环
   - for 循环
   - switch 语句

3. **其他语句**
   - 表达式语句
   - 返回语句
   - 块语句

## 语法规则

### 表达式语法
```ebnf
expression     → assignment
assignment     → IDENTIFIER "=" assignment | logical_or
logical_or     → logical_and ("||" logical_and)*
logical_and    → equality ("&&" equality)*
equality       → comparison (("==" | "!=") comparison)*
comparison     → term ((">" | ">=" | "<" | "<=") term)*
term           → factor (("+" | "-") factor)*
factor         → unary (("*" | "/" | "%") unary)*
unary          → ("!" | "-" | "~") unary | primary
primary        → NUMBER | STRING | CHAR | "true" | "false" | "(" expression ")"
               | IDENTIFIER
```

### 语句语法（待实现）
```ebnf
statement      → exprStmt | declStmt | ifStmt | whileStmt | forStmt
               | returnStmt | block
exprStmt       → expression ";"
declStmt       → type IDENTIFIER ("=" expression)? ";"
ifStmt         → "if" "(" expression ")" statement ("else" statement)?
whileStmt      → "while" "(" expression ")" statement
block          → "{" statement* "}"
```

## 错误处理

1. **语法错误**
   - 缺少分隔符（括号、分号等）
   - 非法的表达式
   - 非法的语句

2. **错误恢复**
   - 同步标记（分号、右大括号等）
   - 恐慌模式恢复

## 使用示例

```cpp
#include "parser/parser.h"

// 创建词法分析器
std::string source = "number x = 42 + y;";
collie::Lexer lexer(source);

// 创建语法分析器
collie::Parser parser(lexer);

// 解析代码
try {
    std::unique_ptr<collie::Stmt> stmt = parser.parse();
    // 使用语法树...
} catch (const std::exception& e) {
    std::cerr << "Parse error: " << e.what() << std::endl;
}
```

## 注意事项

1. **内存管理**
   - 使用智能指针管理 AST 节点
   - 避免循环引用

2. **错误处理**
   - 提供详细的错误信息
   - 包含错误位置（行号、列号）
   - 支持错误恢复

3. **扩展性**
   - 访问者模式支持
   - 易于添加新的节点类型
   - 易于修改语法规则

## TODO

- [ ] 实现基本的表达式解析
- [ ] 添加语句节点定义和解析
- [ ] 实现错误恢复机制
- [ ] 添加更多单元测试
- [ ] 支持类型检查
- [ ] 优化内存使用