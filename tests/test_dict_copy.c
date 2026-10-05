#include "dict.h"
#include "sds.h"
#include <assert.h>
#include <stdio.h>

typedef struct tracker { size_t attempts, fail_at, live, keys, values; } tracker;
static void *allocate(size_t n, void *ctx) {
    tracker *t = (tracker *)ctx;
    void *p;
    if (++t->attempts == t->fail_at) return NULL;
    p = malloc(n);
    if (p) ++t->live;
    return p;
}
static void release(void *p, void *ctx) {
    tracker *t = (tracker *)ctx;
    if (p) { assert(t->live); --t->live; free(p); }
}
static void destroy_key(void *p, void *ctx) {
    SDS_ALLOCATOR_T a = {allocate, release, ctx};
    ++((tracker *)ctx)->keys;
    SDS_FREE((SDS_T)p, &a);
}
static void destroy_value(void *p, void *ctx) {
    ++((tracker *)ctx)->values;
    release(p, ctx);
}
static void *copy_key(const void *p, void *ctx) {
    SDS_ALLOCATOR_T a = {allocate, release, ctx};
    return SDS_DUP((const char *)p, &a);
}
static void *copy_value(const void *p, void *ctx) {
    int *v = (int *)allocate(sizeof(*v), ctx);
    if (v) *v = *(const int *)p;
    return v;
}
typedef struct visit_context { DICT_T *d; SDS_T key; int *value; tracker *t; } visit_context;
static int busy_visit(const void *key, void *value, void *ctx) {
    visit_context *v = (visit_context *)ctx;
    size_t attempts = v->t->attempts;
    (void)key; (void)value;
    assert(Dict_put(v->d, v->key, v->value) == DICT_BUSY);
    assert(v->t->attempts == attempts);
    return 0;
}
int main(void) {
    tracker t = {0, 0, 0, 0, 0};
    SDS_ALLOCATOR_T a = {allocate, release, &t};
    DICT_CONFIG_T c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    DICT_CONFIG_T bad;
    DICT_T d;
    SDS_T key = SDS_NEW("sensor", &a);
    int *input = (int *)allocate(sizeof(*input), &t);
    void *stored, *taken_key, *taken_value;
    size_t n;
    visit_context visit;
    assert(key && input);
    *input = 25;
    c.alloc = allocate; c.free = release; c.ctx = &t;
    assert(DICT_INIT(&d, &c) == DICT_OK);
    assert(Dict_put(&d, key, input) == DICT_INVALID);
    assert(DICT_DESTROY(&d) == DICT_OK);
    c.copy_key = copy_key; c.copy_value = copy_value;
    c.destroy_key = destroy_key; c.destroy_value = destroy_value;
    bad = c; bad.copy_value = NULL;
    assert(DICT_INIT(&d, &bad) == DICT_INVALID);
    bad = c; bad.destroy_key = NULL;
    assert(DICT_INIT(&d, &bad) == DICT_INVALID);
    assert(DICT_INIT(&d, &c) == DICT_OK);
    /* Fail key clone, value clone, node, then initial bucket allocation. */
    for (n = 1; n <= 4; ++n) {
        t.fail_at = t.attempts + n;
        assert(Dict_put(&d, key, input) == DICT_OOM);
        assert(DICT_SIZE(&d) == 0 && t.live == 2);
        assert(strcmp(key, "sensor") == 0 && *input == 25);
    }
    t.fail_at = 0;
    {
        DICT_T *table = &d;
        SDS_T *key_ptr = &key;
        int **value_ptr = &input;
        assert(Dict_put(table++, *key_ptr++, *value_ptr++) == DICT_ADDED);
        assert(table == &d + 1 && key_ptr == &key + 1 && value_ptr == &input + 1);
    } /* each macro argument is evaluated once; inputs remain caller-owned */
    assert(DICT_GET(&d, "sensor", &stored));
    assert(stored != input && *(int *)stored == 25);
    *input = 26;
    for (n = 1; n <= 2; ++n) {
        t.fail_at = t.attempts + n;
        assert(Dict_put(&d, key, input) == DICT_OOM);
        assert(DICT_GET(&d, "sensor", &stored) && *(int *)stored == 25);
        assert(t.live == 6); /* two inputs + two copies + node + buckets */
    }
    t.fail_at = 0;
    n = t.keys;
    assert(Dict_put(&d, key, input) == DICT_REPLACED);
    assert(t.keys == n + 1 && t.live == 6);
    visit.d = &d; visit.key = key; visit.value = input; visit.t = &t;
    assert(DICT_FOREACH(&d, busy_visit, &visit) == DICT_OK);
    SDS_FREE(key, &a); release(input, &t);
    assert(DICT_GET(&d, "sensor", &stored) && *(int *)stored == 26);
    n = t.values;
    assert(DICT_REMOVE(&d, "sensor") == DICT_OK);
    assert(t.values == n + 1 && t.live == 1); /* bucket array is retained */
    assert(!DICT_GET(&d, "sensor", &stored));
    key = SDS_NEW("nullable", &a);
    assert(Dict_put(&d, key, NULL) == DICT_ADDED);
    SDS_FREE(key, &a);
    assert(DICT_GET(&d, "nullable", &stored) && stored == NULL);
    assert(DICT_TAKE(&d, "nullable", &taken_key, &taken_value) == DICT_OK);
    assert(taken_value == NULL);
    destroy_key(taken_key, &t); destroy_value(taken_value, &t);
    key = SDS_NEW("final", &a);
    input = (int *)allocate(sizeof(*input), &t); assert(key && input); *input = 7;
    assert(Dict_put(&d, key, input) == DICT_ADDED);
    SDS_FREE(key, &a); release(input, &t);
    assert(DICT_DESTROY(&d) == DICT_OK && t.live == 0);
    assert(DICT_MEMORY_STATS_GET().live_bytes == 0);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    puts("Copy ownership: replacement, OOM, BUSY, remove/take/destroy cleaned.");
    return 0;
}
