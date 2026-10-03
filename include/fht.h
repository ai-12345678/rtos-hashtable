#ifndef FHT_H
#define FHT_H

/* C99 header-only chained hash table, inspired by Redis dict's two-table
 * incremental rehash. Independent implementation, no Redis dependency.
 * Not thread safe: serialize ALL operations on a shared table, including get.
 * Hash/equality/destructor/allocator callbacks must not reenter the table.
 * Keys must retain a stable hash/equality while stored. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*fht_hash_fn)(const void *key, void *ctx);
typedef int (*fht_equal_fn)(const void *a, const void *b, void *ctx);
typedef void (*fht_destroy_fn)(void *object, void *ctx);
typedef void *(*fht_alloc_fn)(size_t bytes, void *ctx);
typedef void (*fht_free_fn)(void *ptr, void *ctx);

typedef enum fht_status {
    FHT_OK = 0, FHT_ADDED = 1, FHT_REPLACED = 2,
    FHT_INVALID = -1, FHT_OOM = -2, FHT_OVERFLOW = -3,
    FHT_BUSY = -4, FHT_NOT_FOUND = -5
} fht_status;

typedef struct fht_config {
    fht_hash_fn hash;
    fht_equal_fn equal;
    fht_destroy_fn destroy_key;
    fht_destroy_fn destroy_value;
    fht_alloc_fn alloc;           /* alloc/free must both be set, or both NULL */
    fht_free_fn free;
    void *ctx;                    /* passed to every callback */
    size_t rehash_work;            /* per-operation work units; 0 = manual */
} fht_config;

typedef struct fht_entry {
    struct fht_entry *next;
    void *key;
    void *value;
} fht_entry;

typedef struct fht_table {
    fht_entry **buckets;
    size_t capacity;
    size_t used;
} fht_table;

typedef struct fht {
    fht_config config;
    fht_table tables[2];
    size_t rehash_index;
    unsigned visiting;
} fht;

static inline void *fht_default_alloc(size_t bytes, void *ctx) {
    (void)ctx;
    return malloc(bytes);
}
static inline void fht_default_free(void *ptr, void *ctx) {
    (void)ctx;
    free(ptr);
}
static inline fht_config fht_config_default(fht_hash_fn hash,
                                            fht_equal_fn equal) {
    fht_config c;
    memset(&c, 0, sizeof(c));
    c.hash = hash;
    c.equal = equal;
    c.rehash_work = 2;
    return c;
}
/* Initialize an uninitialized or destroyed object; no allocation occurs. */
static inline fht_status fht_init(fht *h, const fht_config *config) {
    if (!h || !config || !config->hash || !config->equal ||
        (!!config->alloc != !!config->free)) return FHT_INVALID;
    memset(h, 0, sizeof(*h));
    h->config = *config;
    if (!h->config.alloc) {
        h->config.alloc = fht_default_alloc;
        h->config.free = fht_default_free;
    }
    return FHT_OK;
}
static inline size_t fht_size(const fht *h) {
    return h->tables[0].used + h->tables[1].used;
}
static inline int fht_is_rehashing(const fht *h) {
    return h->tables[1].buckets != NULL;
}
static inline size_t fht_capacity(const fht *h) {
    return h->tables[fht_is_rehashing(h) ? 1 : 0].capacity;
}
static inline void fht_finish_rehash_(fht *h) {
    if (fht_is_rehashing(h) && h->tables[0].used == 0) {
        h->config.free(h->tables[0].buckets, h->config.ctx);
        h->tables[0] = h->tables[1];
        memset(&h->tables[1], 0, sizeof(h->tables[1]));
        h->rehash_index = 0;
    }
}
/* At most budget units: one unit moves ONE entry or skips ONE empty bucket.
 * Allocation-free. Does not bound wall time of allocators or callbacks. */
static inline size_t fht_rehash_step(fht *h, size_t budget) {
    size_t work = 0;
    if (h->visiting) return 0;
    fht_finish_rehash_(h);
    while (fht_is_rehashing(h) && work < budget) {
        fht_table *old = &h->tables[0], *next = &h->tables[1];
        fht_entry *e = old->buckets[h->rehash_index];
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
        fht_finish_rehash_(h);
    }
    return work;
}
/* Request capacity for at least entries at load factor <= 1. During an
 * active rehash, larger requests return BUSY; call rehash_step and retry.
 * Allocating/zeroing the new bucket array is synchronous, unlike migration. */
static inline fht_status fht_reserve(fht *h, size_t entries) {
    size_t capacity = 4;
    fht_entry **buckets;
    if (h->visiting) return FHT_BUSY;
    if (entries <= fht_capacity(h)) return FHT_OK;
    if (fht_is_rehashing(h)) return FHT_BUSY;
    while (capacity < entries) {
        if (capacity > SIZE_MAX / 2) return FHT_OVERFLOW;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*buckets)) return FHT_OVERFLOW;
    buckets = (fht_entry **)h->config.alloc(capacity * sizeof(*buckets),
                                           h->config.ctx);
    if (!buckets) return FHT_OOM;
    memset(buckets, 0, capacity * sizeof(*buckets));
    if (!h->tables[0].buckets) {
        h->tables[0].buckets = buckets;
        h->tables[0].capacity = capacity;
    } else {
        h->tables[1].buckets = buckets;
        h->tables[1].capacity = capacity;
        h->rehash_index = 0;
        fht_finish_rehash_(h);
    }
    return FHT_OK;
}
static inline fht_entry **fht_find_(fht *h, const void *key,
                                    uint32_t hash, unsigned *table_index) {
    unsigned i;
    for (i = 0; i < 2; ++i) {
        fht_table *t = &h->tables[i];
        fht_entry **link;
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
static inline int fht_get(fht *h, const void *key, void **value) {
    fht_entry **link;
    fht_rehash_step(h, h->config.rehash_work);
    link = fht_find_(h, key, h->config.hash(key, h->config.ctx), NULL);
    if (value) *value = link ? (*link)->value : NULL;
    return link != NULL;
}
/* On success table owns key/value per configured destructors. On error
 * ownership stays with caller. Replacement adopts BOTH new key and value,
 * destroying old objects unless their corresponding pointers are unchanged.
 * Key/value ownership must be independent (no aliases between owned objects). */
static inline fht_status fht_put(fht *h, void *key, void *value) {
    uint32_t hash;
    fht_entry **link, *e;
    fht_table *t;
    size_t bucket, size;
    fht_status status;
    if (h->visiting) return FHT_BUSY;
    fht_rehash_step(h, h->config.rehash_work);
    hash = h->config.hash(key, h->config.ctx);
    link = fht_find_(h, key, hash, NULL);
    if (link) {
        e = *link;
        if (e->key != key && h->config.destroy_key)
            h->config.destroy_key(e->key, h->config.ctx);
        if (e->value != value && h->config.destroy_value)
            h->config.destroy_value(e->value, h->config.ctx);
        e->key = key;
        e->value = value;
        return FHT_REPLACED;
    }
    size = fht_size(h);
    if (size == SIZE_MAX) return FHT_OVERFLOW;
    e = (fht_entry *)h->config.alloc(sizeof(*e), h->config.ctx);
    if (!e) return FHT_OOM;
    /* While migrating, allow chains to grow instead of forcing completion. */
    if (!fht_is_rehashing(h) && size >= h->tables[0].capacity) {
        status = fht_reserve(h, size + 1);
        if (status != FHT_OK) {
            h->config.free(e, h->config.ctx);
            return status;
        }
    }
    t = &h->tables[fht_is_rehashing(h) ? 1 : 0];
    bucket = (size_t)hash & (t->capacity - 1);
    e->key = key;
    e->value = value;
    e->next = t->buckets[bucket];
    t->buckets[bucket] = e;
    ++t->used;
    return FHT_ADDED;
}
static inline fht_status fht_remove_(fht *h, const void *key,
                                      void **out_key, void **out_value,
                                      int take) {
    fht_entry **link, *e;
    unsigned table_index = 0;
    if (out_key) *out_key = NULL;
    if (out_value) *out_value = NULL;
    if (h->visiting) return FHT_BUSY;
    fht_rehash_step(h, h->config.rehash_work);
    link = fht_find_(h, key, h->config.hash(key, h->config.ctx), &table_index);
    if (!link) return FHT_NOT_FOUND;
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
    h->config.free(e, h->config.ctx);
    fht_finish_rehash_(h);
    return FHT_OK;
}
static inline fht_status fht_remove(fht *h, const void *key) {
    return fht_remove_(h, key, NULL, NULL, 0);
}
/* Transfer BOTH objects to caller without destructors. Both outputs required. */
static inline fht_status fht_take(fht *h, const void *key,
                                  void **out_key, void **out_value) {
    if (!out_key || !out_value || out_key == out_value) return FHT_INVALID;
    return fht_remove_(h, key, out_key, out_value, 1);
}
/* Visit each entry exactly once, even during rehash. Return nonzero to stop.
 * Callback may call get/size but mutation/reserve/clear returns BUSY.
 * Recursive visits are rejected. Never destroy the table inside a callback. */
typedef int (*fht_visit_fn)(const void *key, void *value, void *ctx);
static inline fht_status fht_foreach(fht *h, fht_visit_fn visit, void *ctx) {
    unsigned i;
    if (!visit) return FHT_INVALID;
    if (h->visiting) return FHT_BUSY;
    h->visiting = 1;
    for (i = 0; i < 2; ++i) {
        size_t b;
        for (b = 0; b < h->tables[i].capacity; ++b) {
            fht_entry *e = h->tables[i].buckets[b];
            while (e) {
                if (visit(e->key, e->value, ctx)) {
                    h->visiting = 0;
                    return FHT_OK;
                }
                e = e->next;
            }
        }
    }
    h->visiting = 0;
    return FHT_OK;
}
/* Free all storage and owned objects, keep config; table can be reused. */
static inline fht_status fht_clear(fht *h) {
    unsigned i;
    if (h->visiting) return FHT_BUSY;
    for (i = 0; i < 2; ++i) {
        size_t b;
        for (b = 0; b < h->tables[i].capacity; ++b) {
            fht_entry *e = h->tables[i].buckets[b];
            while (e) {
                fht_entry *next = e->next;
                if (h->config.destroy_key)
                    h->config.destroy_key(e->key, h->config.ctx);
                if (h->config.destroy_value)
                    h->config.destroy_value(e->value, h->config.ctx);
                h->config.free(e, h->config.ctx);
                e = next;
            }
        }
        if (h->tables[i].buckets)
            h->config.free(h->tables[i].buckets, h->config.ctx);
        memset(&h->tables[i], 0, sizeof(h->tables[i]));
    }
    h->rehash_index = 0;
    return FHT_OK;
}
static inline fht_status fht_destroy(fht *h) {
    fht_status status = fht_clear(h);
    if (status == FHT_OK) memset(h, 0, sizeof(*h));
    return status;
}
/* Convenience hash for trusted string keys: FNV-1a, NOT collision resistant.
 * For untrusted keys provide a keyed hash, e.g. SipHash. */
static inline uint32_t fht_hash_string(const void *key, void *ctx) {
    const unsigned char *s = (const unsigned char *)key;
    uint32_t hash = UINT32_C(2166136261);
    (void)ctx;
    while (*s) { hash ^= *s++; hash *= UINT32_C(16777619); }
    return hash;
}
static inline int fht_equal_string(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return strcmp((const char *)a, (const char *)b) == 0;
}
#ifdef __cplusplus
}
#endif
#endif /* FHT_H */
