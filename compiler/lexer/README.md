# 词法分析器

## 概述

词法分析器负责将源代码文本转换为 token 序列。主要功能包括：

1. **Token 识别**：识别并分类各种语言元素
   - 关键字（if, else, class 等）
   - 标识符（变量名、函数名等）
   - 字面量（数字、字符串、字符等）
   - 运算符（+, -, *, / 等）
   - 分隔符（括号、逗号、分号等）

2. **Unicode 支持**
   - UTF-8 编码支持
   - UTF-16 编码支持
   - 中文标识符支持
   - Unicode 字符串和字符字面量

3. **错误处理**
   - 无效字符检测
   - 未闭合字符串/字符检测
   - 无效的 UTF-8/UTF-16 序列检测
   - 位置信息跟踪（行号、列号）

4. **特殊处理**
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

## 实现细节

### Unicode 处理
1. **UTF-8 编码**
   - 支持 1-4 字节的 UTF-8 序列
   - 自动检测无效的 UTF-8 序列
   - 正确处理中文等多字节字符

2. **UTF-16 编码**
   - 支持基本平面字符（BMP）
   - 支持代理对（Surrogate Pairs）
   - 自动检测无效的代理对

3. **中文标识符**
   - 支持中文变量名和函数名
   - 正确处理中文字符的位置信息
   - 支持中文注释

### 错误处理
- 详细的错误信息
- 准确的错误位置（行号、列号）
- 错误恢复机制
- 友好的错误提示

## 使用方法

### 基本用法

```cpp
#include "lexer/lexer.h"

// 创建词法分析器（默认 UTF-8 编码）
std::string source = "number 变量 = 42;";
collie::Lexer lexer(source);

// 获取所有 tokens
auto tokens = lexer.tokenize();

// 逐个处理 token
for (const auto& token : tokens) {
    if (token.type() == TokenType::IDENTIFIER) {
        // 处理标识符...
    }
}
```

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
1. 编码处理
   - 源文件必须是有效的 UTF-8 或 UTF-16 编码
   - 不支持混合编码
   - Windows 平台下注意代码页设置

2. 错误处理
   - 词法错误不应该导致分析过程终止，应该继续扫描直到找到下一个有效的 token
   - 捕获并处理 LexError 异常
   - 检查 token 的有效性
   - 注意位置信息的准确性

3. 性能考虑
   - 使用 string_view 避免不必要的拷贝
   - 关键字查找使用哈希表
   - 字符串缓冲区预分配
   - 预分配足够的 token 缓冲区
   - 优化字符串处理

4. 内存管理
   - 注意大文件处理时的内存使用
   - 及时释放不需要的资源
