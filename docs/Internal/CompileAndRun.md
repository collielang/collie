# 编译和运行指南

本文档介绍如何在不同操作系统上编译和运行项目。

## 环境要求
- CMake 3.10 或更高版本
- Visual Studio 2019/2022 (Windows)
- 或 GCC/Clang (Linux/macOS)

## Windows 下编译和运行

### 前置要求
- [Visual Studio](https://visualstudio.microsoft.com/downloads/)（2017或更新版本，社区版即可）
  - 安装时选择"使用C++的桌面开发"工作负载
- [CMake](https://cmake.org/download/) (3.10或更高版本)
  - 安装时勾选"Add CMake to system PATH"

### 所在目录

以下操作都是在 compiler 目录中的

```bash
cd compiler
```

### 编译步骤

选择其中一种方式即可。

1. 生成 Visual Studio 项目文件：
   ```bash
   # 创建并进入构建目录
   mkdir build
   cd build

   # 生成 Visual Studio 项目文件
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```

   !> 注意：在中国，可能会因网络问题无法下载 googletest 框架

#### 使用 Visual Studio
2. 打开解决方案：
   - 用 Visual Studio 打开 `build/collie_compiler.sln`
   - 在解决方案资源管理器中，`lexer_tests` 已被设置为启动项目
   - 在解决方案资源管理器中右键点击解决方案
   - 选择"生成解决方案"

3. 运行测试：
   - 按 F5 或点击"调试 -> 开始调试"
   - 或按 Ctrl+F5 运行不带调试器的版本
   - 测试结果会在输出窗口显示

#### 使用命令行
2. 编译：
   ```bash
   cmake --build . --config Debug
   # 或
   cmake --build . --config Release
   ```

3. 运行测试：
   ```bash
   # 测试
   ctest -C Debug
   ```
   ```bash
   # Debug 模式
   .\Debug\lexer_tests.exe
   # 或 Release 模式
   .\Release\lexer_tests.exe
   ```

## Linux (Ubuntu) 下编译和运行

### 前置要求
```bash
# 安装必要工具
sudo apt update
sudo apt install -y build-essential cmake git
```

### 编译步骤
1. 创建并进入构建目录：
```bash
mkdir build
cd build
```

2. 生成构建文件并编译：
```bash
cmake ..
make
```

3. 运行测试：
```bash
ctest
```

## macOS 下编译和运行

### 前置要求
1. 安装Xcode Command Line Tools：
```bash
xcode-select --install
```

2. 安装Homebrew（如果尚未安装）：
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

3. 安装CMake：
```bash
brew install cmake
```

### 编译步骤
1. 创建并进入构建目录：
```bash
mkdir build
cd build
```

2. 生成构建文件并编译：
```bash
cmake ..
make
```

3. 运行测试：
```bash
ctest
```

## 编译产物位置

### Windows
- Debug 模式：
  - 测试程序：`build/Debug/lexer_tests.exe`
  - 编译器库：`build/Debug/collie_compiler.lib`

- Release 模式：
  - 测试程序：`build/Release/lexer_tests.exe`
  - 编译器库：`build/Release/collie_compiler.lib`

## 测试输出说明
运行测试程序时，会看到类似这样的输出：
```
[==========] Running XX tests from XX test suites.
[----------] Global test environment set-up.
[----------] X tests from LexerTest
[ RUN      ] LexerTest.BasicTokens
[       OK ] LexerTest.BasicTokens (0 ms)
...
[==========] XX tests from XX test suites ran. (XX ms total)
[  PASSED  ] XX tests.
```

- `[  PASSED  ]` 表示测试通过
- `[  FAILED  ]` 表示测试失败
- 失败的测试会显示详细的失败信息，包括期望值和实际值

## 调试
1. 在 Visual Studio 中：
   - 在代码中设置断点
   - 按 F5 开始调试
   - 使用调试工具栏或快捷键控制程序执行

2. 添加自己的测试：
   - 在 `tests/lexer_test.cpp` 中添加新的测试用例
   - 遵循现有的测试格式：
     ```cpp
     TEST(LexerTest, YourTestName) {
         std::string source = "your test code";
         Lexer lexer(source);
         // ... 你的测试逻辑 ...
     }
     ```

## 开发流程
1. 实现新功能
2. 添加单元测试
3. 运行测试确保通过
4. 提交代码前运行完整测试套件

## 常见问题

### 常见问题
1. 找不到 `lexer_tests.exe`
   - 确保已经成功执行了 cmake 构建命令
   - 检查是否在正确的目录下查找

2. 测试运行失败
   - 检查编译是否有错误
   - 查看具体的测试失败信息
   - 使用调试器逐步检查代码执行

3. 中文显示问题
   - 确保源文件使用 UTF-8 编码
   - 检查 Visual Studio 的文本编码设置

### Windows相关
1. 如果CMake无法找到编译器，尝试：
```bash
cmake .. -G "Visual Studio 17 2022" -A x64
```

2. 确保Visual Studio安装了C++开发组件。

### Linux相关
1. 如果遇到编译器版本过低的问题，可以安装更新的GCC：
```bash
sudo apt install -y gcc-11 g++-11
export CC=gcc-11
export CXX=g++-11
```

### 网络问题
如果在中国大陆遇到Google Test下载失败的问题：
1. 使用代理
2. 或手动下载Google Test源码，修改CMakeLists.txt中的GIT_REPOSITORY路径为本地路径

## 开发环境推荐
- Windows: Visual Studio
- Linux: VSCode + C/C++插件
- macOS: VSCode + C/C++插件 或 CLion

如遇到其他问题，请查看项目的Issue页面或创建新的Issue。
