# 工具类模块

本目录包含编译器使用的各种工具类。

## 模块列表

### Token 工具类 (token_utils)
- 提供 Token 相关的辅助函数
- 包括 Token 类型转字符串等功能

### 版本信息工具类 (version_info)
- 提供编译器版本和环境信息的显示功能
- 包含以下主要功能：
  - `get_version_info()`: 获取编译器版本信息
  - `get_environment_info()`: 获取编译环境信息（编译器版本、操作系统、CPU 架构等）

## 使用示例

```cpp
// 显示版本信息
std::cout << collie::utils::get_version_info();

// 显示环境信息
std::cout << collie::utils::get_environment_info();
```
