---
sidebar_label: 枚举类型（Enum Type）
---

# 枚举类型（Enum Type）

## 🐳类型简介 {/* #intro */}

TODO

基础使用

```collie
enum Season
{
    Spring,
    Summer,
    Autumn,
    Winter,
}
```

枚举属性

```collie
enum Season(string name)
{
    Spring(name='春'),
    Summer(name='夏'),
    Autumn(name='秋'),
    Winter(name='冬'),
}

Season season = Season.Spring;
season.name // 春
```

枚举属性 + 枚举值

```collie
enum Season(string name) : string
{
    Spring(name='春') = 'spring',
    Summer(name='夏') = 'summer',
    Autumn(name='秋') = 'autumn',
    Winter(name='冬') = 'winter',
}

Season season = Season.Spring;
season === 'spring' // true
```
