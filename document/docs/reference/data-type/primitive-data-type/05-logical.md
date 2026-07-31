---
sidebar_label: Logical Type
---

# Logical Type

:::danger[Is it worth changing to yes, no? (but JSON conversion might be inconsistent)]
- Answer: not worth it.
- Conclusion: use true, false
:::

## 🐳Type Overview {/* #intro */}

|   Type    | Description                                                  |
| :-------: | ----------------------------------------------------------- |
|  `bool`   | Two-valued logic (boolean type); value is `true` or `false`, default is `false` |
| `tribool` | Three-valued logic (ternary type); value is `true`, `false`, or `unset`, default is `unset` |


:::warning[TODO]
Need to confirm whether to design a default value (see whether we can implement Java-style "not allowed to use before initialization").
:::

## Boolean Type {/* #anthor-boolean-type */}

### 🏅Syntax Examples {/* #syntax-example */}

- Define a boolean:

```collie
bool a = true;
bool b = !a; // result: false

a == false // result: false
a == true  // result: true
```

- The multi-way operator and its shorthand form:

```collie
bool a;
// The logic for assigning a value to a is omitted here

// Shorthand form
a ? 1 : 2
// when a equals true,  result: 1
// when a equals false, result: 2

// Original form
a ==? true: 1, false: 2
// when a equals true,  result: 1
// when a equals false, result: 2

a ==? false: 1, 2
// when a equals false,       result: 1
// otherwise (a equals true), result: 2
```

## Tri-State Boolean Type {/* #anthor-tri-state-boolean-type */}

The tri-state boolean type extends the boolean type by adding an `unset` option. Negating `unset` still yields `unset`; all other characteristics are the same as the boolean type.

> Reference: Kleene three-valued logic

### Logical Operators (Kleene Three-Valued Logic) {/* #anthor-kleene-logic */}

When `tribool` participates in the logical operators (`!`, `&&`, `||`), Kleene three-valued logic (K3) is used: `unset` means "unknown"; when the result can be uniquely determined by the known side, the known value is taken, otherwise the result is `unset`. When either operand is a `tribool`, the operation is performed over the three-valued domain, and the result type is `tribool` (a `bool` operand participates as `true`/`false`).

| `a` | `!a` |
| :---: | :---: |
| `true` | `false` |
| `false` | `true` |
| `unset` | `unset` |

| `a && b` | `b = true` | `b = false` | `b = unset` |
| :---: | :---: | :---: | :---: |
| **`a = true`** | `true` | `false` | `unset` |
| **`a = false`** | `false` | `false` | `false` |
| **`a = unset`** | `unset` | `false` | `unset` |

> Mnemonic: if any operand is false, the result is false; if there is no false but there is an unset, the result is unset; if all are true, the result is true.

| `a \|\| b` | `b = true` | `b = false` | `b = unset` |
| :---: | :---: | :---: | :---: |
| **`a = true`** | `true` | `true` | `true` |
| **`a = false`** | `true` | `false` | `unset` |
| **`a = unset`** | `true` | `unset` | `unset` |

> Mnemonic: if any operand is true, the result is true; if there is no true but there is an unset, the result is unset; if all are false, the result is false.

Short-circuit rules are unchanged: `false && …` yields `false` directly, `true || …` yields `true` directly, and the right-hand side is not evaluated; when the left-hand side is `unset`, short-circuiting is not possible and the right-hand side must be evaluated (`unset && false` yields `false`, `unset || true` yields `true`).

### tribool in Conditional Statements {/* #anthor-tribool-in-condition */}

Condition positions such as `if` / `while` require the **`bool`** type; `tribool` is not implicitly converted, so writing `if (t)` directly is a compile error. You must use one of the following forms to make the tri-state meaning explicit:

```collie
tribool t = unset;

// if (t) { ... }   // ❌ Compile error: the condition must be a bool

// Form 1: built-in predicate properties that return bool
if (t.isTrue())  { /* ... */ }
if (t.isFalse()) { /* ... */ }
if (t.isUnset()) { /* ... */ }

// Form 2: explicit comparison (the result of ==/!= is a bool)
if (t == true)  { /* ... */ }
if (t == false) { /* ... */ }
if (t == unset) { /* ... */ }
```

`isTrue` / `isFalse` / `isUnset` are built-in properties of the `tribool` type. Note that the result of a logical operation (such as `a && b`) is still of type `tribool`, so it must also be evaluated using the forms above before being used as a condition.

### 🏅Syntax Examples

- Define a tri-state boolean:

```collie
tribool a = unset;
```

- Comparing boolean and tri-state boolean types:

```collie
tribool a, bool b = unset, false;
a == b  // result: false
a == !b // result: false

unset == true   // result: false
unset == false  // result: false
unset == !unset // result: true

!true  // result: false
!false // result: true
!unset // result: unset
```

- The multi-way operator:

```collie
tribool a;
// The logic for assigning a value to a is omitted here

a ==? unset: 1, true: 2, false: 3
// when a equals unset, result: 1
// when a equals true,  result: 2
// when a equals false, result: 3

a ==? unset, true: 2, false: 3
// when a equals true or unset,  result: 2
// when a equals false,          result: 3

a ==? unset, true: 1, 2
// when a equals unset or true, result: 1
// otherwise (a equals false),  result: 2

a ==? unset: 1, 2
// when a equals unset,         result: 1
// when a equals false or true, result: 2

a ==? 2, unset: 1
// when a equals unset,                result: 1
// otherwise (a equals false or true), result: 2

// a ==? unset, true: 2
// ❌ Not allowed: the false branch is missing

/* If the expression is long, the recommended format is as follows.
// Note: if both value1 and value3 equal hereIsAVeryLongParamName, the result of the first matching condition (expression 1) is returned.
object a = hereIsAVeryLongParamName ==?
                value1, value2: {
                    expression 1
                },
                value3: {
                    expression 2
                },
                expression 3
 */
```

:::info[Applicable scope and exhaustiveness rules of `==?`]
The `==?` multi-way matching operator is not limited to logical types: any value that can be compared with `==` (`number`, `string`, etc.) can be matched, acting like a switch-style expression (see the long example above). Branches must cover all possibilities:

- **tribool**: exhaust the three states `true` / `false` / `unset`, or provide a default branch (a trailing branch without `value:`);
- **bool**: exhaust the two states `true` / `false`, or provide a default branch;
- **other types** (`number`, `string`, etc., whose value domain cannot be enumerated): a default branch is **required**.
:::

- Shorthand form of the multi-way operator:

```collie
tribool a;
// The logic for assigning a value to a is omitted here

a ? 1 : 2 : 3
// when a equals true,  result: 1
// when a equals false, result: 2
// when a equals unset, result: 3

a ? 1 : 2
// when a equals true,           result: 1
// when a equals false or unset, result: 2

/* If the expression is long, the recommended format is as follows.
object a = hereIsAVeryLongParamName
               ? expression 1
               : expression 2
               [: expression 3]
 */
```
