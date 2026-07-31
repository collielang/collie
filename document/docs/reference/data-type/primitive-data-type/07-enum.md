---
sidebar_label: Enum Type
---

# Enum Type

## 🐳Type Overview {/* #intro */}

TODO

Basic usage

```collie
enum Season
{
    Spring,
    Summer,
    Autumn,
    Winter,
}
```

Enum properties

```collie
enum Season(string name)
{
    Spring(name='Spring'),
    Summer(name='Summer'),
    Autumn(name='Autumn'),
    Winter(name='Winter'),
}

Season season = Season.Spring;
season.name // Spring
```

Enum properties + enum values

```collie
enum Season(string name) : string
{
    Spring(name='Spring') = 'spring',
    Summer(name='Summer') = 'summer',
    Autumn(name='Autumn') = 'autumn',
    Winter(name='Winter') = 'winter',
}

Season season = Season.Spring;
season === 'spring' // true
```
