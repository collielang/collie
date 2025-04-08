---
sidebar_label: Collie.Id（唯一ID生成器包）
---

# Collie.Id（唯一ID生成器包）

```collie
using Collie.Id;
```

TODO

```
id/                # 顶层包
├── uuid/          # UUID生成与解析（v1/v4/v7等）
├── snowflake/     # SnowflakeID生成器（含数据中心/机器ID配置）
├── ulid/          # ULID生成器（时间有序、Base32编码）
└── core/          # 公共接口（如ID编码、解码、校验）
```
