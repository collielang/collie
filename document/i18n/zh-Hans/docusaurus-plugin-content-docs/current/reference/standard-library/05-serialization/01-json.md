---
sidebar_label: .Json（JSON 处理模块）
---

# Collie.Serialization.Json（JSON 处理模块）

```collie
using Collie.Serialization.Json;
```

:::danger TODO
以下文档由 AI 生成，仅供参考，需要重写
:::

### 核心模块说明

提供高效的 **JSON 序列化与反序列化** 功能，支持：
- 对象 ↔ JSON 字符串双向转换
- 流式读写（`Utf8JsonReader/Writer`）
- 自定义命名策略（驼峰/蛇形命名等）
- 忽略空值、循环引用处理等配置

*示例代码：*
```collie
var user = new User { Name = "Alice", Age = 25 };
string json = Collie.Json.Serialize(user); // 序列化
User deserialized = Collie.Json.Deserialize<User>(json); // 反序列化
```

### 统一对比表

| 模块                      | 主要功能                          | 适用场景                     |
|---------------------------|-----------------------------------|----------------------------|
| `Collie.Json`             | 完整的序列化/反序列化             | 常规对象与 JSON 互转        |
| `Collie.Json.Serialization` | 高级序列化控制                    | 需要定制化输出的场景         |
| `Collie.Json.Parser`      | 低层解析/流式处理                 | 大数据或动态 JSON 处理      |

#### **`Collie.Json.Serialization`**

## JSON 序列化 (`Collie.Json.Serialization`)

专注于 **序列化行为控制**，提供：
- 自定义转换器（`JsonConverter<T>`）
- 日期格式、数字精度等精细化配置
- 多态类型处理（如接口/继承场景）

*典型用途：*
```collie
var options = new JsonSerializerOptions {
    PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
    WriteIndented = true
};
string json = Collie.Json.Serialization.Serialize(data, options);
```

#### **`Collie.Json.Parser`**

## JSON 解析器 (`Collie.Json.Parser`)

提供底层 **JSON 解析** 能力，适用于：
- 大型文档的流式解析（避免全量加载）
- 动态访问 JSON 节点（类似 `JToken`）
- 性能敏感场景（低内存分配）

*示例：*
```collie
using var reader = new JsonTextReader(fileStream);
while (reader.Read()) {
    if (reader.TokenType == JsonToken.PropertyName) {
        string key = reader.Value!.ToString();
    }
}
```
