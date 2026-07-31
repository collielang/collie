---
sidebar_label: Base Type
---

# Base Type

## 🐳Type Overview {/* #intro */}

The base type is the foundation of all types; every type inherits from it.

|   Type   | Description   |
| :------: | ------------- |
| `object` | The base type |

:::tip[Analogy with other languages]

The base type is similar to Java's ancestor class: every class inherits, directly or indirectly, from the `Object` class.

The base type provides some very important and commonly used methods that are frequently used during development.

:::

## 🏅Base Methods {/* #method */}

| Method                                                       | Description                                                  |
| ------------------------------------------------------------ | ------------------------------------------------------------ |
| object.clone(bool deep)                                      | Clones the object. `deep`: whether to perform a deep copy (recursively copying its contents) or a shallow copy |
| object.toString()                                            | Converts the object to a string                             |
| object.valueEquals(object anotherObject)                     | Compares whether the values of two objects are equal        |
| object.referenceEquals(object anotherObject)                 | Compares whether two objects are the same object. Equivalent to: `object == anotherObject` |
| object.isNull()                                              | Determines whether an object is null                        |
| object.isProxy()                                             | Determines whether an object is a proxy object              |
| object.getProxyTarget()<br />object.getProxyTarget(bool: deep) | Gets the original object of a proxy object. For nested proxy objects, if `deep` is true, recursively retrieves the innermost one |
