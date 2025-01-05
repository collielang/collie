# Collie 词法分析器

## 设计概述

词法分析器负责将源代码文本转换为 token 序列。主要功能包括：

1. **Token 识别**：识别并分类各种语言元素
   - 关键字（if, else, class 等）
   - 标识符（变量名、函数名等）
   - 字面量（数字、字符串、字符等）
   - 运算符（+, -, *, / 等）
   - 分隔符（括号、逗号、分号等）

2. **错误处理**：
   - 无效字符检测
   - 未闭合字符串/字符检测
   - 位置信息跟踪（行号、列号）

3. **特殊处理**：
   - UTF-16 字符支持
   - 多行字符串支持
   - 注释处理（单行、多行）
   - 空白字符处理

## 代码结构

```
lexer/
├── README.md           # 本文档
├── token.h            # Token 类型定义
├── token.cpp          # Token 类实现
├── lexer.h            # 词法分析器接口
└── lexer.cpp          # 词法分析器实现
```

## 使用方法

### 基本用法

```cpp
#include "lexer/lexer.h"

// 创建词法分析器
std::string source = "number x = 42;";
collie::Lexer lexer(source);

// 方式1：逐个获取 token
while (true) {
    Token token = lexer.next_token();
    if (token.is_eof()) break;
    // 处理 token...
}

// 方式2：一次性获取所有 tokens
std::vector<Token> tokens = lexer.tokenize();
```

### 错误处理

```cpp
try {
    Token token = lexer.next_token();
    if (token.is_invalid()) {
        // 处理词法错误...
    }
} catch (const std::exception& e) {
    // 处理异常...
}
```

## 编译和测试

### 编译要求
- C++17 或更高版本
- CMake 3.10 或更高版本

### 编译步骤

```bash
# 在项目根目录下
mkdir build && cd build
cmake ..
make

# 运行测试
./tests/lexer_tests
```

## 实现细节

### 1. UTF-16 字符处理

Collie 使用 UTF-16 编码处理字符，需要特别注意：

```cpp
// 处理 UTF-16 字符
char16_t ch = scan_utf16_char();
if (is_surrogate_pair(ch)) {
    char16_t low = scan_utf16_char();
    // 组合代理对...
}
```

### 2. 多行字符串

支持使用三引号的多行字符串：

```collie
const text = """
    Hello,
    World!
    """
```

处理时需要：
- 检查起止引号对齐
- 保持正确的缩进
- 正确处理换行符

### 3. 注释处理

支持两种注释形式：
- 单行注释：`// 注释内容`
- 多行注释：`/* 注释内容 */`

### 4. 数字字面量

支持多种数字格式：
- 整数：`42`
- 小数：`3.14`
- 科学计数：`1.23e-4`
- 不同进制：
  - 二进制：`0b1010`
  - 八进制：`0o755`
  - 十六进制：`0xFF`

## 注意事项

1. **性能考虑**
   - 使用 string_view 避免不必要的字符串拷贝
   - 关键字查找使用哈希表
   - 字符串缓冲区预分配

2. **错误恢复**
   - 词法错误不应该导致分析过程终止
   - 应该继续扫描直到找到下一个有效的 token

3. **内存管理**
   - 注意大文件处理时的内存使用
   - 及时释放不需要的资源

## TODO

- [ ] 添加更多单元测试
- [ ] 优化数字字面量解析
- [ ] 改进错误报告机制
- [ ] 添加词法分析性能基准测试