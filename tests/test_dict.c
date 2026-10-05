/* Internal engine tests exercise borrowed/owned insertion directly.
 * Public callers use DICT_PUT (copy-only); see test_dict_copy.c. */
#include "dict.h"
#include <assert.h>
#include <stdio.h>

#define KEY_COUNT 257

typedef struct tracker {
    size_t allocations, fail_at, live, keys, values;
} tracker;
static void *tracked_alloc(size_t n, void *ctx) {
    tracker *t = (tracker *)ctx;
    void *p;
    ++t->allocations;
    if (t->fail_at && t->allocations == t->fail_at) return NULL;
    p = malloc(n);
    if (p) ++t->live;
    return p;
}
static void tracked_free(void *p, void *ctx) {
    tracker *t = (tracker *)ctx;
    if (p) { assert(t->live); --t->live; free(p); }
}
static uint32_t int_hash(const void *p, void *ctx) {
    (void)ctx;
    return (uint32_t)*(const int *)p * UINT32_C(2654435761);
}
static uint32_t collision_hash(const void *p, void *ctx) {
    (void)p; (void)ctx; return 3;
}
static int int_equal(const void *a, const void *b, void *ctx) {
    (void)ctx; return *(const int *)a == *(const int *)b;
}
static void destroy_key(void *p, void *ctx) {
    ++((tracker *)ctx)->keys; free(p);
}
static void destroy_value(void *p, void *ctx) {
    ++((tracker *)ctx)->values; free(p);
}
static int *new_int(int v) {
    int *p = (int *)malloc(sizeof(*p)); assert(p); *p = v; return p;
}
static void test_ownership(void) {
    tracker t = {0};
    dict h;
    dict_config c = DICT_CONFIG_DEFAULT(int_hash, int_equal);
    int *k = new_int(7), *v = new_int(10), query = 7;
    void *out_key, *out_value;
    c.ctx = &t; c.alloc = tracked_alloc; c.free = tracked_free;
    c.destroy_key = destroy_key; c.destroy_value = destroy_value;
    assert(DICT_INIT(&h, &c) == DICT_OK);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, k, v) == DICT_ADDED);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, k, v) == DICT_REPLACED);
    assert(t.keys == 0 && t.values == 0);
    k = new_int(7); v = new_int(20);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, k, v) == DICT_REPLACED);
    assert(t.keys == 1 && t.values == 1);
    assert(DICT_TAKE(&h, &query, &out_key, &out_value) == DICT_OK);
    assert(out_key == k && out_value == v);
    free(out_key); free(out_value);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, new_int(7), new_int(30)) == DICT_ADDED);
    assert(DICT_REMOVE(&h, &query) == DICT_OK);
    assert(t.keys == 2 && t.values == 2);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, new_int(7), NULL) == DICT_ADDED);
    assert(DICT_GET(&h, &query, &out_value) && out_value == NULL);
    assert(DICT_DESTROY(&h) == DICT_OK);
    assert(t.keys == 3 && t.values == 3 && t.live == 0);
}
static void test_oom(void) {
    size_t fail;
    /* Exhaustively fail each allocation along an insertion/resize path. */
    for (fail = 1; fail <= 55; ++fail) {
        tracker t = {0};
        dict h;
        dict_config c = DICT_CONFIG_DEFAULT(int_hash, int_equal);
        int keys[40], values[40], present[40] = {0};
        size_t i, count = 0;
        c.alloc = tracked_alloc; c.free = tracked_free; c.ctx = &t;
        t.fail_at = fail;
        assert(DICT_INIT(&h, &c) == DICT_OK);
        for (i = 0; i < 40; ++i) {
            dict_status s;
            keys[i] = (int)i; values[i] = (int)i + 100;
            s = RTOS_SYMBOL(dict_put_owned_impl)(&h, &keys[i], &values[i]);
            assert(s == DICT_ADDED || s == DICT_OOM);
            if (s == DICT_ADDED) { present[i] = 1; ++count; }
        }
        assert(DICT_SIZE(&h) == count);
        for (i = 0; i < 40; ++i) {
            void *v = NULL;
            assert(DICT_GET(&h, &keys[i], &v) == present[i]);
            if (present[i]) assert(v == &values[i]);
        }
        assert(DICT_DESTROY(&h) == DICT_OK && t.live == 0);
        assert(DICT_MEMORY_STATS_GET().live_bytes == 0);
        assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    }
}
typedef struct visit_context {
    dict *h;
    size_t count;
    int stop;
} visit_context;
static int visit(const void *key, void *value, void *ctx) {
    visit_context *v = (visit_context *)ctx;
    void *found;
    size_t old_used = v->h->tables[0].used;
    assert(DICT_GET(v->h, key, &found) && found == value);
    assert(v->h->tables[0].used == old_used);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(v->h, (void *)key, value) == DICT_BUSY);
    assert(DICT_REMOVE(v->h, key) == DICT_BUSY);
    assert(DICT_CLEAR(v->h) == DICT_BUSY);
    assert(DICT_DESTROY(v->h) == DICT_BUSY);
    assert(DICT_RESERVE(v->h, 1) == DICT_BUSY);
    assert(DICT_FOREACH(v->h, visit, ctx) == DICT_BUSY);
    assert(DICT_REHASH_STEP(v->h, 100) == 0);
    ++v->count;
    return v->stop;
}
static void test_manual_collision_rehash(void) {
    dict h;
    dict_config c = DICT_CONFIG_DEFAULT(collision_hash, int_equal);
    int keys[100], values[100], i;
    size_t previous;
    visit_context v = {&h, 0, 0};
    c.rehash_work = 0;
    assert(DICT_INIT(&h, &c) == DICT_OK);
    assert(DICT_RESERVE(&h, 100) == DICT_OK);
    for (i = 0; i < 100; ++i) {
        keys[i] = i; values[i] = i * 10;
        assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, &keys[i], &values[i]) == DICT_ADDED);
    }
    assert(DICT_RESERVE(&h, 200) == DICT_OK);
    assert(DICT_IS_REHASHING(&h));
    assert(DICT_RESERVE(&h, 1000) == DICT_BUSY);
    assert(DICT_FOREACH(&h, visit, &v) == DICT_OK && v.count == 100);
    v.count = 0; v.stop = 1;
    assert(DICT_FOREACH(&h, visit, &v) == DICT_OK && v.count == 1);
    while (DICT_IS_REHASHING(&h)) {
        previous = h.tables[0].used;
        assert(DICT_REHASH_STEP(&h, 1) == 1);
        if (DICT_IS_REHASHING(&h))
            assert(previous - h.tables[0].used <= 1);
    }
    for (i = 0; i < 100; ++i) {
        void *out;
        assert(DICT_GET(&h, &keys[i], &out) && out == &values[i]);
    }
    assert(DICT_RESERVE(&h, 1000) == DICT_OK);
    /* Delete from both tables, including the last old entry. */
    for (i = 0; i < 100; ++i) assert(DICT_REMOVE(&h, &keys[i]) == DICT_OK);
    assert(!DICT_IS_REHASHING(&h) && DICT_SIZE(&h) == 0);
    assert(DICT_CLEAR(&h) == DICT_OK);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, &keys[0], &values[0]) == DICT_ADDED);
    assert(DICT_DESTROY(&h) == DICT_OK);
}
static uint32_t rng_state = UINT32_C(1234567);
static uint32_t random_u32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static void test_random(dict_hash_fn hash, size_t work) {
    dict h;
    dict_config c = DICT_CONFIG_DEFAULT(hash, int_equal);
    int keys[KEY_COUNT], values[KEY_COUNT], present[KEY_COUNT] = {0};
    size_t step, count = 0;
    int i;
    c.rehash_work = work;
    assert(DICT_INIT(&h, &c) == DICT_OK);
    for (i = 0; i < KEY_COUNT; ++i) { keys[i] = i; values[i] = i + 1; }
    for (step = 0; step < 50000; ++step) {
        uint32_t r = random_u32();
        int k = (int)(r % KEY_COUNT);
        switch ((r >> 16) % 4) {
        case 0:
            assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, &keys[k], &values[k]) ==
                   (present[k] ? DICT_REPLACED : DICT_ADDED));
            if (!present[k]) { present[k] = 1; ++count; }
            break;
        case 1:
            assert(DICT_REMOVE(&h, &keys[k]) ==
                   (present[k] ? DICT_OK : DICT_NOT_FOUND));
            if (present[k]) { present[k] = 0; --count; }
            break;
        case 2: {
            void *value;
            assert(DICT_GET(&h, &keys[k], &value) == present[k]);
            assert(value == (present[k] ? &values[k] : NULL));
            break;
        }
        default: assert(DICT_REHASH_STEP(&h, 3) <= 3); break;
        }
        assert(DICT_SIZE(&h) == count);
        if (step % 997 == 0) {
            for (i = 0; i < KEY_COUNT; ++i) {
                void *value;
                assert(DICT_GET(&h, &keys[i], &value) == present[i]);
                assert(value == (present[i] ? &values[i] : NULL));
            }
        }
    }
    assert(DICT_DESTROY(&h) == DICT_OK);
}
static void test_invalid_and_strings(void) {
    dict h;
    dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    void *value;
    char key[] = "sensor", same[] = "sensor", val[] = "online";
    assert(DICT_INIT(NULL, &c) == DICT_INVALID);
    assert(DICT_INIT(&h, NULL) == DICT_INVALID);
    c.alloc = tracked_alloc;
    assert(DICT_INIT(&h, &c) == DICT_INVALID);
    c.alloc = NULL;
    assert(DICT_INIT(&h, &c) == DICT_OK);
    assert(DICT_GET(&h, key, &value) == 0 && value == NULL);
    assert(DICT_RESERVE(&h, SIZE_MAX) == DICT_OVERFLOW);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, key, val) == DICT_ADDED);
    assert(DICT_GET(&h, same, &value) == 1 && value == val);
    assert(DICT_TAKE(&h, key, NULL, &value) == DICT_INVALID);
    assert(DICT_TAKE(&h, key, &value, &value) == DICT_INVALID);
    assert(DICT_REMOVE(&h, "missing") == DICT_NOT_FOUND);
    assert(DICT_DESTROY(&h) == DICT_OK);
}
/* Verify side effects are evaluated once through the public macro boundary. */
static void test_macro_api(void) {
    dict tables[2], *table = tables;
    dict_config configs[2], *config = configs;
    dict_hash_fn hashes[2] = {DICT_HASH_STRING, DICT_HASH_STRING};
    dict_equal_fn equals[2] = {DICT_EQUAL_STRING, DICT_EQUAL_STRING};
    dict_hash_fn *hash = hashes;
    dict_equal_fn *equal = equals;
    char key[] = "macro-key";
    int value = 123;
    void *keys[2] = {key, NULL}, *values[2] = {&value, NULL};
    void **key_ptr = keys, **value_ptr = values;
    void *outputs[2] = {NULL, NULL}, **output = outputs;
    void *taken_keys[2], *taken_values[2];
    void **taken_key = taken_keys, **taken_value = taken_values;
    size_t entries = 8, budget = 1;

    configs[0] = DICT_CONFIG_DEFAULT(*hash++, *equal++);
    configs[0].rehash_work = 0;
    assert(hash == hashes + 1 && equal == equals + 1);
    assert(DICT_HASH_STRING(key, NULL) == hashes[0](key, NULL));
    assert(DICT_EQUAL_STRING(key, "macro-key", NULL));
    assert(DICT_INIT(table++, config++) == DICT_OK);
    assert(table == tables + 1 && config == configs + 1);

    table = tables;
    assert(DICT_RESERVE(table++, entries++) == DICT_OK);
    assert(table == tables + 1 && entries == 9);
    table = tables;
    assert(RTOS_SYMBOL(dict_put_owned_impl)(table++, *key_ptr++, *value_ptr++) == DICT_ADDED);
    assert(table == tables + 1 && key_ptr == keys + 1 &&
           value_ptr == values + 1);
    table = tables; key_ptr = keys;
    assert(DICT_GET(table++, *key_ptr++, output++));
    assert(table == tables + 1 && key_ptr == keys + 1 &&
           output == outputs + 1 && outputs[0] == &value);
    table = tables;
    assert(DICT_SIZE(table++) == 1 && table == tables + 1);
    table = tables;
    assert(DICT_CAPACITY(table++) == 8 && table == tables + 1);
    table = tables;
    assert(!DICT_IS_REHASHING(table++) && table == tables + 1);
    assert(DICT_RESERVE(tables, 16) == DICT_OK);
    table = tables;
    assert(DICT_REHASH_STEP(table++, budget++) <= 1);
    assert(table == tables + 1 && budget == 2);

    table = tables; key_ptr = keys;
    assert(DICT_TAKE(table++, *key_ptr++, taken_key++, taken_value++) == DICT_OK);
    assert(table == tables + 1 && key_ptr == keys + 1 &&
           taken_key == taken_keys + 1 && taken_value == taken_values + 1);
    assert(taken_keys[0] == key && taken_values[0] == &value);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(tables, key, &value) == DICT_ADDED);
    table = tables; key_ptr = keys;
    assert(DICT_REMOVE(table++, *key_ptr++) == DICT_OK);
    assert(table == tables + 1 && key_ptr == keys + 1);
    table = tables;
    assert(DICT_CLEAR(table++) == DICT_OK && table == tables + 1);
    table = tables;
    assert(DICT_DESTROY(table++) == DICT_OK && table == tables + 1);
}

extern int dict_other_translation_unit(void);
int main(void) {
    assert(dict_other_translation_unit() == 0);
    test_macro_api();
    test_invalid_and_strings();
    test_ownership();
    test_oom();
    test_manual_collision_rehash();
    test_random(int_hash, 2);
    test_random(int_hash, 0);
    test_random(collision_hash, 2);
    test_random(collision_hash, 0);
    assert(DICT_MEMORY_STATS_GET().live_bytes == 0);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    puts("All dict tests passed (200000 randomized operations).");
    return 0;
}
