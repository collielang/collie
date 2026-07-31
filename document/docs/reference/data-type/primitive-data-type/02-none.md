---
sidebar_label: None Type
---

# None Type

## 🐳Type Overview {/* #intro */}

The none type is also an object.

|  Type  | Description   |
| :----: | ------------- |
| `none` | The none type |

:::warning[Note]
Collie's none type differs somewhat from other languages: `null` is a special object that also has its own properties.
:::

## 🏅Base Methods {/* #method */}

| Method                                    | Result | Description                 |
| ----------------------------------------- | ------ | --------------------------- |
| null.toString()                           | "null" | Converts the object to a string |
| null.valueEquals(object? anotherNull)     | true   |                             |
| null.referenceEquals(object? anotherNull) | true   |                             |

## 🏅Syntax Examples {/* #syntax-example */}

- Define a none value:

```collie
none nullObject = null; // All objects of the none type are null (equal by both reference and value), so this can also be written simply as: none nullObject;

if (nullObject.isNull()) { // or: if (nullObject == null) {
    print("Life is but an empty dream");
}
```
