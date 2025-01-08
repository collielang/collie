# IR 生成器

中间代码生成器（IR Generator）是编译器的重要组成部分，负责将抽象语法树（AST）转换为中间表示（IR）。

## 模块功能

- AST 到 IR 的转换
- 提供优化基础设施
- 支持不同后端代码生成

## 设计思路

### IR 表示形式

采用三地址码（Three-Address Code）作为中间表示形式：
- 每条指令最多包含三个地址（两个操作数，一个结果）
- 便于优化和代码生成
- 支持 SSA（Static Single Assignment）形式

### 主要组件

1. **IR 节点（ir_node.h/cpp）**
   - 定义 IR 指令的基本结构
   - 支持各种操作类型（算术、逻辑、控制流等）
   - 提供节点访问和修改接口

2. **IR 生成器（ir_generator.h/cpp）**
   - 实现 AST 到 IR 的转换
   - 处理表达式、语句、函数等转换
   - 生成基本块和控制流图

3. **IR 优化器接口（ir_optimizer.h/cpp）**
   - 定义优化器接口
   - 提供基本的优化支持
   - 支持自定义优化策略

4. **IR 工具（ir_utils.h/cpp）**
   - 提供 IR 操作的辅助函数
   - 实现 IR 的序列化和反序列化
   - 提供调试和可视化支持

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

```cpp
// 创建 IR 生成器
IRGenerator generator;

// 将 AST 转换为 IR
IRNode* ir = generator.generate(ast);

// 打印 IR
ir->dump();
```