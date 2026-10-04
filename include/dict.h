#ifndef DICT_H
#define DICT_H

/* C99 header-only chained hash table, inspired by Redis dict's two-table
 * incremental rehash. Independent implementation, no Redis dependency.
 * Not thread safe: serialize ALL operations on a shared table, including get.
 * Hash/equality/destructor/allocator callbacks must not reenter the table.
 * Keys must retain a stable hash/equality while stored. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Accept legacy configuration names so FHT and DICT users share one setting. */
#if defined(FHT_ENABLE_MEMORY_STATS) && !defined(DICT_ENABLE_MEMORY_STATS)
#define DICT_ENABLE_MEMORY_STATS FHT_ENABLE_MEMORY_STATS
#endif
#if defined(FHT_ENABLE_MEMORY_STATS) && defined(DICT_ENABLE_MEMORY_STATS) && \
    FHT_ENABLE_MEMORY_STATS != DICT_ENABLE_MEMORY_STATS
#error "FHT and DICT memory statistics switches must match"
#endif
#if defined(FHT_MEMORY_STATS_IMPLEMENTATION) && !defined(DICT_MEMORY_STATS_IMPLEMENTATION)
#define DICT_MEMORY_STATS_IMPLEMENTATION
#endif
#if defined(FHT_MEMORY_STATS_LOCK) && !defined(DICT_MEMORY_STATS_LOCK)
#define DICT_MEMORY_STATS_LOCK() FHT_MEMORY_STATS_LOCK()
#endif
#if defined(FHT_MEMORY_STATS_UNLOCK) && !defined(DICT_MEMORY_STATS_UNLOCK)
#define DICT_MEMORY_STATS_UNLOCK() FHT_MEMORY_STATS_UNLOCK()
#endif

/* Set identically in every translation unit. Disabled builds have no global
 * storage or accounting. Enabled builds define DICT_MEMORY_STATS_IMPLEMENTATION
 * in exactly one translation unit before including this header. */
#ifndef DICT_ENABLE_MEMORY_STATS
#define DICT_ENABLE_MEMORY_STATS 0
#endif
#if DICT_ENABLE_MEMORY_STATS != 0 && DICT_ENABLE_MEMORY_STATS != 1
#error "DICT_ENABLE_MEMORY_STATS must be 0 or 1"
#endif
#if defined(DICT_MEMORY_STATS_LOCK) != defined(DICT_MEMORY_STATS_UNLOCK)
#error "Define both DICT_MEMORY_STATS_LOCK and DICT_MEMORY_STATS_UNLOCK"
#endif
#ifndef DICT_MEMORY_STATS_LOCK
#define DICT_MEMORY_STATS_LOCK() ((void)0)
#define DICT_MEMORY_STATS_UNLOCK() ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*dict_hash_fn)(const void *key, void *ctx);
typedef int (*dict_equal_fn)(const void *a, const void *b, void *ctx);
typedef void (*dict_destroy_fn)(void *object, void *ctx);
typedef void *(*dict_alloc_fn)(size_t bytes, void *ctx);
typedef void (*dict_free_fn)(void *ptr, void *ctx);

typedef enum dict_status {
    DICT_OK = 0, DICT_ADDED = 1, DICT_REPLACED = 2,
    DICT_INVALID = -1, DICT_OOM = -2, DICT_OVERFLOW = -3,
    DICT_BUSY = -4, DICT_NOT_FOUND = -5
} dict_status;

typedef struct dict_config {
    dict_hash_fn hash;
    dict_equal_fn equal;
    dict_destroy_fn destroy_key;
    dict_destroy_fn destroy_value;
    dict_alloc_fn alloc;           /* alloc/free must both be set, or both NULL */
    dict_free_fn free;
    void *ctx;                    /* passed to every callback */
    size_t rehash_work;            /* per-operation work units; 0 = manual */
} dict_config;

typedef struct dict_entry {
    struct dict_entry *next;
    void *key;
    void *value;
} dict_entry;

typedef struct dict_table {
    dict_entry **buckets;
    size_t capacity;
    size_t used;
} dict_table;

/* A dictionary owns two bucket tables; table[1] is active during rehash. */
typedef struct dict {
    dict_config config;
    dict_table tables[2];
    size_t rehash_index;
    unsigned visiting;
} dict;

/* Requested bytes for nodes and bucket arrays only, excluding user objects,
 * table structs and allocator overhead. peak_bytes is a historical watermark. */
typedef struct dict_memory_stats {
    size_t live_bytes;
    size_t live_blocks;
    size_t peak_bytes;
} dict_memory_stats;

#if DICT_ENABLE_MEMORY_STATS
#ifdef DICT_MEMORY_STATS_IMPLEMENTATION
dict_memory_stats dict_memory_stats_global_impl = {0, 0, 0};
#else
extern dict_memory_stats dict_memory_stats_global_impl;
#endif
#endif

/* Supply shared lock/unlock hooks if different tasks operate on different
 * tables. These hooks protect counters ONLY, not table operations. Allocator
 * callbacks execute outside the statistics lock. */
static inline dict_memory_stats dict_memory_stats_get_impl(void) {
    dict_memory_stats result = {0, 0, 0};
#if DICT_ENABLE_MEMORY_STATS
    DICT_MEMORY_STATS_LOCK();
    result = dict_memory_stats_global_impl;
    DICT_MEMORY_STATS_UNLOCK();
#endif
    return result;
}
/* Never erase outstanding allocations to make a leak check pass. Call only
 * when operations have quiesced; concurrent allocations can follow the reset. */
static inline dict_status dict_memory_stats_reset_impl(void) {
#if DICT_ENABLE_MEMORY_STATS
    dict_status status = DICT_OK;
    DICT_MEMORY_STATS_LOCK();
    if (dict_memory_stats_global_impl.live_blocks != 0)
        status = DICT_BUSY;
    else
        memset(&dict_memory_stats_global_impl, 0,
               sizeof(dict_memory_stats_global_impl));
    DICT_MEMORY_STATS_UNLOCK();
    return status;
#else
    return DICT_OK;
#endif
}

static inline void *dict_alloc_bytes_impl(dict *h, size_t bytes) {
    void *ptr = h->config.alloc(bytes, h->config.ctx);
#if DICT_ENABLE_MEMORY_STATS
    int overflow = 0;
    if (ptr) {
        DICT_MEMORY_STATS_LOCK();
        if (bytes > SIZE_MAX - dict_memory_stats_global_impl.live_bytes ||
            dict_memory_stats_global_impl.live_blocks == SIZE_MAX) {
            overflow = 1;
        } else {
            dict_memory_stats_global_impl.live_bytes += bytes;
            ++dict_memory_stats_global_impl.live_blocks;
            if (dict_memory_stats_global_impl.live_bytes >
                dict_memory_stats_global_impl.peak_bytes)
                dict_memory_stats_global_impl.peak_bytes =
                    dict_memory_stats_global_impl.live_bytes;
        }
        DICT_MEMORY_STATS_UNLOCK();
        if (overflow) {
            h->config.free(ptr, h->config.ctx);
            return NULL;
        }
    }
#endif
    return ptr;
}
static inline void dict_free_bytes_impl(dict *h, void *ptr, size_t bytes) {
    if (!ptr) return;
    h->config.free(ptr, h->config.ctx);
#if DICT_ENABLE_MEMORY_STATS
    DICT_MEMORY_STATS_LOCK();
    dict_memory_stats_global_impl.live_bytes -= bytes;
    --dict_memory_stats_global_impl.live_blocks;
    DICT_MEMORY_STATS_UNLOCK();
#else
    (void)bytes;
#endif
}

static inline void *dict_default_alloc_impl(size_t bytes, void *ctx) {
    (void)ctx;
    return malloc(bytes);
}
static inline void dict_default_free_impl(void *ptr, void *ctx) {
    (void)ctx;
    free(ptr);
}
static inline dict_config dict_config_default_impl(dict_hash_fn hash,
                                            dict_equal_fn equal) {
    dict_config c;
    memset(&c, 0, sizeof(c));
    c.hash = hash;
    c.equal = equal;
    c.rehash_work = 2;
    return c;
}
/* Initialize an uninitialized or destroyed object; no allocation occurs. */
static inline dict_status dict_init_impl(dict *h, const dict_config *config) {
    if (!h || !config || !config->hash || !config->equal ||
        (!!config->alloc != !!config->free)) return DICT_INVALID;
    memset(h, 0, sizeof(*h));
    h->config = *config;
    if (!h->config.alloc) {
        h->config.alloc = dict_default_alloc_impl;
        h->config.free = dict_default_free_impl;
    }
    return DICT_OK;
}
static inline size_t dict_size_impl(const dict *h) {
    return h->tables[0].used + h->tables[1].used;
}
static inline int dict_is_rehashing_impl(const dict *h) {
    return h->tables[1].buckets != NULL;
}
static inline size_t dict_capacity_impl(const dict *h) {
    return h->tables[dict_is_rehashing_impl(h) ? 1 : 0].capacity;
}
static inline void dict_finish_rehash_impl(dict *h) {
    if (dict_is_rehashing_impl(h) && h->tables[0].used == 0) {
        dict_free_bytes_impl(h, h->tables[0].buckets,
                            h->tables[0].capacity * sizeof(dict_entry *));
        h->tables[0] = h->tables[1];
        memset(&h->tables[1], 0, sizeof(h->tables[1]));
        h->rehash_index = 0;
    }
}
/* At most budget units: one unit moves ONE entry or skips ONE empty bucket.
 * Allocation-free. Does not bound wall time of allocators or callbacks. */
static inline size_t dict_rehash_step_impl(dict *h, size_t budget) {
    size_t work = 0;
    if (h->visiting) return 0;
    dict_finish_rehash_impl(h);
    while (dict_is_rehashing_impl(h) && work < budget) {
        dict_table *old = &h->tables[0], *next = &h->tables[1];
        dict_entry *e = old->buckets[h->rehash_index];
        if (!e) {
            ++h->rehash_index;
        } else {
            size_t bucket = (size_t)h->config.hash(e->key, h->config.ctx) &
                            (next->capacity - 1);
            old->buckets[h->rehash_index] = e->next;
            e->next = next->buckets[bucket];
            next->buckets[bucket] = e;
            --old->used;
            ++next->used;
        }
        ++work;
        dict_finish_rehash_impl(h);
    }
    return work;
}
/* Request capacity for at least entries at load factor <= 1. During an
 * active rehash, larger requests return BUSY; call rehash_step and retry.
 * Allocating/zeroing the new bucket array is synchronous, unlike migration. */
static inline dict_status dict_reserve_impl(dict *h, size_t entries) {
    size_t capacity = 4;
    dict_entry **buckets;
    if (h->visiting) return DICT_BUSY;
    if (entries <= dict_capacity_impl(h)) return DICT_OK;
    if (dict_is_rehashing_impl(h)) return DICT_BUSY;
    while (capacity < entries) {
        if (capacity > SIZE_MAX / 2) return DICT_OVERFLOW;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*buckets)) return DICT_OVERFLOW;
    buckets = (dict_entry **)dict_alloc_bytes_impl(h, capacity * sizeof(*buckets));
    if (!buckets) return DICT_OOM;
    memset(buckets, 0, capacity * sizeof(*buckets));
    if (!h->tables[0].buckets) {
        h->tables[0].buckets = buckets;
        h->tables[0].capacity = capacity;
    } else {
        h->tables[1].buckets = buckets;
        h->tables[1].capacity = capacity;
        h->rehash_index = 0;
        dict_finish_rehash_impl(h);
    }
    return DICT_OK;
}
static inline dict_entry **dict_find_impl(dict *h, const void *key,
                                    uint32_t hash, unsigned *table_index) {
    unsigned i;
    for (i = 0; i < 2; ++i) {
        dict_table *t = &h->tables[i];
        dict_entry **link;
        if (!t->buckets) continue;
        link = &t->buckets[(size_t)hash & (t->capacity - 1)];
        while (*link) {
            if (h->config.equal((*link)->key, key, h->config.ctx)) {
                if (table_index) *table_index = i;
                return link;
            }
            link = &(*link)->next;
        }
    }
    return NULL;
}
/* Returns 1 if found, including stored NULL values. On miss *value = NULL.
 * Returned objects are borrowed, valid until replacement/removal/clear. */
static inline int dict_get_impl(dict *h, const void *key, void **value) {
    dict_entry **link;
    dict_rehash_step_impl(h, h->config.rehash_work);
    link = dict_find_impl(h, key, h->config.hash(key, h->config.ctx), NULL);
    if (value) *value = link ? (*link)->value : NULL;
    return link != NULL;
}
/* On success table owns key/value per configured destructors. On error
 * ownership stays with caller. Replacement adopts BOTH new key and value,
 * destroying old objects unless their corresponding pointers are unchanged.
 * Key/value ownership must be independent (no aliases between owned objects). */
static inline dict_status dict_put_impl(dict *h, void *key, void *value) {
    uint32_t hash;
    dict_entry **link, *e;
    dict_table *t;
    size_t bucket, size;
    dict_status status;
    if (h->visiting) return DICT_BUSY;
    dict_rehash_step_impl(h, h->config.rehash_work);
    hash = h->config.hash(key, h->config.ctx);
    link = dict_find_impl(h, key, hash, NULL);
    if (link) {
        e = *link;
        if (e->key != key && h->config.destroy_key)
            h->config.destroy_key(e->key, h->config.ctx);
        if (e->value != value && h->config.destroy_value)
            h->config.destroy_value(e->value, h->config.ctx);
        e->key = key;
        e->value = value;
        return DICT_REPLACED;
    }
    size = dict_size_impl(h);
    if (size == SIZE_MAX) return DICT_OVERFLOW;
    e = (dict_entry *)dict_alloc_bytes_impl(h, sizeof(*e));
    if (!e) return DICT_OOM;
    /* While migrating, allow chains to grow instead of forcing completion. */
    if (!dict_is_rehashing_impl(h) && size >= h->tables[0].capacity) {
        status = dict_reserve_impl(h, size + 1);
        if (status != DICT_OK) {
            dict_free_bytes_impl(h, e, sizeof(*e));
            return status;
        }
    }
    t = &h->tables[dict_is_rehashing_impl(h) ? 1 : 0];
    bucket = (size_t)hash & (t->capacity - 1);
    e->key = key;
    e->value = value;
    e->next = t->buckets[bucket];
    t->buckets[bucket] = e;
    ++t->used;
    return DICT_ADDED;
}
static inline dict_status dict_remove_entry_impl(dict *h, const void *key,
                                      void **out_key, void **out_value,
                                      int take) {
    dict_entry **link, *e;
    unsigned table_index = 0;
    if (out_key) *out_key = NULL;
    if (out_value) *out_value = NULL;
    if (h->visiting) return DICT_BUSY;
    dict_rehash_step_impl(h, h->config.rehash_work);
    link = dict_find_impl(h, key, h->config.hash(key, h->config.ctx), &table_index);
    if (!link) return DICT_NOT_FOUND;
    e = *link;
    *link = e->next;
    --h->tables[table_index].used;
    if (take) {
        if (out_key) *out_key = e->key;
        if (out_value) *out_value = e->value;
    } else {
        if (h->config.destroy_key) h->config.destroy_key(e->key, h->config.ctx);
        if (h->config.destroy_value) h->config.destroy_value(e->value, h->config.ctx);
    }
    dict_free_bytes_impl(h, e, sizeof(*e));
    dict_finish_rehash_impl(h);
    return DICT_OK;
}
static inline dict_status dict_remove_impl(dict *h, const void *key) {
    return dict_remove_entry_impl(h, key, NULL, NULL, 0);
}
/* Transfer BOTH objects to caller without destructors. Both outputs required. */
static inline dict_status dict_take_impl(dict *h, const void *key,
                                  void **out_key, void **out_value) {
    if (!out_key || !out_value || out_key == out_value) return DICT_INVALID;
    return dict_remove_entry_impl(h, key, out_key, out_value, 1);
}
/* Visit each entry exactly once, even during rehash. Return nonzero to stop.
 * Callback may call get/size but mutation/reserve/clear returns BUSY.
 * Recursive visits are rejected. Never destroy the table inside a callback. */
typedef int (*dict_visit_fn)(const void *key, void *value, void *ctx);
static inline dict_status dict_foreach_impl(dict *h, dict_visit_fn visit, void *ctx) {
    unsigned i;
    if (!visit) return DICT_INVALID;
    if (h->visiting) return DICT_BUSY;
    h->visiting = 1;
    for (i = 0; i < 2; ++i) {
        size_t b;
        for (b = 0; b < h->tables[i].capacity; ++b) {
            dict_entry *e = h->tables[i].buckets[b];
            while (e) {
                if (visit(e->key, e->value, ctx)) {
                    h->visiting = 0;
                    return DICT_OK;
                }
                e = e->next;
            }
        }
    }
    h->visiting = 0;
    return DICT_OK;
}
/* Free all storage and owned objects, keep config; table can be reused. */
static inline dict_status dict_clear_impl(dict *h) {
    unsigned i;
    if (h->visiting) return DICT_BUSY;
    for (i = 0; i < 2; ++i) {
        size_t b;
        for (b = 0; b < h->tables[i].capacity; ++b) {
            dict_entry *e = h->tables[i].buckets[b];
            while (e) {
                dict_entry *next = e->next;
                if (h->config.destroy_key)
                    h->config.destroy_key(e->key, h->config.ctx);
                if (h->config.destroy_value)
                    h->config.destroy_value(e->value, h->config.ctx);
                dict_free_bytes_impl(h, e, sizeof(*e));
                e = next;
            }
        }
        if (h->tables[i].buckets)
            dict_free_bytes_impl(h, h->tables[i].buckets,
                                h->tables[i].capacity * sizeof(dict_entry *));
        memset(&h->tables[i], 0, sizeof(h->tables[i]));
    }
    h->rehash_index = 0;
    return DICT_OK;
}
static inline dict_status dict_destroy_impl(dict *h) {
    dict_status status = dict_clear_impl(h);
    if (status == DICT_OK) memset(h, 0, sizeof(*h));
    return status;
}
/* Convenience hash for trusted string keys: FNV-1a, NOT collision resistant.
 * For untrusted keys provide a keyed hash, e.g. SipHash. */
static inline uint32_t dict_hash_string_impl(const void *key, void *ctx) {
    const unsigned char *s = (const unsigned char *)key;
    uint32_t hash = UINT32_C(2166136261);
    (void)ctx;
    while (*s) { hash ^= *s++; hash *= UINT32_C(16777619); }
    return hash;
}
static inline int dict_equal_string_impl(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return strcmp((const char *)a, (const char *)b) == 0;
}

/* Public API. Each argument appears exactly once in the expansion; normal C
 * function-call type checking and argument evaluation order still apply.
 * *_impl functions are implementation details, not the public contract. */
#define DICT_CONFIG_DEFAULT(hash_fn, equal_fn) \
    (dict_config_default_impl((hash_fn), (equal_fn)))
#define DICT_MEMORY_STATS_GET() (dict_memory_stats_get_impl())
#define DICT_MEMORY_STATS_RESET() (dict_memory_stats_reset_impl())
#define DICT_INIT(table, config) \
    (dict_init_impl((table), (config)))
#define DICT_SIZE(table) \
    (dict_size_impl((table)))
#define DICT_IS_REHASHING(table) \
    (dict_is_rehashing_impl((table)))
#define DICT_CAPACITY(table) \
    (dict_capacity_impl((table)))
#define DICT_REHASH_STEP(table, budget) \
    (dict_rehash_step_impl((table), (budget)))
#define DICT_RESERVE(table, entries) \
    (dict_reserve_impl((table), (entries)))
#define DICT_GET(table, key, out_value) \
    (dict_get_impl((table), (key), (out_value)))
#define DICT_PUT(table, key, value) \
    (dict_put_impl((table), (key), (value)))
#define DICT_REMOVE(table, key) \
    (dict_remove_impl((table), (key)))
#define DICT_TAKE(table, key, out_key, out_value) \
    (dict_take_impl((table), (key), (out_key), (out_value)))
#define DICT_FOREACH(table, visit_fn, ctx) \
    (dict_foreach_impl((table), (visit_fn), (ctx)))
#define DICT_CLEAR(table) \
    (dict_clear_impl((table)))
#define DICT_DESTROY(table) \
    (dict_destroy_impl((table)))

/* Object-like aliases: usable both as callbacks and in direct calls. */
#define DICT_HASH_STRING dict_hash_string_impl
#define DICT_EQUAL_STRING dict_equal_string_impl

/* Compatibility macros for existing source using the original API names. */
#define dict_config_default(hash_fn, equal_fn) \
    DICT_CONFIG_DEFAULT((hash_fn), (equal_fn))
#define dict_init(table, config) \
    DICT_INIT((table), (config))
#define dict_size(table) \
    DICT_SIZE((table))
#define dict_is_rehashing(table) \
    DICT_IS_REHASHING((table))
#define dict_capacity(table) \
    DICT_CAPACITY((table))
#define dict_rehash_step(table, budget) \
    DICT_REHASH_STEP((table), (budget))
#define dict_reserve(table, entries) \
    DICT_RESERVE((table), (entries))
#define dict_get(table, key, out_value) \
    DICT_GET((table), (key), (out_value))
#define dict_put(table, key, value) \
    DICT_PUT((table), (key), (value))
#define dict_remove(table, key) \
    DICT_REMOVE((table), (key))
#define dict_take(table, key, out_key, out_value) \
    DICT_TAKE((table), (key), (out_key), (out_value))
#define dict_foreach(table, visit_fn, ctx) \
    DICT_FOREACH((table), (visit_fn), (ctx))
#define dict_clear(table) \
    DICT_CLEAR((table))
#define dict_destroy(table) \
    DICT_DESTROY((table))
#define dict_hash_string DICT_HASH_STRING
#define dict_equal_string DICT_EQUAL_STRING

#ifdef __cplusplus
}
#endif
#endif /* DICT_H */
