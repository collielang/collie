
后置类型，方便类型推断
（语义重于语法）

但是 Collie 还是选择类型放在前面
更强调类型

关注下其他语言的好用、不好用的点，收集起来

```collie
if (obj is Type) {
    // 如果 obj 是 Type 类型，则可以直接使用 Type 类型的属性和方法
} else {
    // 处理其他类型情况
}
```

```kotlin
// kotlin 语法
    for (i in 1..4 step 2) print(i) // 输出“13”
    for (i in 4 downTo 1 step 2) print(i) // 输出“42”
    for (i in 1 until 4) {   // i in [1, 4) 排除了 4
```

枚举


语法层面支持类似 Java 的 @NotNull @Nullable 等注解

考虑数据判空逻辑（比如写个接口，有些参数没有传入怎么处理）

try-with-resource
(想一想有没有更好的方式写try-catch, 现在的 Java 和 Js 等写的异常处理都太冗长了)

flomo中记录的点子


synchronized实现可以参考：
https://zhuanlan.zhihu.com/p/336248650
https://blog.csdn.net/weixin_44215363/article/details/109782776

Map变种：多key map
参考：
https://github.com/apache/commons-collections/blob/3d1c985232d3fe8fff803379b3dae55f367d7163/src/main/java/org/apache/commons/collections4/map/MultiKeyMap.java
https://www.techiedelight.com/zh/implement-map-with-multiple-keys-multikeymap-java/

多value map
```collie
map.put("key", "value1");
map.put("key", "value2");
map.get("key") => [ "value1", "value2" ]
```

```collie
for-if ... else

boolean isTruck;
for (DispatchScene dispatchScene : dispatchSceneList) {
    if (staffId.equals(dispatchScene.getTruckStaffId())) {
        isTruck = true;
        break;
    }
} else {
    isTruck = false;
}
```
