# IR 模块

## 简介

IR（Intermediate Representation，中间表示）模块是编译器的核心组件之一，负责将抽象语法树（AST）转换为更接近目标代码的中间表示形式。

### 设计目标

1. 提供一个统一的、平台无关的代码表示形式
2. 便于进行各种编译优化
3. 简化后端代码生成的复杂度
4. 支持不同后端代码生成

## IR 表示形式

采用三地址码（Three-Address Code）作为中间表示形式：
- 每条指令最多包含三个地址（两个操作数，一个结果）
- 便于优化和代码生成
- 支持 SSA（Static Single Assignment）形式

## 主要组件

### IR 节点（ir_node.h/cpp）

IR 节点是构成 IR 的基本单位，包括：

- `IRNode`：所有 IR 节点的基类
- `IROperand`：操作数基类，包括变量和常量
- `IRInstruction`：IR 指令，表示具体的操作
- `IRBasicBlock`：基本块，包含一系列指令
- `IRFunction`：函数，由多个基本块组成

主要功能：
- 定义 IR 指令的基本结构
- 支持各种操作类型（算术、逻辑、控制流等）
- 提供节点访问和修改接口

### IR 生成器（ir_generator.h/cpp）

负责将 AST 转换为 IR，主要功能包括：

- 表达式的 IR 生成
- 语句的 IR 生成
- 函数的 IR 生成
- 生成基本块和控制流图

### IR 优化器（ir_optimizer.h/cpp）

实现各种 IR 级别的优化，包括：

- 常量折叠
- 死代码消除
- 基本块合并
- 循环优化
  - 循环不变量外提
  - 循环展开
  - 强度削弱

### IR 工具（ir_utils.h/cpp）

提供辅助功能：
- IR 操作的辅助函数
- IR 的序列化和反序列化
- 调试和可视化支持

## 设计思路

1. **模块化设计**：每个组件都有明确的职责，便于维护和扩展

2. **类型系统**：支持基本的数据类型（整数、浮点数、布尔值、字符串）

3. **SSA 形式**：IR 采用静态单赋值（SSA）形式，简化优化分析

4. **控制流图**：提供完整的控制流分析支持

## 代码结构
```
ir/
├── ir_node.h         # IR 节点定义
├── ir_node.cpp       # IR 节点实现
├── ir_generator.h    # IR 生成器接口
├── ir_generator.cpp  # IR 生成器实现
├── ir_optimizer.h    # IR 优化器接口
├── ir_optimizer.cpp  # IR 优化器实现
├── ir_utils.h        # IR 工具函数
└── ir_utils.cpp      # IR 工具实现
```

## 使用示例

### 使用 IR 生成器
```cpp
// 创建 IR 生成器
IRGenerator generator;

// 将 AST 转换为 IR
IRNode* ir = generator.generate(ast);

// 打印 IR
ir->dump();
```

### 手动创建 IR
```cpp
// 创建一个简单的 IR
auto func = std::make_shared<IRFunction>("test_func");
auto block = std::make_shared<IRBasicBlock>();
auto var = std::make_shared<IRVariable>("x", IRType::INT);
auto constant = std::make_shared<IRConstant>(42);
auto add = std::make_shared<IRInstruction>(IROpType::ADD);
add->addOperand(var);
add->addOperand(constant);
block->addInstruction(add);
func->addBasicBlock(block);
```

## 注意事项

1. IR 节点使用智能指针管理内存
2. 优化器的执行顺序会影响优化效果
3. 需要维护正确的控制流信息
4. 在进行优化时需要保持 SSA 属性
5. 优化器的执行顺序可能影响优化效果