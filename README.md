# rtos-hashtable

面向 ESP32 / FreeRTOS 的 header-only 字典（dict）。字典底层持有两张 hashtable，参考 Redis dict 的双表渐进式 rehash 设计。
正常情况下只使用 `tables[0]`；扩容时分配 `tables[1]` 的桶数组，逐步迁移节点。
查找和删除检查两张表，新条目进入新表。迁移完成后释放旧桶并切换回单表。

纯 C99，无外部依赖，无内置锁。复制 `include/dict.h`、`include/sds.h` 和 `include/rtos_namespace.h`，所有函数均为
`static inline`，可在多个 C 编译单元中包含，不需要单独编译库。
对外通过 `DICT_PUT` 和其他 `DICT_*` 宏调用，具体实现使用 `dict_*_impl` 内联函数。

参考 Redis dict 的链式冲突处理和双表渐进式 rehash，自行实现，并非移植
Redis 源码。支持自定义 malloc/free、键哈希/比较，自动复制和释放字符串 key/value。

## 内存设计

节点只存三个指针：`next / key / value`，不缓存 hash；复制模式的 key/value 独立分配。
在常见 ESP32 32 位 ABI 下，节点为 **12 字节**，每个桶 **4 字节**，
表对象约 **76 字节**。上述数字按类型布局计算，未在 ESP32 设备上实测；
不包含分配器的块头、对齐浪费和用户对象。可用 `sizeof(dict_entry)`、
`sizeof(dict)` 在实际编译目标上确认。

正常运行时，表自身占用约为 `76 + 12 × 节点数 + 4 × 桶数` 字节。
例如 100 条记录、128 个桶约 1788 字节。rehash 期间同时保留旧、新桶数组，
**不复制节点**；从 128 桶扩展到 256 桶时，额外占用 1024 字节。

容量取不小于请求值的 2 的幂，最少 4 个桶。默认负载达到 1 后扩容；
不会自动缩容。删除释放节点，`clear` 释放所有节点和桶并保留配置。
`reserve` 可在启动阶段预分配桶，减少运行中的扩容。
它不会预分配节点；如需避免节点分配抖动，可接入自己的内存池分配器。

## 最小用法：普通 char* key/value

`DICT_PUT` 接收两个以 NUL 结尾的 `const char *`，自动复制为内部 SDS，
无需配置复制或析构回调。输入始终归调用方。以下输入均在堆上分配：

```c
#include "dict.h" /* 同时包含 sds.h */

static void *example_malloc(size_t bytes, void *ctx) {
    (void)ctx;
    return malloc(bytes);
}
static void example_free(void *ptr, void *ctx) {
    (void)ctx;
    free(ptr);
}

void example(void) {
    DICT_T table;
    DICT_CONFIG_T config = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    char *key = NULL, *value = NULL;
    void *stored;
    DICT_STATUS_T status;
    config.alloc = example_malloc;
    config.free = example_free;
    if (DICT_INIT(&table, &config) != DICT_OK) return;
    key = (char *)config.alloc(sizeof("temperature"), config.ctx);
    value = (char *)config.alloc(sizeof("25"), config.ctx);
    if (!key || !value) goto done;
    strcpy(key, "temperature");
    strcpy(value, "25");
    status = DICT_PUT(&table, key, value);
    /* 输入和内部副本独立；写入成功或失败都由调用方释放输入。 */
    config.free(key, config.ctx); key = NULL;
    config.free(value, config.ctx); value = NULL;
    if (status != DICT_ADDED && status != DICT_REPLACED) goto done;
    if (DICT_GET(&table, "temperature", &stored)) {
        SDS_T result = (SDS_T)stored; /* 借用，不能自行释放或修改 */
        size_t length = SDS_LEN(result);
        (void)length;
    }
    (void)DICT_REMOVE(&table, "temperature"); /* 释放内部两个 SDS 和节点 */
done:
    config.free(key, config.ctx);
    config.free(value, config.ctx);
    (void)DICT_DESTROY(&table);
}
```

## SDS key/value 写入

`DICT_PUT_SDS` 的两个输入都必须为本库创建的 SDS，按长度头复制；
不能传普通 malloc 字符串或字面量，否则会访问不存在的长度头。
value 可以为 NULL，两种接口都支持；key 必须非 NULL。

```c
SDS_ALLOCATOR_T allocator = {config.alloc, config.free, config.ctx};
SDS_T key = SDS_NEW("temperature", &allocator);
SDS_T value = SDS_NEW("25", &allocator);
if (key && value) {
    DICT_STATUS_T status = DICT_PUT_SDS(&table, key, value);
    (void)status;
}
SDS_FREE(key, &allocator);
SDS_FREE(value, &allocator);
/* table 的内部副本在 REMOVE/CLEAR/DESTROY 时自动释放。 */
```

两种接口都用 dict 的 alloc/free/ctx 管理内部 SDS，可在同一表交替写入。
`DICT_PUT` 遇到第一个 NUL 结束；`DICT_PUT_SDS` 保留内部 NUL 和后续字节。
默认哈希/比较仍按 C 字符串处理，含内部 NUL 的 key 必须改用按 SDS 长度
计算的 hash/equal；这时 GET/REMOVE/TAKE 查询 key 也必须是 SDS。
内部 SDS 对象不计入节点/桶的诊断统计；包含它们的总量由共享分配器统计。

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
DICT_CONFIG_T c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
c.alloc = my_malloc;
c.free = my_free;
c.ctx = NULL; /* 可用于传递内存池或应用上下文 */
```

`alloc/free` 必须同时配置；省略时使用标准 malloc/free。它们管理节点、
桶数组及两种公共写入接口创建的 SDS 副本。分配器须正确对齐，失败返回 NULL。
`ctx` 会传给分配器和 hash/equal。公共字符串写入不需要对象复制/析构回调；
若配置 copy_key/copy_value/destroy_key/destroy_value，则公共写入返回 DICT_INVALID，
避免混用对象释放约定。这些字段仅保留供内部通用引擎验证，应用不应使用。
ESP-IDF 适配见 examples/esp_idf.c，表操作仍需外部串行化。

## SDS / dict / value 共用一个分配器

完整可运行示例见 `examples/shared_allocator.c`。两张独立 dict 共用
shared_malloc/shared_free、global_memory 和同一 ctx：第一张用堆上的普通 char*
输入调用 DICT_PUT，第二张用堆上的 SDS 输入调用 DICT_PUT_SDS。
函数 `put_char_example` 与 `put_sds_example` 分别展示两种调用方式。
这两个堆输入示例每次写入后立即释放输入，dict 内部的 key/value 副本均为 SDS。
另提供 `put_char_array_example`，展示 key/value 都为局部 char 数组：

```c
char key[] = "mode";
char value[] = "auto";
DICT_STATUS_T status = DICT_PUT(&table, key, value);
/* 成功时已经保存独立副本；key/value 无需 free。 */
```

示例在写入后修改两个数组，并在函数返回后再次查询，验证内部副本仍为
mode/auto。局部数组不能传给 shared_free 或 SDS_FREE；dict 只释放自己的副本。

`put_get_int64_example` 演示 int64_t 写入/读取：用 snprintf 和 PRId64
把整数编码为十进制 char[]，经 DICT_PUT 保存；GET 后用 strtoimax
解析，检查 ERANGE、完整字符串消费和 INT64_MIN/MAX 范围，再转回 int64_t。
示例验证 INT64_MIN 和 INT64_MAX；不经过浮点数或 32 位 int。
这里保存的是整数的字符串表示，不能对取出的字符串直接强转 int64_t* 读取。


```c
SDS_ALLOCATOR_T allocator = {shared_malloc, shared_free, &global_memory};
DICT_CONFIG_T config = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
config.alloc = allocator.alloc;
config.free = allocator.free;
config.ctx = allocator.ctx;
/* 不配置对象复制/析构回调；公共字符串接口自动处理。 */
```

公共分配器在每个分配块前保存大小，返回保持对齐的用户指针；free 根据
块头扣减统计并释放原始地址。因此它统计的不仅有 dict 节点/新旧桶数组，
还有 SDS 长度头、内容、终止符，以及 key/value 副本。示例的 live_bytes
包含公共分配器自己的统计头，仍不包含底层 libc 的元数据和对齐开销。
SDS_FREE 先跳回 SDS 的 int 头，再由 shared_free 跳回公共分配器的头。
这两层起始地址不同，不能直接对 SDS 内容指针调用 shared_free。

运行：

```sh
make build/shared_allocator
./build/shared_allocator
./build/shared_allocator --test
```

输出依次显示：两张 dict 加 key/value 的总占用、销毁第一张后剩余占用、
全部清理后 `0 bytes / 0 blocks`。数值取决于目标 ABI。`--test` 会对每个
分配点注入一次失败，包括输入和副本的 key/value 分配及节点/桶分配，逐次检查总量归零。

这是单线程示例；应用多个任务共用分配器时，计数和底层分配器必须同步。
`DICT_ENABLE_MEMORY_STATS` 只控制库内节点/桶的诊断统计，不控制示例的自定义
计数器；两组数据有重叠，不能相加，否则重复计数。判断包含 SDS/value 的
总占用时使用公共分配器的数据。输入在成功或失败后都由调用方释放，
内部副本由 dict 自动释放；两张 dict 全部销毁才要求公共总量归零。
示例自己定义唯一统计对象并含 main，作为独立程序运行；集成回调时不要
在多个文件重复定义 DICT_MEMORY_STATS_IMPLEMENTATION。

## 独立字符串头文件 sds.h

`include/sds.h` 不依赖 dict，可与 `include/rtos_namespace.h` 一起单独使用。采用类似 SDS 的分配布局：

| 分配偏移 | 内容 |
| --- | --- |
| 0 | `int` 长度头，记录内容的字节数 |
| `sizeof(int)` | 字符串内容，返回的 `sds` 指向这里 |
| `sizeof(int) + length` | 末尾 `\0` |

长度不包含末尾零，也不是 Unicode 字符数量。支持内容中有零字节；
此时 strlen/strcmp 只能处理首个零之前的部分，需要使用长度或二进制比较。
这不是 Redis SDS 的完整实现或二进制兼容布局，没有额外容量字段；
复制和拼接按最终大小重新分配，不提供摊销 O(1) 的追加性能。

| API | 行为 |
| --- | --- |
| `SDS_NEW(text, allocator)` | 从 NUL 结尾字符串创建 |
| `SDS_NEW_LEN(bytes, length, allocator)` | 从指定字节数创建，允许内部零；NULL/0 创建空串 |
| `SDS_LEN(s)` | O(1) 读取长度；NULL 返回 0 |
| `SDS_ALLOC_SIZE(s)` | 请求分配大小：头部＋长度＋1；NULL 返回 0 |
| `SDS_DUP(s, allocator)` | 复制 sds，保留内部零 |
| `SDS_COPY(&s, bytes, length, allocator)` | 替换内容；成功返回 1，失败保留原指针 |
| `SDS_APPEND(&s, bytes, length, allocator)` | 拼接内容；成功返回 1，失败保留原指针 |
| `SDS_COMPARE(a, b)` | 按完整字节内容比较，返回负/零/正；NULL 视为空串 |
| `SDS_FREE(s, allocator)` | 还原分配起始地址后释放；NULL 安全 |

```c
#include "sds.h"

void string_example(void) {
    SDS_T s = SDS_NEW("hello", NULL); /* NULL allocator 使用 malloc/free */
    if (!s) return;
    if (!SDS_APPEND(&s, " world", 6, NULL)) {
        SDS_FREE(s, NULL); /* OOM 时原字符串仍有效 */
        return;
    }
    size_t len = SDS_LEN(s);          /* 11 */
    size_t bytes = SDS_ALLOC_SIZE(s); /* sizeof(int) + 11 + 1 */
    (void)len; (void)bytes;
    SDS_FREE(s, NULL);
}
```

自定义 allocator 使用 `sds_allocator {alloc, free, ctx}`，回调必须成对，
并在创建、复制/拼接、释放期间保持同一组分配器和有效 ctx。allocator 的请求
包含完整长度头和终止符，方便在用户的回调中统计分配总量。
超过 `INT_MAX`、分配大小溢出或 OOM 返回失败。

只允许对 sds 创建的指针调用长度/释放函数；不能对普通字面量、普通
malloc 字符串或子串指针调用。不要直接 `free(s)`，也不要用普通 C 字符串
操作改变长度，否则头部信息不再正确。成功复制/拼接会使旧指针失效，
支持源数据位于旧字符串中的情况。key 入 dict 后不得修改或重新分配。
使用现有 DICT_HASH_STRING/DICT_EQUAL_STRING 时，key 应避免内部零；
二进制 key 需另配 hash/equal 回调。

调用方的 sds 分配仍不计入 dict 节点/桶的全局计数；`sds_alloc_size` 是
请求大小，不包含分配器元数据/对齐开销。析构字符串必须使用 sds_free，
普通 char* 输入使用 allocator free，SDS 输入使用 SDS_FREE；dict 自动正确释放内部 SDS。

## 使用宏作为类型

可以：预处理器会把对象式类型宏展开成真正的 typedef 名称，然后编译器
按普通类型处理。提供的宏自动跟随 RTOS_PREFIX，不需要在变量声明处反复
写 RTOS_SYMBOL：

```c
SDS_T key;
SDS_ALLOCATOR_T allocator;
DICT_T table;
DICT_CONFIG_T config;
DICT_STATUS_T status;
DICT_MEMORY_STATS_T stats;
```

例如 `SDS_T` 定义为 `RTOS_SYMBOL(sds)`：默认展开成 sds；前缀为 app_ 时
展开成 app_sds。也可以用于函数参数、返回类型、指针和强制转换。
这是 typedef 的宏别名，没有增加存储或运行成本。另提供
`DICT_HASH_FN_T/DICT_EQUAL_FN_T/DICT_DESTROY_FN_T/DICT_COPY_FN_T/DICT_ALLOC_FN_T/DICT_FREE_FN_T/DICT_VISIT_FN_T`
回调类型，以及 `DICT_ENTRY_T/DICT_TABLE_T`；后两者属于底层结构，应用
优先使用 DICT_T 操作整个字典。

## 自定义 C 符号前缀

`include/rtos_namespace.h` 统一控制 dict 和 SDS 的 C 符号名称。默认
`RTOS_PREFIX` 是空宏，不是字符串字面量 `""`，默认名称为 `dict`、
`sds`、`sds_len` 等。项目所有编译单元可以统一添加：

```sh
-DRTOS_PREFIX=app_
```

也可以在任何库头文件之前，通过一个公共配置头定义：

```c
#define RTOS_PREFIX app_
#include "dict.h"
#include "sds.h"

void example_namespaced_string(void) {
    SDS_T s = SDS_NEW("hello", NULL); /* 类型为 app_sds */
    if (!s) return;
    size_t n = SDS_LEN(s); /* 调用 app_sds_len */
    (void)n;
    SDS_FREE(s, NULL);
}
```

前缀覆盖 typedef、struct/enum tag、枚举值、static inline 函数（含内部辅助）、
全局内存统计对象。类型通过 `DICT_T`、`DICT_CONFIG_T`、
`SDS_T`、`SDS_ALLOCATOR_T` 等引用。需要直接调用或取
函数地址时，使用 `RTOS_SYMBOL(sds_len)` 等；默认空前缀时也可直接使用
`sds_len`。字符串操作推荐统一使用 `SDS_NEW/SDS_LEN/SDS_FREE` 等宏，
每个参数在展开中只出现一次，参数间仍遵循 C 的求值规则。

`DICT_*` / `SDS_*` 调用宏、配置宏、头文件保护宏及 RTOS 命名空间宏的名称
保持固定，内部转发到所选前缀；标准 C 预处理器不能动态拼接 #define 的宏名。
枚举值请通过 `DICT_OK/DICT_OOM` 等调用层名称引用，不再套 RTOS_SYMBOL。
不要在包含头文件后改变前缀。同一个实例的所有编译单元必须配置同一前缀，
包含唯一全局统计对象的实现文件也必须一致，否则会链接失败或分属不同实例。
不同前缀可以在不同编译单元中构建并链接到同一个程序；各自的统计对象独立，
同一前缀内所有 dict 共享统计。一个编译单元只支持一种前缀。

ESP-IDF 示例的共享统计 mux 也带前缀；`app_main` 保留框架要求的固定名称。
库本身不为调用方的回调或应用业务符号自动添加前缀。

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
DICT_MEMORY_STATS_T stats = DICT_MEMORY_STATS_GET();
/* stats.live_bytes: 尚未释放的请求字节数
 * stats.live_blocks: 尚未释放的分配块数
 * stats.peak_bytes:  历史最高请求字节数 */
```

统计覆盖默认和自定义 `alloc/free` 分配的**节点、桶数组**，包括 rehash
期间的新旧桶数组和 OOM 回滚释放。内部释放路径已知对应分配的大小，
因此无需给每块内存加统计头，也不改变自定义分配器收到的大小和指针。
计数不会增加节点/表的布局开销。统计不是实际堆占用：不包含 allocator 元数据、
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

统一使用 `include/dict.h`、`DICT_T`、`DICT_CONFIG_T`、`DICT_PUT` 和其他 `DICT_*`。
项目级统计开关、实现定义和锁钩子也统一使用 `DICT_*` 名称。

## API 与所有权

| API | 行为 |
| --- | --- |
| `DICT_INIT` | 初始化未初始化或已销毁对象；不分配内存 |
| `DICT_PUT` | 两个普通 char* 输入，按 strlen 复制为 SDS；新增 ADDED，替换 REPLACED |
| `DICT_PUT_SDS` | 两个 SDS 输入，按长度头复制为 SDS；保留完整字节内容 |
| `DICT_GET` | 找到返回 1，未找到返回 0；输出借用的 value |
| `DICT_REMOVE` | 删除并自动释放内部 SDS key/value 副本 |
| `DICT_TAKE` | 删除但不析构，将 key/value 所有权交给调用方 |
| `DICT_RESERVE` | 请求桶容量；正在迁移且请求更大容量时返回 `DICT_BUSY` |
| `DICT_REHASH_STEP` | 手动迁移，返回消耗的工作单元数 |
| `DICT_FOREACH` | 遍历两个表；回调返回非零时停止 |
| `DICT_SIZE / DICT_CAPACITY` | 条目数 / 目标表桶数 |
| `DICT_IS_REHASHING` | 是否处于双表迁移状态 |
| `DICT_CLEAR` | 释放全部存储，保留配置，可继续插入 |
| `DICT_DESTROY` | 释放并清零；再次使用前需要 init |

两种写入接口都自动复制 key/value，输入始终归调用方；失败释放部分副本，
已有条目不变。NULL value 直接保存 NULL。替换、REMOVE、CLEAR、DESTROY
自动释放内部 SDS；GET 返回借用的 SDS 指针，不得自行释放或改变 key。
TAKE 则把两个 SDS 副本交给调用方，调用方使用与 dict 相同的 allocator
执行 SDS_FREE，而不是直接 free 内容指针。不同表可以使用不同分配器。

初始化失败后不要调用其他操作；已初始化的表不要再次直接 init，也不要
按值复制拥有动态内存的 `dict`。对象析构/分配/哈希/比较回调不得重入表操作。
公共字符串接口自动处理 NULL value，无需自定义析构。

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

测试覆盖多编译单元链接、字符串/整数键、NULL value、复制模式与所有权转移、替换及删除/销毁释放、
逐个注入分配失败、全碰撞长链、单节点迁移预算、遍历期间修改保护，
以及 20 万次对照模型随机操作。新增普通 char*/SDS 两种 key-value 写入测试，
覆盖参数单次求值、交替写入、二进制数据、OOM、全碰撞 rehash 和释放归零。

`make test` 同时测试统计开/关、跨编译单元共享、默认/自定义分配器混用、
OOM 回滚、rehash 两张桶表的统计和清理后归零；另用四个 pthread 任务操作
各自的表，验证共享统计锁。CMake 在支持 Threads 及 GNU/Clang 的主机上也
运行该并发测试。另验证默认与 demo_ 前缀在同一程序中共存、跨编译单元链接和 SDS 宏参数单次求值。公共分配器示例还验证统计开/关、自定义前缀、所有分配失败点及包含 key/value 的总量归零。测试强制启用 assert，避免其中的操作被 NDEBUG 去掉。
这些仍是主机测试，不代表 ESP-IDF 5.5.1 或 ESP32-S3 板上验证已完成。

当前工作区没有 CMake，已直接使用 GCC 验证 C99，分别运行优化构建和
AddressSanitizer/UndefinedBehaviorSanitizer 构建。当前运行环境的 ptrace
限制使 LeakSanitizer 无法运行，因此 sanitizer 验证时设置了
`ASAN_OPTIONS=detect_leaks=0`；自定义分配器测试另行核对未释放分配数。
尚未使用 ESP-IDF 工具链编译或在实际 ESP32 上验证。
