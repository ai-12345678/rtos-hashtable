#include "sds.h"
#include <assert.h>
#include <stdio.h>

typedef struct tracker {
    void *ptrs[16];
    size_t sizes[16];
    size_t bytes, blocks, attempts, fail_at;
} tracker;
static void *tracked_alloc(size_t n, void *ctx) {
    tracker *t = (tracker *)ctx;
    size_t i;
    void *p;
    if (++t->attempts == t->fail_at) return NULL;
    p = malloc(n); assert(p);
    for (i = 0; i < 16 && t->ptrs[i]; ++i) {}
    assert(i < 16);
    t->ptrs[i] = p; t->sizes[i] = n;
    t->bytes += n; ++t->blocks;
    return p;
}
static void tracked_free(void *p, void *ctx) {
    tracker *t = (tracker *)ctx;
    size_t i;
    if (!p) return;
    for (i = 0; i < 16 && t->ptrs[i] != p; ++i) {}
    assert(i < 16 && t->blocks);
    t->bytes -= t->sizes[i]; --t->blocks;
    t->ptrs[i] = NULL;
    free(p);
}
extern sds dict_other_string(void);
int main(void) {
    tracker t = {0};
    sds_allocator a = {tracked_alloc, tracked_free, &t};
    sds_allocator invalid = {tracked_alloc, NULL, &t};
    char binary[] = {'a', '\0', 'b'};
    sds s, copy, empty, old, other;
    size_t before;
    assert(SDS_LEN(NULL) == 0 && SDS_ALLOC_SIZE(NULL) == 0);
    SDS_FREE(NULL, &a);
    assert(!SDS_NEW(NULL, &a));
    assert(!SDS_NEW("bad", &invalid));
    assert(!SDS_NEW_LEN(NULL, 1, &a));
    before = t.attempts;
    assert(!SDS_NEW_LEN("x", SIZE_MAX, &a));
    assert(!SDS_NEW_LEN("x", (size_t)INT_MAX + 1, &a));
    assert(t.attempts == before);
    empty = SDS_NEW_LEN(NULL, 0, &a);
    assert(empty && empty[0] == '\0' && SDS_LEN(empty) == 0);
    assert(SDS_ALLOC_SIZE(empty) == sizeof(int) + 1);
    s = SDS_NEW_LEN(binary, sizeof(binary), &a);
    assert(s && SDS_LEN(s) == 3 && s[3] == '\0');
    assert(memcmp(s, binary, sizeof(binary)) == 0);
    copy = SDS_DUP(s, &a);
    assert(copy && copy != s && SDS_COMPARE(s, copy) == 0);
    assert(SDS_COMPARE(empty, s) < 0 && SDS_COMPARE(NULL, empty) == 0);
    assert(SDS_COPY(&copy, "a", 1, &a));
    assert(SDS_COMPARE(s, copy) > 0);
    assert(SDS_APPEND(&copy, copy, SDS_LEN(copy), &a));
    assert(strcmp(copy, "aa") == 0 && SDS_LEN(copy) == 2);
    assert(SDS_COPY(&copy, copy + 1, 1, &a));
    assert(strcmp(copy, "a") == 0);
    old = copy; before = t.bytes;
    t.fail_at = t.attempts + 1;
    assert(!SDS_APPEND(&copy, "xyz", 3, &a));
    assert(copy == old && t.bytes == before && strcmp(copy, "a") == 0);
    t.fail_at = t.attempts + 1;
    assert(!SDS_COPY(&copy, "xyz", 3, &a));
    assert(copy == old && t.bytes == before);
    assert(!SDS_APPEND(&copy, "x", SIZE_MAX, &a) && copy == old);
    assert(!SDS_APPEND(&copy, NULL, 1, &a) && copy == old);
    assert(SDS_APPEND(&copy, NULL, 0, &a) && copy == old);
    t.fail_at = t.attempts + 1;
    assert(!SDS_NEW("OOM", &a) && t.bytes == before);
    t.fail_at = 0;
    assert(t.bytes == SDS_ALLOC_SIZE(s) + SDS_ALLOC_SIZE(copy) + SDS_ALLOC_SIZE(empty));
    SDS_FREE(s, &a); SDS_FREE(copy, &a); SDS_FREE(empty, &a);
    assert(t.bytes == 0 && t.blocks == 0);
    /* Public macro arguments, including allocator pointers, run once. */
    {
        const char *inputs[2] = {"abc", "unused"}, **input = inputs;
        sds_allocator allocators[2] = {a, a}, *allocator = allocators;
        sds strings[2] = {NULL, NULL}, *string = strings;
        size_t length = 3;
        strings[0] = SDS_NEW_LEN(*input++, length++, allocator++);
        assert(strings[0] && input == inputs + 1 && length == 4 && allocator == allocators + 1);
        assert(SDS_LEN(*string++) == 3 && string == strings + 1);
        string = strings;
        strings[1] = SDS_DUP(*string++, &a);
        assert(strings[1] && string == strings + 1);
        string = strings;
        SDS_FREE(*string++, &a);
        SDS_FREE(*string++, &a);
        assert(string == strings + 2 && t.bytes == 0 && t.blocks == 0);
    }
    other = dict_other_string();
    assert(other && strcmp(other, "other-tu") == 0 && SDS_LEN(other) == 8);
    SDS_FREE(other, NULL);
    s = NULL;
    assert(SDS_APPEND(&s, "new", 3, NULL));
    assert(strcmp(s, "new") == 0);
    SDS_FREE(s, NULL);
    puts("sds tests passed: binary data, aliasing, OOM, overflow, accounting, multiple TUs.");
    return 0;
}
