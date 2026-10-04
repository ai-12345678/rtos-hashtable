#include "fht.h"
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
    fht h;
    fht_config c = FHT_CONFIG_DEFAULT(int_hash, int_equal);
    int *k = new_int(7), *v = new_int(10), query = 7;
    void *out_key, *out_value;
    c.ctx = &t; c.alloc = tracked_alloc; c.free = tracked_free;
    c.destroy_key = destroy_key; c.destroy_value = destroy_value;
    assert(FHT_INIT(&h, &c) == FHT_OK);
    assert(FHT_PUT(&h, k, v) == FHT_ADDED);
    assert(FHT_PUT(&h, k, v) == FHT_REPLACED);
    assert(t.keys == 0 && t.values == 0);
    k = new_int(7); v = new_int(20);
    assert(FHT_PUT(&h, k, v) == FHT_REPLACED);
    assert(t.keys == 1 && t.values == 1);
    assert(FHT_TAKE(&h, &query, &out_key, &out_value) == FHT_OK);
    assert(out_key == k && out_value == v);
    free(out_key); free(out_value);
    assert(FHT_PUT(&h, new_int(7), new_int(30)) == FHT_ADDED);
    assert(FHT_REMOVE(&h, &query) == FHT_OK);
    assert(t.keys == 2 && t.values == 2);
    assert(FHT_PUT(&h, new_int(7), NULL) == FHT_ADDED);
    assert(FHT_GET(&h, &query, &out_value) && out_value == NULL);
    assert(FHT_DESTROY(&h) == FHT_OK);
    assert(t.keys == 3 && t.values == 3 && t.live == 0);
}
static void test_oom(void) {
    size_t fail;
    /* Exhaustively fail each allocation along an insertion/resize path. */
    for (fail = 1; fail <= 55; ++fail) {
        tracker t = {0};
        fht h;
        fht_config c = FHT_CONFIG_DEFAULT(int_hash, int_equal);
        int keys[40], values[40], present[40] = {0};
        size_t i, count = 0;
        c.alloc = tracked_alloc; c.free = tracked_free; c.ctx = &t;
        t.fail_at = fail;
        assert(FHT_INIT(&h, &c) == FHT_OK);
        for (i = 0; i < 40; ++i) {
            fht_status s;
            keys[i] = (int)i; values[i] = (int)i + 100;
            s = FHT_PUT(&h, &keys[i], &values[i]);
            assert(s == FHT_ADDED || s == FHT_OOM);
            if (s == FHT_ADDED) { present[i] = 1; ++count; }
        }
        assert(FHT_SIZE(&h) == count);
        for (i = 0; i < 40; ++i) {
            void *v = NULL;
            assert(FHT_GET(&h, &keys[i], &v) == present[i]);
            if (present[i]) assert(v == &values[i]);
        }
        assert(FHT_DESTROY(&h) == FHT_OK && t.live == 0);
    }
}
typedef struct visit_context {
    fht *h;
    size_t count;
    int stop;
} visit_context;
static int visit(const void *key, void *value, void *ctx) {
    visit_context *v = (visit_context *)ctx;
    void *found;
    size_t old_used = v->h->tables[0].used;
    assert(FHT_GET(v->h, key, &found) && found == value);
    assert(v->h->tables[0].used == old_used);
    assert(FHT_PUT(v->h, (void *)key, value) == FHT_BUSY);
    assert(FHT_REMOVE(v->h, key) == FHT_BUSY);
    assert(FHT_CLEAR(v->h) == FHT_BUSY);
    assert(FHT_DESTROY(v->h) == FHT_BUSY);
    assert(FHT_RESERVE(v->h, 1) == FHT_BUSY);
    assert(FHT_FOREACH(v->h, visit, ctx) == FHT_BUSY);
    assert(FHT_REHASH_STEP(v->h, 100) == 0);
    ++v->count;
    return v->stop;
}
static void test_manual_collision_rehash(void) {
    fht h;
    fht_config c = FHT_CONFIG_DEFAULT(collision_hash, int_equal);
    int keys[100], values[100], i;
    size_t previous;
    visit_context v = {&h, 0, 0};
    c.rehash_work = 0;
    assert(FHT_INIT(&h, &c) == FHT_OK);
    assert(FHT_RESERVE(&h, 100) == FHT_OK);
    for (i = 0; i < 100; ++i) {
        keys[i] = i; values[i] = i * 10;
        assert(FHT_PUT(&h, &keys[i], &values[i]) == FHT_ADDED);
    }
    assert(FHT_RESERVE(&h, 200) == FHT_OK);
    assert(FHT_IS_REHASHING(&h));
    assert(FHT_RESERVE(&h, 1000) == FHT_BUSY);
    assert(FHT_FOREACH(&h, visit, &v) == FHT_OK && v.count == 100);
    v.count = 0; v.stop = 1;
    assert(FHT_FOREACH(&h, visit, &v) == FHT_OK && v.count == 1);
    while (FHT_IS_REHASHING(&h)) {
        previous = h.tables[0].used;
        assert(FHT_REHASH_STEP(&h, 1) == 1);
        if (FHT_IS_REHASHING(&h))
            assert(previous - h.tables[0].used <= 1);
    }
    for (i = 0; i < 100; ++i) {
        void *out;
        assert(FHT_GET(&h, &keys[i], &out) && out == &values[i]);
    }
    assert(FHT_RESERVE(&h, 1000) == FHT_OK);
    /* Delete from both tables, including the last old entry. */
    for (i = 0; i < 100; ++i) assert(FHT_REMOVE(&h, &keys[i]) == FHT_OK);
    assert(!FHT_IS_REHASHING(&h) && FHT_SIZE(&h) == 0);
    assert(FHT_CLEAR(&h) == FHT_OK);
    assert(FHT_PUT(&h, &keys[0], &values[0]) == FHT_ADDED);
    assert(FHT_DESTROY(&h) == FHT_OK);
}
static uint32_t rng_state = UINT32_C(1234567);
static uint32_t random_u32(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
static void test_random(fht_hash_fn hash, size_t work) {
    fht h;
    fht_config c = FHT_CONFIG_DEFAULT(hash, int_equal);
    int keys[KEY_COUNT], values[KEY_COUNT], present[KEY_COUNT] = {0};
    size_t step, count = 0;
    int i;
    c.rehash_work = work;
    assert(FHT_INIT(&h, &c) == FHT_OK);
    for (i = 0; i < KEY_COUNT; ++i) { keys[i] = i; values[i] = i + 1; }
    for (step = 0; step < 50000; ++step) {
        uint32_t r = random_u32();
        int k = (int)(r % KEY_COUNT);
        switch ((r >> 16) % 4) {
        case 0:
            assert(FHT_PUT(&h, &keys[k], &values[k]) ==
                   (present[k] ? FHT_REPLACED : FHT_ADDED));
            if (!present[k]) { present[k] = 1; ++count; }
            break;
        case 1:
            assert(FHT_REMOVE(&h, &keys[k]) ==
                   (present[k] ? FHT_OK : FHT_NOT_FOUND));
            if (present[k]) { present[k] = 0; --count; }
            break;
        case 2: {
            void *value;
            assert(FHT_GET(&h, &keys[k], &value) == present[k]);
            assert(value == (present[k] ? &values[k] : NULL));
            break;
        }
        default: assert(FHT_REHASH_STEP(&h, 3) <= 3); break;
        }
        assert(FHT_SIZE(&h) == count);
        if (step % 997 == 0) {
            for (i = 0; i < KEY_COUNT; ++i) {
                void *value;
                assert(FHT_GET(&h, &keys[i], &value) == present[i]);
                assert(value == (present[i] ? &values[i] : NULL));
            }
        }
    }
    assert(FHT_DESTROY(&h) == FHT_OK);
}
static void test_invalid_and_strings(void) {
    fht h;
    fht_config c = FHT_CONFIG_DEFAULT(FHT_HASH_STRING, FHT_EQUAL_STRING);
    void *value;
    char key[] = "sensor", same[] = "sensor", val[] = "online";
    assert(FHT_INIT(NULL, &c) == FHT_INVALID);
    assert(FHT_INIT(&h, NULL) == FHT_INVALID);
    c.alloc = tracked_alloc;
    assert(FHT_INIT(&h, &c) == FHT_INVALID);
    c.alloc = NULL;
    assert(FHT_INIT(&h, &c) == FHT_OK);
    assert(FHT_GET(&h, key, &value) == 0 && value == NULL);
    assert(FHT_RESERVE(&h, SIZE_MAX) == FHT_OVERFLOW);
    assert(FHT_PUT(&h, key, val) == FHT_ADDED);
    assert(FHT_GET(&h, same, &value) == 1 && value == val);
    assert(FHT_TAKE(&h, key, NULL, &value) == FHT_INVALID);
    assert(FHT_TAKE(&h, key, &value, &value) == FHT_INVALID);
    assert(FHT_REMOVE(&h, "missing") == FHT_NOT_FOUND);
    assert(FHT_DESTROY(&h) == FHT_OK);
}
/* Verify side effects are evaluated once through the public macro boundary. */
static void test_macro_api(void) {
    fht tables[2], *table = tables;
    fht_config configs[2], *config = configs;
    fht_hash_fn hashes[2] = {FHT_HASH_STRING, FHT_HASH_STRING};
    fht_equal_fn equals[2] = {FHT_EQUAL_STRING, FHT_EQUAL_STRING};
    fht_hash_fn *hash = hashes;
    fht_equal_fn *equal = equals;
    char key[] = "macro-key";
    int value = 123;
    void *keys[2] = {key, NULL}, *values[2] = {&value, NULL};
    void **key_ptr = keys, **value_ptr = values;
    void *outputs[2] = {NULL, NULL}, **output = outputs;
    void *taken_keys[2], *taken_values[2];
    void **taken_key = taken_keys, **taken_value = taken_values;
    size_t entries = 8, budget = 1;

    configs[0] = FHT_CONFIG_DEFAULT(*hash++, *equal++);
    configs[0].rehash_work = 0;
    assert(hash == hashes + 1 && equal == equals + 1);
    assert(FHT_HASH_STRING(key, NULL) == hashes[0](key, NULL));
    assert(FHT_EQUAL_STRING(key, "macro-key", NULL));
    assert(FHT_INIT(table++, config++) == FHT_OK);
    assert(table == tables + 1 && config == configs + 1);

    table = tables;
    assert(FHT_RESERVE(table++, entries++) == FHT_OK);
    assert(table == tables + 1 && entries == 9);
    table = tables;
    assert(FHT_PUT(table++, *key_ptr++, *value_ptr++) == FHT_ADDED);
    assert(table == tables + 1 && key_ptr == keys + 1 &&
           value_ptr == values + 1);
    table = tables; key_ptr = keys;
    assert(FHT_GET(table++, *key_ptr++, output++));
    assert(table == tables + 1 && key_ptr == keys + 1 &&
           output == outputs + 1 && outputs[0] == &value);
    table = tables;
    assert(FHT_SIZE(table++) == 1 && table == tables + 1);
    table = tables;
    assert(FHT_CAPACITY(table++) == 8 && table == tables + 1);
    table = tables;
    assert(!FHT_IS_REHASHING(table++) && table == tables + 1);
    assert(FHT_RESERVE(tables, 16) == FHT_OK);
    table = tables;
    assert(FHT_REHASH_STEP(table++, budget++) <= 1);
    assert(table == tables + 1 && budget == 2);

    table = tables; key_ptr = keys;
    assert(FHT_TAKE(table++, *key_ptr++, taken_key++, taken_value++) == FHT_OK);
    assert(table == tables + 1 && key_ptr == keys + 1 &&
           taken_key == taken_keys + 1 && taken_value == taken_values + 1);
    assert(taken_keys[0] == key && taken_values[0] == &value);
    assert(FHT_PUT(tables, key, &value) == FHT_ADDED);
    table = tables; key_ptr = keys;
    assert(FHT_REMOVE(table++, *key_ptr++) == FHT_OK);
    assert(table == tables + 1 && key_ptr == keys + 1);
    table = tables;
    assert(FHT_CLEAR(table++) == FHT_OK && table == tables + 1);
    table = tables;
    assert(FHT_DESTROY(table++) == FHT_OK && table == tables + 1);
}

extern int fht_other_translation_unit(void);
int main(void) {
    assert(fht_other_translation_unit() == 0);
    test_macro_api();
    test_invalid_and_strings();
    test_ownership();
    test_oom();
    test_manual_collision_rehash();
    test_random(int_hash, 2);
    test_random(int_hash, 0);
    test_random(collision_hash, 2);
    test_random(collision_hash, 0);
    puts("All fht tests passed (200000 randomized operations).");
    return 0;
}
