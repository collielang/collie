---
title: 其他语言实现 Other Language Impliments
---

<head>
  <meta name="robots" content="noindex, nofollow" />
</head>

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

export const expand = false

<!--

### {PlaceHolder}

>
- Website:
- Documentation:
- GitHub Repo:

<details open={expand}>
<summary>{PlaceHolder} Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    ####

    >
    - Website:
    - Documentation:
    - GitHub Repo:
  </TabItem>
  <TabItem value="Build System" label="Build System">
  </TabItem>
</Tabs>
</details>
 -->

# 其他语言实现

## 主流编程语言

<details open={false}>
<summary>主流编程语言派系</summary>

根据编程范式和设计哲学的演进关系，主流的编程语言可以划分为以下派系：

---

**一、C语言系**

**核心特征**：底层控制、高性能、系统级开发
- **C语言**：结构化编程的基石，广泛应用于操作系统（如Linux内核）、嵌入式系统等领域
- **C++**：在C基础上引入面向对象特性，支持多范式编程，用于游戏引擎（如Unreal）、高频交易系统等
- **Rust**：继承C/C++的底层控制能力，但通过所有权系统实现内存安全，成为系统编程的新标杆（如操作系统Redox）

---

**二、Java系**

**核心特征**：JVM生态、跨平台、企业级应用
- **Java**：面向对象语言标杆，Android原生开发语言（2019年前），企业级后台首选（如Spring框架）
- **Kotlin**：完全兼容Java的现代语言，2017年被谷歌定为Android官方开发语言，语法更简洁
- **Scala**：融合面向对象与函数式编程，用于大数据处理（如Apache Spark）

---

**三、C#系**

**核心特征**：.NET框架、微软生态、跨平台演进
- **C#**：语法与Java高度相似，广泛用于Windows桌面应用（WPF）、游戏开发（Unity引擎）
- **F#**：.NET平台上的函数式语言，适合金融建模和数据分析

---

**四、脚本语言系**

**核心特征**：动态类型、解释执行、快速开发
- **Python**：多范式语言，主导AI（TensorFlow）、数据科学（Pandas）领域
- **Ruby**：以"程序员友好"著称，Ruby on Rails框架推动Web开发革新
- **JavaScript**：浏览器端统治级语言，Node.js使其拓展至服务端（如Express框架）
- **PHP**：专为Web开发设计，WordPress等CMS系统的核心语言

---

**五、函数式语言系**

**核心特征**：不可变数据、高阶函数、声明式风格
- **Haskell**：纯函数式语言，学术研究与金融领域应用广泛
- **Erlang/Elixir**：基于Actor模型的并发语言，支撑电信级系统（如WhatsApp消息系统）
- **Clojure**：Lisp方言运行于JVM，适合高并发场景

---

**六、新生代系统语言**

**核心特征**：内存安全、并发友好、现代化语法
- **Go**：谷歌开发的C系变种，内置协程（goroutine）简化并发，云原生基础设施主流语言（如Docker/Kubernetes）
- **Swift**：苹果推出的Obj-C替代品，语法接近Python，用于iOS/macOS开发生态

---

**七、特殊领域语言**

- **SQL**：声明式数据库查询语言
- **R**：统计分析与可视化专精
- **MATLAB**：工程计算与仿真领域标准工具

---

**范式交叉现象**：现代语言多支持混合范式，例如：
- Python可同时用于OOP和函数式编程
- JavaScript ES6+引入类语法和箭头函数
- Rust在系统语言中引入函数式特性

</details>

### Python

- Website: https://www.python.org/
- GitHub Repo: https://github.com/python/cpython

<details open={expand}>
<summary>Python Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### pip

    > The Python package installer
    - Documentation: https://pip.pypa.io/
    - GitHub Repo: https://github.com/pypa/pip
  </TabItem>
</Tabs>
</details>



### Java

- Website: https://www.java.com/, https://dev.java/
- JDK 24 Documentation: https://docs.oracle.com/en/java/javase/24/

<details open={expand}>
<summary>Java Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    Repository: https://mvnrepository.com/

    #### Maven

    > Maven, a Yiddish word *meaning accumulator of knowledge* ([refer](https://maven.apache.org/what-is-maven.html))
    - Website: https://maven.apache.org/
    - GitHub Repo: https://github.com/apache/maven-sources

    #### Gradle

    > "Gradle"这个词来自于英语单词"gradual"（渐进）和"ale"（一种酒类）的合成，因此形成了"Gradle"。这个名称的灵感源于Gradle构建工具的设计理念，即通过渐进式的方式来构建和管理项目。
    - Website: https://gradle.org/
    - GitHub Repo: https://github.com/gradle/gradle
  </TabItem>
  <TabItem value="JDK" label="JDK">
    #### OpenJDK

    > Java Development Kit
    - GitHub Repo: https://github.com/openjdk/jdk
    - JDK main-line development: https://openjdk.org/projects/jdk
  </TabItem>
</Tabs>
</details>



#### Kotlin

>
- Website: https://kotlinlang.org/
- Documentation: https://kotlinlang.org/docs/home.html
- GitHub Repo: https://github.com/JetBrains/kotlin



### Go

> *The Go programming language.*
- Website: https://go.dev/
- GitHub Repo: https://github.com/golang/go
- Standard library: https://pkg.go.dev/std

<details open={expand}>
<summary>Go Lang Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### Go Modules

    - Documentation:
      - Using Go Modules: https://go.dev/blog/using-go-modules
      - Managing dependencies: https://go.dev/doc/modules/managing-dependencies
  </TabItem>
</Tabs>
</details>



### JavaScript

- Documentation: https://developer.mozilla.org/en-US/docs/Web/JavaScript

<details open={expand}>
<summary>JavaScript Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    Repository: https://www.npmjs.com/

    #### npm

    > The package manager for JavaScript
    - Website: https://www.npmjs.com/
    - Documentation: https://docs.npmjs.com/
    - GitHub Repo: https://github.com/npm/cli

    #### yarn

    > Fast, reliable, and secure dependency management.
    - Website: https://yarnpkg.com/
    - Documentation: https://yarnpkg.com/getting-started
    - GitHub Repo: https://github.com/yarnpkg/berry

    #### pnpm

    > Fast, disk space efficient package manager
    - Website: https://pnpm.io/
    - Documentation: https://pnpm.io/motivation
    - GitHub Repo: https://github.com/pnpm/pnpm

    #### cnpm

    > Private NPM Registry for self-host.
    - GitHub Repo: https://github.com/cnpm/cnpmcore
  </TabItem>
  <TabItem value="Engine" label="Engine">
    #### QuickJS

    - Website: https://bellard.org/quickjs/
    - GitHub Repo: https://github.com/bellard/quickjs

    #### QuickJS-NG

    - Website: https://quickjs-ng.github.io/quickjs/
    - GitHub Repo: https://github.com/quickjs-ng/quickjs
  </TabItem>
  <TabItem value="Runtime" label="Runtime">
    #### Node.js

    - Website: https://nodejs.org/
    - GitHub Repo: https://github.com/nodejs/node

    #### Bun

    - Website: https://bun.sh/
    - GitHub Repo: https://github.com/oven-sh/bun

    #### Deno

    - Website: https://deno.com/
    - GitHub Repo: https://github.com/denoland/deno/
  </TabItem>
  <TabItem value="Framework" label="Framework">
    #### Vue

    - Website: https://vuejs.org/
    - GitHub Repo: https://github.com/vuejs/
  </TabItem>
  <TabItem value="Type" label="Type">
    #### TypeScript

    > TypeScript is JavaScript with syntax for types.
    - Website: https://www.typescriptlang.org/
    - Documentation: https://www.typescriptlang.org/docs/
    - GitHub Repo: https://github.com/microsoft/TypeScript
  </TabItem>
</Tabs>
</details>



### .NET Runtime (C#)

> *.NET is a cross-platform runtime for cloud, mobile, desktop, and IoT apps.*
- Documentation: docs.microsoft.com/dotnet/core/
- GitHub Repo: https://github.com/dotnet/runtime

<details open={expand}>
<summary>.NET Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### NuGet

    > NuGet is the package manager for .NET.
    - Website: https://www.nuget.org/
    - Documentation: https://docs.microsoft.com/nuget/
    - GitHub Repo: https://github.com/NuGet/NuGetGallery
  </TabItem>
  <TabItem value="Build System" label="Build System">
    #### MSBuild

    > The Microsoft Build Engine (MSBuild) is the build platform for .NET and Visual Studio.
    - Documentation: https://learn.microsoft.com/visualstudio/msbuild/msbuild
    - GitLab Repo: https://github.com/dotnet/msbuild
  </TabItem>
</Tabs>
</details>



### Dart

> Dart 是一个易用、可移植且高效的语言，适用于在全平台开发高质量的应用程序。
- Website: https://dart.dev/, https://dart.cn/
- Documentation: https://dart.cn/language/
- GitHub Repo: https://github.com/dart-lang/

<details open={expand}>
<summary>Dart Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### package

    > Dart 生态系统使用 package 来管理共享软件
    - Website: https://dart.cn/tools/pub/packages/
    - Documentation: https://dart.cn/tools/pub/cmd/
  </TabItem>
</Tabs>
</details>



### PHP

> *The PHP Interpreter*
- Website: https://www.php.net/
- Documentation: https://www.php.net/docs.php
- GitHub Repo: https://github.com/php

<details open={expand}>
<summary>PHP Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### Composer

    > A Dependency Manager for PHP
    - Website: https://github.com/composer/composer
    - Documentation: https://getcomposer.org/doc/
    - GitHub Repo: https://github.com/composer/composer
  </TabItem>
</Tabs>
</details>



### Rust

> Empowering everyone to build reliable and efficient software.
- Website: https://www.rust-lang.org/
- Documentation: https://www.rust-lang.org/learn
- Tools: https://www.rust-lang.org/tools
- GitHub Repo: https://github.com/rust-lang/rust

<details open={expand}>
<summary>Rust Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### Cargo

    > The Rust package manager
    - Documentation: https://doc.rust-lang.org/cargo
    - GitHub Repo: https://github.com/rust-lang/cargo
  </TabItem>
</Tabs>
</details>



### C & C++

<details open={expand}>
<summary>C language family Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### Conan

    > Conan - The open-source C and C++ package manager
    - Website: https://conan.io/
    - Documentation: https://docs.conan.io/
    - GitHub Repo: https://github.com/conan-io/conan

    #### vcpkg

    > C++ Library Manager for Windows, Linux, and MacOS
    > vcpkg is a free and open-source C/C++ package manager maintained by Microsoft and the C++ community.
    - GitHub Repo: https://github.com/microsoft/vcpkg

    #### Cabin

    > C++ package manager and build system
    - Website: https://cabinpkg.com/
    - Documentation: https://docs.cabinpkg.com/
    - GitHub Repo: https://github.com/cabinpkg/cabin

    #### clib

    > Package manager for the C programming language.
    - GitHub Repo: https://github.com/clibs/clib
  </TabItem>
  <TabItem value="Compiler" label="Compiler">
    #### MSVC (Microsoft Visual C++)

    > 由微软开发，主要用于Windows平台应用程序的开发。Visual Studio系列IDE默认集成了该编译器
    - Platfrom: Windows

    #### GCC (GNU Compiler Collection)

    > 开源编译器，支持多种平台，Linux下C++开发一般默认会使用此编译器
    - Homepage: https://gcc.gnu.org/
    - Platfrom: Windows, Linux, macOS

    #### Clang (Clang / Low Level Virtual Machine)

    > LLVM项目的一部分，提供高效的编译性能。macOS的XCode工具默认集成了此编译器
    - Homepage: https://clang.llvm.org/
    - Platfrom: Windows, Linux, macOS
  </TabItem>
  <TabItem value="Build System" label="Build System">
    #### CMake

    > CMake is a cross-platform, open-source build system generator.
    - Website: https://cmake.org/
    - Documentation: https://cmake.org/documentation/
    - GitLab Repo: https://gitlab.kitware.com/cmake/cmake
  </TabItem>
</Tabs>
</details>



### Ruby

> Ruby is an interpreted object-oriented programming language often used for web development. It also offers many scripting features to process plain text and serialized files, or manage system tasks. It is simple, straightforward, and extensible.

- Website: https://www.ruby-lang.org/
- Documentation: https://www.ruby-lang.org/zh_cn/documentation/
- GitHub Repo: https://github.com/ruby/ruby

<details open={expand}>
<summary>Ruby Ecosystem</summary>
<Tabs>
  <TabItem value="Dependency Management" label="Dependency Management" default>
    #### RubyGems

    > RubyGems.org 是 Ruby 社区的 Gem 托管服务。
    - Website: https://rubygems.org/
    - GitHub Repo: https://github.com/rubygems/rubygems.org
  </TabItem>
</Tabs>
</details>


## 小众编程语言

### 凹语言 (China)

- Documentation: https://wa-lang.org/
- GitHub Repo: https://github.com/wa-lang/wa/
- Gitee Repo: https://gitee.com/wa-lang/wa

### Key Lang (China)

> *目标是最精致的编程语言*
- Documentation: https://docs.subkey.top/guide/readme
- GitHub Repo: https://github.com/Bylx666/key-lang

### 凸语言 / tu-lang (China)

- GitHub Repo: https://github.com/tu-lang/tu

### 豫言 (China)

> *一款函数式中文编程语言*
- Website: https://yuyan-lang.github.io/yuyan/
- GitHub Repo: https://github.com/yuyan-lang/yuyan

### 洛书编程语言 / LOSU (China)

- Website: https://losu.tech/index-new/
- Documentation: https://losu.tech/wiki/readme.md
- Gitee Repo: https://gitee.com/chen-chaochen/lpk

### Z语言 (China)

> *Z语言是一门面向个人的、专注于学习和分享的编程语言。*
- Gitee Repo: https://gitee.com/z-lang/zc
- Z语言炼成记: https://gitee.com/z-lang/devlog

### Koral (China)

> Koral 是一个专注于效率的开源编程语言，它可以帮你轻松构建跨平台软件。
- Documentation: https://github.com/kulics/koral/blob/master/docs/book-zh/document.md
- GitHub Repo: https://github.com/kulics/koral

### Deeplang (China)

> Deeplang 语言是一种自制编程语言，面向IoT场景
- Website: https://deeplang.org/
- GitHub Repo: https://github.com/deeplang-org/deeplang

### Aya

> A proof assistant designed for formalizing math and type-directed programming.
- Website: https://www.aya-prover.org/
- GitHub Repo: https://github.com/aya-prover/aya-dev

### MoonBit (China)

> MoonBit 是一个端到端的编程语言工具链，用于云和边缘计算，使用 WebAssembly。
- Documentation: https://docs.moonbitlang.cn/
- GitHub Repo: https://github.com/moonbitlang

### 智锐 / Covariant (China)

> Covariant Script is an open source, cross-platform programming language.
- Website: https://covscript.org.cn/
- GitHub Repo: https://github.com/covscript/covscript

### Calcit (China)

> Indentation-based ClojureScript dialect in Rust and compiling to JavaScript ES Modules
- Website: https://calcit-lang.org/
- GitHub Repo: https://github.com/calcit-lang/calcit/



## 新兴编程语言

### 方舟编程语言 / ArkTS (China)

- Documentation:
  - ArkTS 语言介绍: https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/introduction-to-arkts
  - ArkTS（方舟编程语言）: https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V13/arkts-V13

### 仓颉 (China)

- Website: https://cangjie-lang.cn/
- Documentation: https://cangjie-lang.cn/docs
- Gitcode Repo: https://gitcode.com/Cangjie-TPC

### KCL

> KCL is an open-source constraint-based record & functional language mainly used in configuration and policy scenarios.
- Website: https://www.kcl-lang.io/
- Documentation: https://www.kcl-lang.io/docs/user_docs/getting-started/
- GitHub Repo: https://github.com/kcl-lang/kcl

### NASL (DSL) (China)

> *NASL 是网易 CodeWave 智能开发平台用于描述 Web 应用的领域特定语言*
- Website: https://nasl.codewave.163.com/



## 其他

### PLOC (Programming Language Open Community)

- Website: https://www.ploc.org.cn/ploc/
- Gitee Repo: https://gitee.com/ploc-org
- 国产编程语言蓝皮书: https://gitee.com/ploc-org/CNPL-2023
