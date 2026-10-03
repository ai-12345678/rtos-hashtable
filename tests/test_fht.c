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
    fht_config c = fht_config_default(int_hash, int_equal);
    int *k = new_int(7), *v = new_int(10), query = 7;
    void *out_key, *out_value;
    c.ctx = &t; c.alloc = tracked_alloc; c.free = tracked_free;
    c.destroy_key = destroy_key; c.destroy_value = destroy_value;
    assert(fht_init(&h, &c) == FHT_OK);
    assert(fht_put(&h, k, v) == FHT_ADDED);
    assert(fht_put(&h, k, v) == FHT_REPLACED);
    assert(t.keys == 0 && t.values == 0);
    k = new_int(7); v = new_int(20);
    assert(fht_put(&h, k, v) == FHT_REPLACED);
    assert(t.keys == 1 && t.values == 1);
    assert(fht_take(&h, &query, &out_key, &out_value) == FHT_OK);
    assert(out_key == k && out_value == v);
    free(out_key); free(out_value);
    assert(fht_put(&h, new_int(7), new_int(30)) == FHT_ADDED);
    assert(fht_remove(&h, &query) == FHT_OK);
    assert(t.keys == 2 && t.values == 2);
    assert(fht_put(&h, new_int(7), NULL) == FHT_ADDED);
    assert(fht_get(&h, &query, &out_value) && out_value == NULL);
    assert(fht_destroy(&h) == FHT_OK);
    assert(t.keys == 3 && t.values == 3 && t.live == 0);
}
static void test_oom(void) {
    size_t fail;
    /* Exhaustively fail each allocation along an insertion/resize path. */
    for (fail = 1; fail <= 55; ++fail) {
        tracker t = {0};
        fht h;
        fht_config c = fht_config_default(int_hash, int_equal);
        int keys[40], values[40], present[40] = {0};
        size_t i, count = 0;
        c.alloc = tracked_alloc; c.free = tracked_free; c.ctx = &t;
        t.fail_at = fail;
        assert(fht_init(&h, &c) == FHT_OK);
        for (i = 0; i < 40; ++i) {
            fht_status s;
            keys[i] = (int)i; values[i] = (int)i + 100;
            s = fht_put(&h, &keys[i], &values[i]);
            assert(s == FHT_ADDED || s == FHT_OOM);
            if (s == FHT_ADDED) { present[i] = 1; ++count; }
        }
        assert(fht_size(&h) == count);
        for (i = 0; i < 40; ++i) {
            void *v = NULL;
            assert(fht_get(&h, &keys[i], &v) == present[i]);
            if (present[i]) assert(v == &values[i]);
        }
        assert(fht_destroy(&h) == FHT_OK && t.live == 0);
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
    assert(fht_get(v->h, key, &found) && found == value);
    assert(v->h->tables[0].used == old_used);
    assert(fht_put(v->h, (void *)key, value) == FHT_BUSY);
    assert(fht_remove(v->h, key) == FHT_BUSY);
    assert(fht_clear(v->h) == FHT_BUSY);
    assert(fht_destroy(v->h) == FHT_BUSY);
    assert(fht_reserve(v->h, 1) == FHT_BUSY);
    assert(fht_foreach(v->h, visit, ctx) == FHT_BUSY);
    assert(fht_rehash_step(v->h, 100) == 0);
    ++v->count;
    return v->stop;
}
static void test_manual_collision_rehash(void) {
    fht h;
    fht_config c = fht_config_default(collision_hash, int_equal);
    int keys[100], values[100], i;
    size_t previous;
    visit_context v = {&h, 0, 0};
    c.rehash_work = 0;
    assert(fht_init(&h, &c) == FHT_OK);
    assert(fht_reserve(&h, 100) == FHT_OK);
    for (i = 0; i < 100; ++i) {
        keys[i] = i; values[i] = i * 10;
        assert(fht_put(&h, &keys[i], &values[i]) == FHT_ADDED);
    }
    assert(fht_reserve(&h, 200) == FHT_OK);
    assert(fht_is_rehashing(&h));
    assert(fht_reserve(&h, 1000) == FHT_BUSY);
    assert(fht_foreach(&h, visit, &v) == FHT_OK && v.count == 100);
    v.count = 0; v.stop = 1;
    assert(fht_foreach(&h, visit, &v) == FHT_OK && v.count == 1);
    while (fht_is_rehashing(&h)) {
        previous = h.tables[0].used;
        assert(fht_rehash_step(&h, 1) == 1);
        if (fht_is_rehashing(&h))
            assert(previous - h.tables[0].used <= 1);
    }
    for (i = 0; i < 100; ++i) {
        void *out;
        assert(fht_get(&h, &keys[i], &out) && out == &values[i]);
    }
    assert(fht_reserve(&h, 1000) == FHT_OK);
    /* Delete from both tables, including the last old entry. */
    for (i = 0; i < 100; ++i) assert(fht_remove(&h, &keys[i]) == FHT_OK);
    assert(!fht_is_rehashing(&h) && fht_size(&h) == 0);
    assert(fht_clear(&h) == FHT_OK);
    assert(fht_put(&h, &keys[0], &values[0]) == FHT_ADDED);
    assert(fht_destroy(&h) == FHT_OK);
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
    fht_config c = fht_config_default(hash, int_equal);
    int keys[KEY_COUNT], values[KEY_COUNT], present[KEY_COUNT] = {0};
    size_t step, count = 0;
    int i;
    c.rehash_work = work;
    assert(fht_init(&h, &c) == FHT_OK);
    for (i = 0; i < KEY_COUNT; ++i) { keys[i] = i; values[i] = i + 1; }
    for (step = 0; step < 50000; ++step) {
        uint32_t r = random_u32();
        int k = (int)(r % KEY_COUNT);
        switch ((r >> 16) % 4) {
        case 0:
            assert(fht_put(&h, &keys[k], &values[k]) ==
                   (present[k] ? FHT_REPLACED : FHT_ADDED));
            if (!present[k]) { present[k] = 1; ++count; }
            break;
        case 1:
            assert(fht_remove(&h, &keys[k]) ==
                   (present[k] ? FHT_OK : FHT_NOT_FOUND));
            if (present[k]) { present[k] = 0; --count; }
            break;
        case 2: {
            void *value;
            assert(fht_get(&h, &keys[k], &value) == present[k]);
            assert(value == (present[k] ? &values[k] : NULL));
            break;
        }
        default: assert(fht_rehash_step(&h, 3) <= 3); break;
        }
        assert(fht_size(&h) == count);
        if (step % 997 == 0) {
            for (i = 0; i < KEY_COUNT; ++i) {
                void *value;
                assert(fht_get(&h, &keys[i], &value) == present[i]);
                assert(value == (present[i] ? &values[i] : NULL));
            }
        }
    }
    assert(fht_destroy(&h) == FHT_OK);
}
static void test_invalid_and_strings(void) {
    fht h;
    fht_config c = fht_config_default(fht_hash_string, fht_equal_string);
    void *value;
    char key[] = "sensor", same[] = "sensor", val[] = "online";
    assert(fht_init(NULL, &c) == FHT_INVALID);
    assert(fht_init(&h, NULL) == FHT_INVALID);
    c.alloc = tracked_alloc;
    assert(fht_init(&h, &c) == FHT_INVALID);
    c.alloc = NULL;
    assert(fht_init(&h, &c) == FHT_OK);
    assert(fht_get(&h, key, &value) == 0 && value == NULL);
    assert(fht_reserve(&h, SIZE_MAX) == FHT_OVERFLOW);
    assert(fht_put(&h, key, val) == FHT_ADDED);
    assert(fht_get(&h, same, &value) == 1 && value == val);
    assert(fht_take(&h, key, NULL, &value) == FHT_INVALID);
    assert(fht_take(&h, key, &value, &value) == FHT_INVALID);
    assert(fht_remove(&h, "missing") == FHT_NOT_FOUND);
    assert(fht_destroy(&h) == FHT_OK);
}
extern int fht_other_translation_unit(void);
int main(void) {
    assert(fht_other_translation_unit() == 0);
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
