---
sidebar_position: 4
sidebar_label: 函数
---

# 函数定义与调用

## 多返回类型的函数

- 支持多个返回值，编译时隐式转换为元组

```collie
public int, string getAge() {
   return 18, "Alice";
}
```

## 元组支持

- 元组用于返回多个值或传递多个参数

```collie
Tuple a = (name: "Alice", age: 18);
string name = a.name;
int age = a.age;
```
