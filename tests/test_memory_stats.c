/* Internal engine tests exercise borrowed/owned insertion directly.
 * Public callers use Dict_put (copy-only); see test_dict_copy.c. */
#include "dict.h"
#include <assert.h>
#include <stdio.h>

extern int dict_other_table_create(void);
extern int dict_other_table_destroy(void);
extern dict_memory_stats dict_other_memory_stats(void);

typedef struct allocator {
    size_t attempts, fail_at, bytes, blocks;
    void *pointers[32];
    size_t sizes[32];
} allocator;
static void *test_alloc(size_t bytes, void *ctx) {
    allocator *a = (allocator *)ctx;
    size_t i;
    void *p;
    ++a->attempts;
    if (a->attempts == a->fail_at) return NULL;
    p = malloc(bytes);
    assert(p);
    for (i = 0; i < 32; ++i) if (!a->pointers[i]) break;
    assert(i < 32);
    a->pointers[i] = p; a->sizes[i] = bytes;
    a->bytes += bytes; ++a->blocks;
    return p;
}
static void test_free(void *p, void *ctx) {
    allocator *a = (allocator *)ctx;
    size_t i;
    for (i = 0; i < 32; ++i) if (a->pointers[i] == p) break;
    assert(i < 32 && p);
    a->bytes -= a->sizes[i]; --a->blocks;
    a->pointers[i] = NULL;
    free(p);
}
static void expect(size_t bytes, size_t blocks) {
    dict_memory_stats s = DICT_MEMORY_STATS_GET();
#if DICT_ENABLE_MEMORY_STATS
    assert(s.live_bytes == bytes && s.live_blocks == blocks);
    assert(s.peak_bytes >= s.live_bytes);
    assert(dict_other_memory_stats().live_bytes == bytes);
#else
    (void)bytes; (void)blocks;
    assert(s.live_bytes == 0 && s.live_blocks == 0 && s.peak_bytes == 0);
#endif
}
static int busy_visit(const void *k, void *v, void *ctx) {
    dict *h = (dict *)ctx;
    dict_memory_stats before = DICT_MEMORY_STATS_GET(), after;
    (void)k; (void)v;
    assert(DICT_CLEAR(h) == DICT_BUSY);
    assert(DICT_DESTROY(h) == DICT_BUSY);
    after = DICT_MEMORY_STATS_GET();
    assert(before.live_bytes == after.live_bytes);
    assert(before.live_blocks == after.live_blocks);
    return 1;
}
static void test_shared_tables(void) {
    dict h;
    allocator a = {0};
    dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    char key[] = "local", replacement[] = "local";
    int value = 1, updated = 2;
    void *out_key, *out_value;
    size_t other_bytes = 4 * sizeof(dict_entry *) + sizeof(dict_entry);
    c.alloc = test_alloc; c.free = test_free; c.ctx = &a;
    assert(DICT_MEMORY_STATS_RESET() == DICT_OK);
    assert(DICT_INIT(&h, &c) == DICT_OK);
    expect(0, 0);
    assert(dict_other_table_create() == 0);
    expect(other_bytes, 2);
    assert(DICT_RESERVE(&h, 8) == DICT_OK);
    expect(other_bytes + a.bytes, 2 + a.blocks);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, key, &value) == DICT_ADDED);
    assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, replacement, &updated) == DICT_REPLACED);
    expect(other_bytes + 8 * sizeof(dict_entry *) + sizeof(dict_entry), 4);
#if DICT_ENABLE_MEMORY_STATS
    assert(DICT_MEMORY_STATS_RESET() == DICT_BUSY);
#endif
    assert(DICT_FOREACH(&h, busy_visit, &h) == DICT_OK);
    assert(DICT_TAKE(&h, key, &out_key, &out_value) == DICT_OK);
    assert(out_key == replacement && out_value == &updated);
    expect(other_bytes + a.bytes, 2 + a.blocks);
    assert(DICT_CLEAR(&h) == DICT_OK && a.bytes == 0 && a.blocks == 0);
    expect(other_bytes, 2);
    assert(DICT_DESTROY(&h) == DICT_OK);
    assert(dict_other_table_destroy() == 0);
    expect(0, 0);
#if DICT_ENABLE_MEMORY_STATS
    assert(DICT_MEMORY_STATS_GET().peak_bytes != 0);
#endif
    assert(DICT_MEMORY_STATS_RESET() == DICT_OK);
    assert(DICT_MEMORY_STATS_GET().peak_bytes == 0);
}
static void test_oom_and_rehash(void) {
    size_t failure;
    for (failure = 1; failure <= 12; ++failure) {
        dict h;
        allocator a = {0};
        dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
        char keys[5][2] = {"a", "b", "c", "d", "e"};
        size_t i;
        c.alloc = test_alloc; c.free = test_free; c.ctx = &a;
        c.rehash_work = 0; a.fail_at = failure;
        assert(DICT_INIT(&h, &c) == DICT_OK);
        for (i = 0; i < 5; ++i) {
            dict_status s = RTOS_SYMBOL(dict_put_owned_impl)(&h, keys[i], NULL);
            assert(s == DICT_ADDED || s == DICT_OOM);
            expect(a.bytes, a.blocks);
        }
        if (DICT_IS_REHASHING(&h)) {
            assert(a.blocks == DICT_SIZE(&h) + 2);
            expect(a.bytes, a.blocks);
        }
        /* Exercise both cleanup while migrating and freeing the old buckets. */
        if (failure % 2 == 0)
            while (DICT_IS_REHASHING(&h)) {
                assert(DICT_REHASH_STEP(&h, 1) <= 1);
                expect(a.bytes, a.blocks);
            }
        assert(DICT_DESTROY(&h) == DICT_OK);
        assert(a.bytes == 0 && a.blocks == 0);
        expect(0, 0);
    }
}
int main(void) {
    test_shared_tables();
    test_oom_and_rehash();
    puts("Memory accounting tests passed.");
    return 0;
}
