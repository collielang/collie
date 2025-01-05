# 牧羊犬编程语言 Collie Lang

*"The first step is always the hardest."*

## 快速开始

Collie 是一个静态类型的编程语言，旨在提供简洁的语法和强大的类型系统。

## 语言特性

- 静态类型系统
- 函数和过程
- 控制流语句（if-else, while, for）
- 块作用域
- 基本数据类型（number, string, char, bool）

- [语法](LanguageGrammer.md)



## 内部实现

- [代码提交规范](Specification/CodeCommitSpecification.md)

- [候选名称](CandidateNames.md)

## 开发与贡献

- [编译和运行指南](Internal/CompileAndRun.md)

## 其他

- [实用工具](Others/HelpfulTools.md)

- [其他语言实现](Others/OtherLanguageImpliments.md)

## 示例代码
```collie
number add(number x, number y) {
    return x + y;
}

number main() {
    number result = add(40, 2);
    if (result > 0) {
        print(result);
    }
    return 0;
}
```

## 项目结构
```
CollieLang/
├── compiler/           # 编译器源代码
│   ├── lexer/         # 词法分析器
│   ├── parser/        # 语法分析器
│   └── tests/         # 单元测试
├── docs/              # 文档
│   ├── Internal/      # 内部开发文档
│   └── README.md      # 项目说明
└── CMakeLists.txt     # CMake 配置文件
```

## 项目状态
当前已完成：
- [x] 词法分析器
- [x] 语法分析器
- [ ] 语义分析器
- [ ] 中间代码生成
- [ ] 代码优化器
- [ ] 目标代码生成
