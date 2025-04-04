# Collie 编译器

这是 Collie 语言的官方编译器实现。

## 实现语言

项目使用 C++ 作为实现语言，主要考虑因素：
- 优秀的性能表现，适合编译器/解释器开发
- 灵活的内存管理机制
- 良好的跨平台支持
- 丰富的编译器开发相关库

## 编译器架构

编译器采用经典的多阶段编译架构：

1. **词法分析器 (Lexer)**
   - 将源代码转换为 token 序列
   - 支持 UTF-8/UTF-16 编码
   - 处理注释、空白字符、多行字符串等
   - 识别关键字、标识符、字面量等
   - 错误处理和恢复机制

2. **语法分析器 (Parser)**
   - 将 token 序列转换为抽象语法树(AST)
   - 支持表达式和语句解析
   - 函数声明和调用
   - 进行语法正确性检查
   - 错误处理和恢复机制
   - 构建程序的层次结构

3. **语义分析器 (Semantic Analyzer)**
   - 类型检查与推断
   - 作用域分析
   - 变量声明和使用检查

4. **中间代码生成器 (IR Generator)**
   - 生成中间表示代码
   - 提供优化基础
   - 支持 SSA 形式
   - 构建控制流图
   - 便于代码优化和分析

5. **代码优化器 (Optimizer) (待实现)**
   - 执行各种优化策略
   - 提升代码执行效率

6. **目标代码生成器 (Code Generator) (待实现)**
   - 生成最终的机器码
   - 处理平台相关的优化

## 项目结构
```
compiler/
├── lexer/           # 词法分析器
├── parser/          # 语法分析器
├── ast/             # 抽象语法树定义和操作
├── semantic/        # 语义分析器
├── ir/              # 中间代码生成（待实现）
├── optimizer/       # 代码优化器（待实现）
├── codegen/         # 目标代码生成（待实现）
├── types/           # 类型系统实现
└── utils/           # 通用工具函数
└── tests/           # 单元测试
```

## 构建和测试
```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest
```

## 开发路线图

1. **Phase 1: 基础框架**
   - 实现基本的词法分析器
   - 支持基本的 token 识别
   - 搭建测试框架

2. **Phase 2: 语法分析**
   - 实现基本的语法分析器
   - 构建抽象语法树
   - 支持基本语法结构

3. **Phase 3: 类型系统**
   - 实现基本类型
   - 添加类型检查
   - 支持类型推断

4. **Phase 4: 语义分析**
   - 实现作用域分析
   - 添加语义检查
   - 支持变量声明和使用检查

5. **Phase 5: 代码生成**
   - 实现中间代码生成
   - 添加基本优化
   - 生成目标代码

6. **Phase 6: 完善功能**
   - 实现高级语言特性
   - 添加更多优化策略
   - 提升编译性能

## 当前进度
- [x] 词法分析器
  - [x] 基本功能
  - [ ] UTF-8 支持
  - [ ] 中文标识符
- [x] 语法分析器
  - [x] 基本功能
  - [ ] 类型检查
  - [ ] 作用域分析
- [ ] 语义分析器
- [x] 中间代码生成器
  - [x] IR 节点定义
  - [x] IR 优化器框架
  - [ ] AST 到 IR 的转换
  - [x] 基本块和控制流图生成
- [ ] 代码优化器
- [ ] 目标代码生成

## 模块说明

- lexer: 词法分析器，负责将源代码转换为 token 序列
- parser: 语法分析器，负责将 token 序列转换为抽象语法树(AST)
- semantic: 语义分析器，负责类型检查和语义错误检测
- ir: 中间代码生成器，负责将 AST 转换为中间表示代码
  - ir_node: IR 节点的定义和实现
  - ir_generator: AST 到 IR 的转换
  - ir_optimizer: IR 级别的优化器
  - ir_utils: IR 相关的辅助函数
- utils: 通用工具函数
  - token_utils: Token 相关的辅助函数
  - version_info: 编译器版本和环境信息显示功能
