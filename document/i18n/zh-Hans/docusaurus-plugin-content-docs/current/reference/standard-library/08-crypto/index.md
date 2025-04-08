---
sidebar_label: Collie.Crypto（加解密包）
---

# Collie.Crypto（加解密包）

```collie
using Collie.Crypto;
```

TODO

```
Collie.Crypto/
├── algorithms/
│   ├── symmetric/      -- 对称加密
│   │   ├── aes.collie
│   │   ├── chacha20.collie
│   ├── asymmetric/     -- 非对称加密
│   │   ├── rsa.collie
│   │   ├── ecc.collie
│   └── hash/           -- 哈希与HMAC
│       ├── sha.collie        -- 实现 SHA-1（废弃）、SHA-2（SHA-224/256/384/512）
│       ├── sha3.collie       -- 实现 SHA-3（如 SHA3-256、SHA3-512）
│       ├── md5.collie        -- 带废弃警告的 MD5
│       └── hmac.collie       -- 支持 HMAC-SHA/HMAC-SM3
├── utils/
│   ├── random.collie   -- 安全随机数生成
│   ├── keygen.collie   -- 密钥对生成
│   └── encoder.collie  -- 编码工具：Base64/Hex 编解码
└── types/
    ├── key.collie      -- 密钥对象：封装 PEM/DER 格式
    └── cipher.collie   -- 加密模式配置：如填充方式、IV
```
