---
sidebar_position: 1
sidebar_label: 基类型（Base Type）
---

# 基类型（Base Type）

:::info

基类型为[不可变类型](./#immutable-data-type)。

:::

## 🐳类型简介 {#intro}

基类型是所有类型的基础，所有类型都会继承自它。

|   类型   | 描述   |
| :------: | ------ |
| `object` | 基类型 |

:::tip 与其他语言类比

基类型类似 Java 的祖先类，所有的类都直接或间接地继承自Object类。

基类型提供了一些非常重要和常用的方法，这些方法在开发中经常被使用。

:::

## 🏅基础方法 {#method}

| 方法                                                         | 描述                                                         |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| object.clone(bool deep)                                      | 克隆对象，deep：指定是深拷贝（递归拷贝其内容）还是浅拷贝     |
| object.toString()                                            | 将对象转为字符串                                             |
| object.valueEquals(object anotherObject)                     | 比较两个对象的值是否相等                                     |
| object.referenceEquals(object anotherObject)                 | 比较两个对象是否是同一个对象。等价于：`object == anotherObject` |
| object.isNull()                                              | 判断一个对象是否是 null                                      |
| object.isProxy()                                             | 判断一个对象是否是代理对象                                   |
| object.getProxyTarget()<br />object.getProxyTarget(bool: deep) | 获取代理对象的原始对象。对于嵌套的 proxy 对象，如果 deep 为 true，则递归获取到最内层的 |
