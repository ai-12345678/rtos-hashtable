#include "dict.h"
#include <assert.h>
#include <stdio.h>

typedef struct tracker { size_t live, attempts, fail_at; } tracker;
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
static char *plain(const char *s, tracker *t) {
    char *p = (char *)allocate(strlen(s) + 1, t);
    assert(p); strcpy(p, s); return p;
}
static int visit_busy(const void *key, void *value, void *ctx) {
    DICT_T *d = (DICT_T *)ctx;
    tracker *t = (tracker *)d->config.ctx;
    size_t attempts = t->attempts;
    assert(DICT_PUT(d, "blocked", "value") == DICT_BUSY);
    assert(DICT_PUT_SDS(d, (const char *)key, (const char *)value) == DICT_BUSY);
    assert(t->attempts == attempts);
    return 0;
}
static uint32_t hash_sds(const void *key, void *ctx) {
    const unsigned char *p = (const unsigned char *)key;
    size_t i, length = SDS_LEN((const char *)key);
    uint32_t hash = UINT32_C(2166136261);
    (void)ctx;
    for (i = 0; i < length; ++i) { hash ^= p[i]; hash *= UINT32_C(16777619); }
    return hash;
}
static uint32_t collide(const void *key, void *ctx) {
    (void)key; (void)ctx; return 0;
}
static int equal_sds(const void *a, const void *b, void *ctx) {
    (void)ctx; return SDS_COMPARE((const char *)a, (const char *)b) == 0;
}
int main(void) {
    tracker t = {0, 0, 0};
    SDS_ALLOCATOR_T a = {allocate, release, &t};
    DICT_CONFIG_T c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    DICT_T d;
    char *key = plain("sensor", &t), *value = plain("25", &t);
    SDS_T sk, sv;
    void *stored, *taken_key, *taken_value;
    size_t i;
    c.alloc = allocate; c.free = release; c.ctx = &t;
    c.destroy_value = release;
    assert(DICT_INIT(&d, &c) == DICT_OK);
    assert(DICT_PUT(&d, key, value) == DICT_INVALID && t.live == 2);
    assert(DICT_DESTROY(&d) == DICT_OK);
    c.destroy_value = NULL;
    assert(DICT_INIT(&d, &c) == DICT_OK);
    for (i = 1; i <= 4; ++i) {
        t.fail_at = t.attempts + i;
        assert(DICT_PUT(&d, key, value) == DICT_OOM);
        assert(t.live == 2 && DICT_SIZE(&d) == 0);
    }
    t.fail_at = 0;
    {
        DICT_T *dp = &d;
        char **kp = &key, **vp = &value;
        assert(DICT_PUT(dp++, *kp++, *vp++) == DICT_ADDED);
        assert(dp == &d + 1 && kp == &key + 1 && vp == &value + 1);
    }
    assert(DICT_GET(&d, "sensor", &stored) && stored != value);
    assert(SDS_LEN((SDS_T)stored) == 2 && strcmp((char *)stored, "25") == 0);
    strcpy(value, "26");
    release(key, &t); release(value, &t);
    assert(DICT_GET(&d, "sensor", &stored) && strcmp((char *)stored, "25") == 0);
    sk = SDS_NEW("sensor", &a); sv = SDS_NEW("27", &a); assert(sk && sv);
    for (i = 1; i <= 2; ++i) {
        t.fail_at = t.attempts + i;
        assert(DICT_PUT_SDS(&d, sk, sv) == DICT_OOM);
        assert(t.live == 6);
        assert(DICT_GET(&d, "sensor", &stored) && strcmp((char *)stored, "25") == 0);
    }
    t.fail_at = 0;
    {
        DICT_T *dp = &d;
        SDS_T *kp = &sk, *vp = &sv;
        assert(DICT_PUT_SDS(dp++, *kp++, *vp++) == DICT_REPLACED);
        assert(dp == &d + 1 && kp == &sk + 1 && vp == &sv + 1);
    }
    SDS_FREE(sk, &a); SDS_FREE(sv, &a);
    assert(t.live == 4);
    assert(DICT_GET(&d, "sensor", &stored) && strcmp((char *)stored, "27") == 0);
    assert(DICT_FOREACH(&d, visit_busy, &d) == DICT_OK);
    assert(DICT_REMOVE(&d, "sensor") == DICT_OK && t.live == 1);
    key = plain("nullable", &t);
    assert(DICT_PUT(&d, key, NULL) == DICT_ADDED); release(key, &t);
    assert(DICT_GET(&d, "nullable", &stored) && stored == NULL);
    assert(DICT_TAKE(&d, "nullable", &taken_key, &taken_value) == DICT_OK);
    assert(taken_value == NULL); SDS_FREE((SDS_T)taken_key, &a);
    sk = SDS_NEW("", &a); sv = SDS_NEW("", &a); assert(sk && sv);
    assert(DICT_PUT_SDS(&d, sk, sv) == DICT_ADDED);
    SDS_FREE(sk, &a); SDS_FREE(sv, &a);
    assert(DICT_CLEAR(&d) == DICT_OK && t.live == 0);
    key = plain("last", &t); value = plain("v", &t);
    assert(DICT_PUT(&d, key, value) == DICT_ADDED);
    release(key, &t); release(value, &t);
    assert(DICT_DESTROY(&d) == DICT_OK && t.live == 0);
    /* SDS preserves bytes after NUL; binary keys require matching callbacks. */
    c.hash = hash_sds; c.equal = equal_sds;
    assert(DICT_INIT(&d, &c) == DICT_OK);
    sk = SDS_NEW_LEN("a\0x", 3, &a); sv = SDS_NEW_LEN("v\0z", 3, &a);
    assert(sk && sv && DICT_PUT_SDS(&d, sk, sv) == DICT_ADDED);
    assert(DICT_GET(&d, sk, &stored) && SDS_LEN((SDS_T)stored) == 3);
    assert(memcmp(stored, "v\0z", 3) == 0);
    SDS_FREE(sk, &a); SDS_FREE(sv, &a);
    assert(DICT_DESTROY(&d) == DICT_OK && t.live == 0);
    /* Force collisions and many dual-table rehashes with public string writes. */
    c.hash = collide; c.equal = DICT_EQUAL_STRING;
    assert(DICT_INIT(&d, &c) == DICT_OK);
    for (i = 0; i < 200; ++i) {
        char text[32];
        (void)snprintf(text, sizeof(text), "key-%zu", i);
        key = plain(text, &t); value = plain("payload", &t);
        assert(DICT_PUT(&d, key, value) == DICT_ADDED);
        release(key, &t); release(value, &t);
    }
    for (i = 0; i < 200; ++i) {
        char text[32];
        (void)snprintf(text, sizeof(text), "key-%zu", i);
        assert(DICT_GET(&d, text, &stored) && strcmp((const char *)stored, "payload") == 0);
    }
    assert(DICT_DESTROY(&d) == DICT_OK && t.live == 0);
    assert(DICT_MEMORY_STATS_GET().live_bytes == 0);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    puts("char*/SDS key-value copies: OOM, mixed writes, binary, BUSY and cleanup passed.");
    return 0;
}
