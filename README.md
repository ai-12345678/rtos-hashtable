# rtos-hashtable

面向 ESP32 / FreeRTOS 的 header-only 字典（dict）。字典底层持有两张 hashtable，参考 Redis dict 的双表渐进式 rehash 设计。
正常情况下只使用 `tables[0]`；扩容时分配 `tables[1]` 的桶数组，逐步迁移节点。
查找和删除检查两张表，新条目进入新表。迁移完成后释放旧桶并切换回单表。

纯 C99，无外部依赖，无内置锁。只需复制 `include/dict.h`，所有函数均为
`static inline`，可在多个 C 编译单元中包含，不需要单独编译库。
对外通过 `DICT_*` 宏调用，具体实现使用 `dict_*_impl` 内联函数。

参考 Redis dict 的链式冲突处理和双表渐进式 rehash，自行实现，并非移植
Redis 源码。支持自定义 malloc/free、键哈希/比较、可选对象释放回调。

## 内存设计

节点只存三个指针：`next / key / value`，不缓存 hash，不复制 key/value。
在常见 ESP32 32 位 ABI 下，节点为 **12 字节**，每个桶 **4 字节**，
表对象约 **64 字节**。上述数字按类型布局计算，未在 ESP32 设备上实测；
不包含分配器的块头、对齐浪费和用户对象。可用 `sizeof(dict_entry)`、
`sizeof(dict)` 在实际编译目标上确认。

正常运行时，表自身占用约为 `64 + 12 × 节点数 + 4 × 桶数` 字节。
例如 100 条记录、128 个桶约 1776 字节。rehash 期间同时保留旧、新桶数组，
**不复制节点**；从 128 桶扩展到 256 桶时，额外占用 1024 字节。

容量取不小于请求值的 2 的幂，最少 4 个桶。默认负载达到 1 后扩容；
不会自动缩容。删除释放节点，`clear` 释放所有节点和桶并保留配置。
`reserve` 可在启动阶段预分配桶，减少运行中的扩容。
它不会预分配节点；如需避免节点分配抖动，可接入自己的内存池分配器。

## 最小用法

```c
#include "dict.h"
#include "dstr.h"

static void *example_malloc(size_t bytes, void *ctx) {
    (void)ctx;
    return malloc(bytes);
}
static void example_free(void *ptr, void *ctx) {
    (void)ctx;
    free(ptr);
}
static void example_destroy_string(void *ptr, void *ctx) {
    dstr_free((dstr)ptr, (const dstr_allocator *)ctx);
}

void example(void) {
    dict h;
    dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    dstr_allocator strings = {example_malloc, example_free, NULL};
    dstr key = NULL;
    int *temperature = NULL;
    void *value;
    dict_status status;

    c.alloc = example_malloc;
    c.free = example_free;
    c.ctx = &strings;
    c.destroy_key = example_destroy_string;
    c.destroy_value = example_free;

    if (DICT_INIT(&h, &c) != DICT_OK) return;
    key = dstr_new("temperature", &strings);
    temperature = (int *)c.alloc(sizeof(*temperature), c.ctx);
    if (!key || !temperature) goto done;
    *temperature = 25;
    status = DICT_PUT(&h, key, temperature);
    if (status != DICT_ADDED && status != DICT_REPLACED) goto done;
    key = NULL; /* 成功后交由 dict 的析构回调释放 */
    temperature = NULL;
    if (DICT_GET(&h, "temperature", &value)) {
        int current = *(int *)value;
        (void)current;
    }
done:
    dstr_free(key, &strings); /* 失败时调用方释放；成功时为 NULL */
    c.free(temperature, c.ctx);
    (void)DICT_DESTROY(&h);
}
```

本例的 key 使用 dstr，value 为动态分配的 int；都由自定义 allocator 分配，
成功 put 后由析构回调释放，
失败时仍由调用方释放。ESP-IDF 示例使用同样的所有权处理，适配
`heap_caps_malloc/heap_caps_free`。这些用户对象不计入 dict 内部节点/桶的全局统计。
如果不配置析构回调，默认不释放 key/value，调用方保证对象在条目存活期间有效。
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
dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
c.alloc = my_malloc;
c.free = my_free;
c.ctx = NULL; /* 可用于传递内存池或应用上下文 */
```

`alloc/free` 必须同时配置；省略时使用标准 `malloc/free`。它们管理节点
和桶数组，**不自动复制或分配 key/value**。分配器需提供普通 C 对象所需的对齐，
失败返回 NULL。`ctx` 会传给全部回调。
若 key/value 也由用户分配，可配置 `destroy_key/destroy_value` 回收它们。
ESP-IDF 的 `heap_caps_malloc` 示例见 `examples/esp_idf.c`，没有锁。

## 独立字符串头文件 dstr.h

`include/dstr.h` 不依赖 dict，可单独使用。采用类似 SDS 的分配布局：

| 分配偏移 | 内容 |
| --- | --- |
| 0 | `int` 长度头，记录内容的字节数 |
| `sizeof(int)` | 字符串内容，返回的 `dstr` 指向这里 |
| `sizeof(int) + length` | 末尾 `\0` |

长度不包含末尾零，也不是 Unicode 字符数量。支持内容中有零字节；
此时 strlen/strcmp 只能处理首个零之前的部分，需要使用长度或二进制比较。
这不是 Redis SDS 的完整实现或二进制兼容布局，没有额外容量字段；
复制和拼接按最终大小重新分配，不提供摊销 O(1) 的追加性能。

| API | 行为 |
| --- | --- |
| `dstr_new(text, allocator)` | 从 NUL 结尾字符串创建 |
| `dstr_new_len(bytes, length, allocator)` | 从指定字节数创建，允许内部零；NULL/0 创建空串 |
| `dstr_len(s)` | O(1) 读取长度；NULL 返回 0 |
| `dstr_alloc_size(s)` | 请求分配大小：头部＋长度＋1；NULL 返回 0 |
| `dstr_dup(s, allocator)` | 复制 dstr，保留内部零 |
| `dstr_copy(&s, bytes, length, allocator)` | 替换内容；成功返回 1，失败保留原指针 |
| `dstr_append(&s, bytes, length, allocator)` | 拼接内容；成功返回 1，失败保留原指针 |
| `dstr_compare(a, b)` | 按完整字节内容比较，返回负/零/正；NULL 视为空串 |
| `dstr_free(s, allocator)` | 还原分配起始地址后释放；NULL 安全 |

```c
#include "dstr.h"

void string_example(void) {
    dstr s = dstr_new("hello", NULL); /* NULL allocator 使用 malloc/free */
    if (!s) return;
    if (!dstr_append(&s, " world", 6, NULL)) {
        dstr_free(s, NULL); /* OOM 时原字符串仍有效 */
        return;
    }
    size_t len = dstr_len(s);          /* 11 */
    size_t bytes = dstr_alloc_size(s); /* sizeof(int) + 11 + 1 */
    (void)len; (void)bytes;
    dstr_free(s, NULL);
}
```

自定义 allocator 使用 `dstr_allocator {alloc, free, ctx}`，回调必须成对，
并在创建、复制/拼接、释放期间保持同一组分配器和有效 ctx。allocator 的请求
包含完整长度头和终止符，方便在用户的回调中统计分配总量。
超过 `INT_MAX`、分配大小溢出或 OOM 返回失败。

只允许对 dstr 创建的指针调用长度/释放函数；不能对普通字面量、普通
malloc 字符串或子串指针调用。不要直接 `free(s)`，也不要用普通 C 字符串
操作改变长度，否则头部信息不再正确。成功复制/拼接会使旧指针失效，
支持源数据位于旧字符串中的情况。key 入 dict 后不得修改或重新分配。
使用现有 DICT_HASH_STRING/DICT_EQUAL_STRING 时，key 应避免内部零；
二进制 key 需另配 hash/equal 回调。

调用方的 dstr 分配仍不计入 dict 节点/桶的全局计数；`dstr_alloc_size` 是
请求大小，不包含分配器元数据/对齐开销。析构字符串必须使用 dstr_free，
因此示例单独提供 string 析构回调，int value 则使用普通 allocator free。

## 全局内存统计（可关闭）

默认 `DICT_ENABLE_MEMORY_STATS=0`，不创建全局统计对象，也不执行统计更新。
需要统计时，给项目中**所有包含 dict.h 的编译单元**统一添加
`-DDICT_ENABLE_MEMORY_STATS=1`。这是项目级编译开关，不是运行时开关；
不同编译单元混用 0/1 会遗漏分配或释放，导致统计错误。

打开后，在**恰好一个** `.c` 文件中定义统计对象：

```c
/* 项目所有编译单元已经统一设置 DICT_ENABLE_MEMORY_STATS=1 */
#define DICT_MEMORY_STATS_IMPLEMENTATION
#include "dict.h"
```

其他文件正常包含 `dict.h` 即可。所有表、所有编译单元共用一个统计对象，
不是在头文件中为每个文件创建 static 计数器。无需增加单独的库源文件。
关闭统计时，不需要上述实现定义；查询仍可调用，返回全零。

```c
dict_memory_stats stats = DICT_MEMORY_STATS_GET();
/* stats.live_bytes: 尚未释放的请求字节数
 * stats.live_blocks: 尚未释放的分配块数
 * stats.peak_bytes:  历史最高请求字节数 */
```

统计覆盖默认和自定义 `alloc/free` 分配的**节点、桶数组**，包括 rehash
期间的新旧桶数组和 OOM 回滚释放。内部释放路径已知对应分配的大小，
因此无需给每块内存加统计头，也不改变自定义分配器收到的大小和指针。
节点/表结构布局保持不变。统计不是实际堆占用：不包含 allocator 元数据、
对齐开销、栈或静态的 `dict` 对象、调用方分配的 key/value，以及内存池预留空间。
析构回调释放的 key/value 也不进入这组计数，需由调用方另行检查。

成功 `DICT_CLEAR` 或 `DICT_DESTROY` 会扣除该表的所有内部存储，但只有
**所有表**清理完毕，全局 `live_bytes/live_blocks` 才都应为 0。
仅 remove/take 全部条目仍保留桶数组；返回 `DICT_BUSY` 的清理不会释放存储。
`peak_bytes` 是历史值，不随释放归零。需要重新开始测量峰值时调用
`DICT_MEMORY_STATS_RESET()`；仍有未释放块时返回 `DICT_BUSY`，禁止直接清零
来掩盖泄漏。请在任务停止操作后检查全局归零或重置。

### 多任务共享统计

默认统计也不内置锁。即使不同任务操作不同的表，全局计数仍是共享数据。
多任务使用时，在每个编译单元包含 `dict.h` 前，通过统一配置头定义
`DICT_MEMORY_STATS_LOCK()` 和 `DICT_MEMORY_STATS_UNLOCK()`，它们必须使用
同一把锁，保护统计更新、读取和重置。不允许只定义其中一个。
分配/释放回调在统计锁外执行；快照用于已完成操作的诊断，不是底层堆的
事务快照，并发进行中的分配/释放可能暂未反映出来。

ESP-IDF 示例的 `examples/esp_idf_dict_config.h` 使用共享 `portMUX_TYPE`，
仅把短小的计数更新或快照放入临界区。所有使用表的文件必须先包含这个配置头。
`examples/esp_idf.c` 给出唯一统计对象和 spinlock 定义，默认打开统计；
添加项目级 `DICT_ENABLE_MEMORY_STATS=0` 可关闭。
**统计锁不会保护表操作**；同一表的多个任务仍需外部 mutex 或单任务串行化。
这也不提供 ISR 支持，表 API 仍只用于任务上下文。

统计归零是内存泄漏检查的必要条件，不是对越界、重复释放、悬空指针或
用户对象泄漏的完整证明，建议结合 heap integrity / heap tracing。

## 宏接口

所有操作统一通过 `DICT_INIT`、`DICT_PUT`、`DICT_GET` 等宏调用，宏只转发到
类型明确的 `static inline` 实现。每个参数在展开式中仅出现一次，允许使用
`ptr++` 等带副作用的表达式；不同参数之间的求值顺序仍遵循 C 函数调用规则，
不要在不同参数中同时修改同一个变量。返回值和原有 API 保持一致。

`DICT_HASH_STRING`、`DICT_EQUAL_STRING` 使用对象式宏，可直接作为函数指针
传入配置，也可以直接调用。操作宏本身不能取函数地址；需要回调的 API
继续通过配置结构中的函数指针提供，不依赖操作宏。

`dict_init(...)` 等小写调用形式作为便捷宏保留。实现函数 `dict_*_impl` 和
数据结构的内部字段不作为稳定接口，应用应使用公开宏。
宏包装不增加运行时分配，也不改变节点或字典对象的内存布局。

统一使用 `include/dict.h`、`dict`、`dict_config` 和 `DICT_*`。
项目级统计开关、实现定义和锁钩子也统一使用 `DICT_*` 名称。

## API 与所有权

| API | 行为 |
| --- | --- |
| `DICT_INIT` | 初始化未初始化或已销毁对象；不分配内存 |
| `DICT_PUT` | 新增返回 `DICT_ADDED`；相同键替换返回 `DICT_REPLACED` |
| `DICT_GET` | 找到返回 1，未找到返回 0；输出借用的 value |
| `DICT_REMOVE` | 删除并调用配置的对象析构回调 |
| `DICT_TAKE` | 删除但不析构，将 key/value 所有权交给调用方 |
| `DICT_RESERVE` | 请求桶容量；正在迁移且请求更大容量时返回 `DICT_BUSY` |
| `DICT_REHASH_STEP` | 手动迁移，返回消耗的工作单元数 |
| `DICT_FOREACH` | 遍历两个表；回调返回非零时停止 |
| `DICT_SIZE / DICT_CAPACITY` | 条目数 / 目标表桶数 |
| `DICT_IS_REHASHING` | 是否处于双表迁移状态 |
| `DICT_CLEAR` | 释放全部存储，保留配置，可继续插入 |
| `DICT_DESTROY` | 释放并清零；再次使用前需要 init |

配置析构回调后，成功 put 的 key/value 由表负责释放；失败仍由调用方负责。
替换时同时采用新的 key 和 value，释放原来的对象；对应指针未变则不释放。
不同所有权对象之间不得共享同一地址，尤其不要把同一个需要释放的对象
同时作为 key/value。`take` 要求两个非 NULL 且不同的输出地址。

初始化失败后不要调用其他操作；已初始化的表不要再次直接 init，也不要
按值复制拥有动态内存的 `dict`。对象析构/分配/哈希/比较回调不得重入表操作。
析构回调需要能够处理传入的 NULL value。

## 渐进式 rehash 与运行时间

默认每次 put/get/remove/take 推进 2 个工作单元。一个单元只迁移一个节点，
或跳过一个空桶，因此长链不会在一个单元中全部搬迁。取消缓存 hash 后，
迁移节点会重新调用 hash 回调，这是用计算量换取更小节点内存的选择。

可设置 `c.rehash_work = 0` 关闭自动推进，随后在任务合适的位置调用
`DICT_REHASH_STEP(&h, budget)`。设置预算只是限制迁移工作量，不是硬实时保证：
新桶数组的分配/清零是同步的，查询可能遍历长链，回调和分配器耗时也不受限制。
哈希函数应满足“比较相等的键具有相同哈希值”。默认字符串 FNV-1a 适合可信键；
面对外部恶意输入，应使用带密钥的哈希，例如 SipHash。

迁移时，新条目进入新表；查找/删除查询两个表。迁移期间不会启动第二次扩容，
也不会为了插入而强制完成迁移，所以禁用自动推进却不手动推进会使链增长。
`foreach` 期间暂停迁移，可在回调中 get/size；修改、嵌套遍历和 clear/destroy
返回 `DICT_BUSY`，遍历结束后自动恢复。

没有内置 mutex、原子操作或 FreeRTOS 依赖。可直接用于单任务环境。
多个任务共享同一表时需要调用方串行化访问；**get 也可能推进 rehash**。
默认涉及堆分配，不应在 ISR 中调用；长操作不要放进 FreeRTOS 临界区。

## 验证

```sh
make test
make sanitize
# 也可使用 CMake
cmake -S . -B build-cmake -DDICT_SANITIZE=ON
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

测试覆盖多编译单元链接、字符串/整数键、NULL value、所有权转移与释放、
逐个注入分配失败、全碰撞长链、单节点迁移预算、遍历期间修改保护，
以及 20 万次对照模型随机操作。

`make test` 同时测试统计开/关、跨编译单元共享、默认/自定义分配器混用、
OOM 回滚、rehash 两张桶表的统计和清理后归零；另用四个 pthread 任务操作
各自的表，验证共享统计锁。CMake 在支持 Threads 及 GNU/Clang 的主机上也
运行该并发测试。测试强制启用 assert，避免其中的操作被 NDEBUG 去掉。
这些仍是主机测试，不代表 ESP-IDF 5.5.1 或 ESP32-S3 板上验证已完成。

当前工作区没有 CMake，已直接使用 GCC 验证 C99，分别运行优化构建和
AddressSanitizer/UndefinedBehaviorSanitizer 构建。当前运行环境的 ptrace
限制使 LeakSanitizer 无法运行，因此 sanitizer 验证时设置了
`ASAN_OPTIONS=detect_leaks=0`；自定义分配器测试另行核对未释放分配数。
尚未使用 ESP-IDF 工具链编译或在实际 ESP32 上验证。
