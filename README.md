# rtos-hashtable

面向 ESP32 / FreeRTOS 的 header-only 哈希表（fht）。

纯 C99，无外部依赖，无内置锁。只需复制 `include/fht.h`，所有函数均为
`static inline`，可在多个 C 编译单元中包含，不需要单独编译库。
对外通过 `FHT_*` 宏调用，具体实现使用 `fht_*_impl` 内联函数。

参考 Redis dict 的链式冲突处理和双表渐进式 rehash，自行实现，并非移植
Redis 源码。支持自定义 malloc/free、键哈希/比较、可选对象释放回调。

## 内存设计

节点只存三个指针：`next / key / value`，不缓存 hash，不复制 key/value。
在常见 ESP32 32 位 ABI 下，节点为 **12 字节**，每个桶 **4 字节**，
表对象约 **64 字节**。上述数字按类型布局计算，未在 ESP32 设备上实测；
不包含分配器的块头、对齐浪费和用户对象。可用 `sizeof(fht_entry)`、
`sizeof(fht)` 在实际编译目标上确认。

正常运行时，表自身占用约为 `64 + 12 × 节点数 + 4 × 桶数` 字节。
例如 100 条记录、128 个桶约 1776 字节。rehash 期间同时保留旧、新桶数组，
**不复制节点**；从 128 桶扩展到 256 桶时，额外占用 1024 字节。

容量取不小于请求值的 2 的幂，最少 4 个桶。默认负载达到 1 后扩容；
不会自动缩容。删除释放节点，`clear` 释放所有节点和桶并保留配置。
`reserve` 可在启动阶段预分配桶，减少运行中的扩容。
它不会预分配节点；如需避免节点分配抖动，可接入自己的内存池分配器。

## 最小用法

```c
#include "fht.h"

void example(void) {
    fht h;
    fht_config c = FHT_CONFIG_DEFAULT(FHT_HASH_STRING, FHT_EQUAL_STRING);
    char key[] = "temperature";
    int temperature = 25;
    void *value;

    if (FHT_INIT(&h, &c) != FHT_OK) return;
    if (FHT_PUT(&h, key, &temperature) == FHT_ADDED) {
        if (FHT_GET(&h, "temperature", &value)) {
            int current = *(int *)value;
            (void)current;
        }
    }
    FHT_DESTROY(&h);
}
```

默认不释放 key/value，调用方保证对象在条目存活期间有效。
key 的哈希和比较结果必须保持不变。字符串辅助函数需要非 NULL、
以 NUL 结尾的字符串；通用接口是否允许 NULL key 由回调决定。
value 可以为 NULL，`get` 用返回值区分“找到 NULL”和“没找到”。

## 自定义 malloc/free

```c
static void *my_malloc(size_t size, void *ctx) {
    (void)ctx;
    return pvPortMalloc(size);
}
static void my_free(void *ptr, void *ctx) {
    (void)ctx;
    vPortFree(ptr);
}

/* 在包含 FreeRTOS.h 后使用 */
fht_config c = FHT_CONFIG_DEFAULT(FHT_HASH_STRING, FHT_EQUAL_STRING);
c.alloc = my_malloc;
c.free = my_free;
c.ctx = NULL; /* 可用于传递内存池或应用上下文 */
```

`alloc/free` 必须同时配置；省略时使用标准 `malloc/free`。它们管理节点
和桶数组，**不自动复制或分配 key/value**。分配器需提供普通 C 对象所需的对齐，
失败返回 NULL。`ctx` 会传给全部回调。
若 key/value 也由用户分配，可配置 `destroy_key/destroy_value` 回收它们。
ESP-IDF 的 `heap_caps_malloc` 示例见 `examples/esp_idf.c`，没有锁。

## 宏接口

所有操作统一通过 `FHT_INIT`、`FHT_PUT`、`FHT_GET` 等宏调用，宏只转发到
类型明确的 `static inline` 实现。每个参数在展开式中仅出现一次，允许使用
`ptr++` 等带副作用的表达式；不同参数之间的求值顺序仍遵循 C 函数调用规则，
不要在不同参数中同时修改同一个变量。返回值和原有 API 保持一致。

`FHT_HASH_STRING`、`FHT_EQUAL_STRING` 使用对象式宏，可直接作为函数指针
传入配置，也可以直接调用。操作宏本身不能取函数地址；需要回调的 API
继续通过配置结构中的函数指针提供，不依赖操作宏。

旧版 `fht_init(...)` 等调用形式保留为兼容宏。实现函数 `fht_*_impl` 和
数据结构的内部字段不作为稳定接口，应用应使用公开宏。
宏包装不增加运行时分配，也不改变节点或表对象的内存布局。

## API 与所有权

| API | 行为 |
| --- | --- |
| `FHT_INIT` | 初始化未初始化或已销毁对象；不分配内存 |
| `FHT_PUT` | 新增返回 `FHT_ADDED`；相同键替换返回 `FHT_REPLACED` |
| `FHT_GET` | 找到返回 1，未找到返回 0；输出借用的 value |
| `FHT_REMOVE` | 删除并调用配置的对象析构回调 |
| `FHT_TAKE` | 删除但不析构，将 key/value 所有权交给调用方 |
| `FHT_RESERVE` | 请求桶容量；正在迁移且请求更大容量时返回 `FHT_BUSY` |
| `FHT_REHASH_STEP` | 手动迁移，返回消耗的工作单元数 |
| `FHT_FOREACH` | 遍历两个表；回调返回非零时停止 |
| `FHT_SIZE / FHT_CAPACITY` | 条目数 / 目标表桶数 |
| `FHT_IS_REHASHING` | 是否处于双表迁移状态 |
| `FHT_CLEAR` | 释放全部存储，保留配置，可继续插入 |
| `FHT_DESTROY` | 释放并清零；再次使用前需要 init |

配置析构回调后，成功 put 的 key/value 由表负责释放；失败仍由调用方负责。
替换时同时采用新的 key 和 value，释放原来的对象；对应指针未变则不释放。
不同所有权对象之间不得共享同一地址，尤其不要把同一个需要释放的对象
同时作为 key/value。`take` 要求两个非 NULL 且不同的输出地址。

初始化失败后不要调用其他操作；已初始化的表不要再次直接 init，也不要
按值复制拥有动态内存的 `fht`。对象析构/分配/哈希/比较回调不得重入表操作。
析构回调需要能够处理传入的 NULL value。

## 渐进式 rehash 与运行时间

默认每次 put/get/remove/take 推进 2 个工作单元。一个单元只迁移一个节点，
或跳过一个空桶，因此长链不会在一个单元中全部搬迁。取消缓存 hash 后，
迁移节点会重新调用 hash 回调，这是用计算量换取更小节点内存的选择。

可设置 `c.rehash_work = 0` 关闭自动推进，随后在任务合适的位置调用
`FHT_REHASH_STEP(&h, budget)`。设置预算只是限制迁移工作量，不是硬实时保证：
新桶数组的分配/清零是同步的，查询可能遍历长链，回调和分配器耗时也不受限制。
哈希函数应满足“比较相等的键具有相同哈希值”。默认字符串 FNV-1a 适合可信键；
面对外部恶意输入，应使用带密钥的哈希，例如 SipHash。

迁移时，新条目进入新表；查找/删除查询两个表。迁移期间不会启动第二次扩容，
也不会为了插入而强制完成迁移，所以禁用自动推进却不手动推进会使链增长。
`foreach` 期间暂停迁移，可在回调中 get/size；修改、嵌套遍历和 clear/destroy
返回 `FHT_BUSY`，遍历结束后自动恢复。

没有内置 mutex、原子操作或 FreeRTOS 依赖。可直接用于单任务环境。
多个任务共享同一表时需要调用方串行化访问；**get 也可能推进 rehash**。
默认涉及堆分配，不应在 ISR 中调用；长操作不要放进 FreeRTOS 临界区。

## 验证

```sh
make test
make sanitize
# 也可使用 CMake
cmake -S . -B build-cmake -DFHT_SANITIZE=ON
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

测试覆盖多编译单元链接、字符串/整数键、NULL value、所有权转移与释放、
逐个注入分配失败、全碰撞长链、单节点迁移预算、遍历期间修改保护，
以及 20 万次对照模型随机操作。

当前工作区没有 CMake，已直接使用 GCC 验证 C99，分别运行优化构建和
AddressSanitizer/UndefinedBehaviorSanitizer 构建。当前运行环境的 ptrace
限制使 LeakSanitizer 无法运行，因此 sanitizer 验证时设置了
`ASAN_OPTIONS=detect_leaks=0`；自定义分配器测试另行核对未释放分配数。
尚未使用 ESP-IDF 工具链编译或在实际 ESP32 上验证。
