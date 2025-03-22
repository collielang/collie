---
sidebar_position: 2
sidebar_label: Compile And Run
---

# Compilation and Execution Guide

This document explains how to compile and run the project on different operating systems.

## Environment Requirements
- CMake 3.10 or higher
- Visual Studio 2019/2022 (Windows)
- or GCC/Clang (Linux/macOS)

## Compilation and Execution on Windows

### Prerequisites
- [Visual Studio](https://visualstudio.microsoft.com/downloads/) (2017 or later, Community edition is sufficient)
  - Select the "Desktop development with C++" workload during installation
- [CMake](https://cmake.org/download/) (3.10 or higher)
  - Check "Add CMake to system PATH" during installation

### Directory

All the following operations are performed in the `compiler` directory.

```bash
cd compiler
```

### Compilation Steps

Choose one of the following methods.

1. Generate Visual Studio project files:
   ```bash
   # Create and enter the build directory
   mkdir build
   cd build

   # Generate Visual Studio project files
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```

   :::danger
   Note: In China, there may be network issues preventing the download of the googletest framework
   :::

#### Using Visual Studio
2. Open the solution:
   - Open `build/collie_compiler.sln` with Visual Studio
   - In the Solution Explorer, `lexer_tests` is already set as the startup project
   - Right-click the solution in the Solution Explorer
   - Select "Build Solution"

3. Run tests:
   - Press F5 or click "Debug -> Start Debugging"
   - Or press Ctrl+F5 to run without the debugger
   - Test results will be displayed in the Output window

#### Using Command Line
2. Compile:
   ```bash
   cmake --build . --config Debug
   # or
   cmake --build . --config Release
   ```

3. Run tests:
   ```bash
   # Test
   ctest -C Debug
   ```
   ```bash
   # Debug mode
   .\Debug\lexer_tests.exe
   # or Release mode
   .\Release\lexer_tests.exe
   ```

## Compilation and Execution on Linux (Ubuntu)

### Prerequisites
```bash
# Install necessary tools
sudo apt update
sudo apt install -y build-essential cmake git
```

### Compilation Steps
1. Create and enter the build directory:
```bash
mkdir build
cd build
```

2. Generate build files and compile:
```bash
cmake ..
make
```

3. Run tests:
```bash
ctest
```

## Compilation and Execution on macOS

### Prerequisites
1. Install Xcode Command Line Tools:
```bash
xcode-select --install
```

2. Install Homebrew (if not already installed):
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

3. Install CMake:
```bash
brew install cmake
```

### Compilation Steps
1. Create and enter the build directory:
```bash
mkdir build
cd build
```

2. Generate build files and compile:
```bash
cmake ..
make
```

3. Run tests:
```bash
ctest
```

## Build Output Locations

### Windows
- Debug mode:
  - Test program: `build/Debug/lexer_tests.exe`
  - Compiler library: `build/Debug/collie_compiler.lib`

- Release mode:
  - Test program: `build/Release/lexer_tests.exe`
  - Compiler library: `build/Release/collie_compiler.lib`

## Test Output Explanation
When running the test program, you will see output similar to the following:
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

- `[  PASSED  ]` indicates that the test passed
- `[  FAILED  ]` indicates that the test failed
- Failed tests will display detailed failure information, including expected and actual values

## Debugging
1. In Visual Studio:
   - Set breakpoints in the code
   - Press F5 to start debugging
   - Use the debug toolbar or shortcuts to control program execution

2. Adding your own tests:
   - Add new test cases in `tests/lexer_test.cpp`
   - Follow the existing test format:
     ```cpp
     TEST(LexerTest, YourTestName) {
         std::string source = "your test code";
         Lexer lexer(source);
         // ... your test logic ...
     }
     ```

## Development Workflow
1. Implement new features
2. Add unit tests
3. Run tests to ensure they pass
4. Run the full test suite before committing code

## Frequently Asked Questions

### Common Issues
1. `lexer_tests.exe` not found
   - Ensure that the cmake build command was successfully executed
   - Check if you are looking in the correct directory

2. Test failures
   - Check if there were any compilation errors
   - Review the specific test failure information
   - Use the debugger to step through the code execution

3. Chinese display issues
   - Ensure that source files use UTF-8 encoding
   - Check Visual Studio's text encoding settings

### Windows-Specific
1. If CMake cannot find the compiler, try:
```bash
cmake .. -G "Visual Studio 17 2022" -A x64
```

2. Ensure that the C++ development components are installed in Visual Studio.

### Linux-Specific
1. If you encounter issues with an outdated compiler, you can install a newer version of GCC:
```bash
sudo apt install -y gcc-11 g++-11
export CC=gcc-11
export CXX=g++-11
```

### Network Issues
If you encounter issues downloading Google Test in Mainland China:
1. Use a proxy
2. Or manually download the Google Test source code and modify the GIT_REPOSITORY path in CMakeLists.txt to a local path

## Recommended Development Environments
- Windows: Visual Studio
- Linux: VSCode + C/C++ plugin
- macOS: VSCode + C/C++ plugin or CLion

If you encounter other issues, please check the project's Issue page or create a new Issue.
