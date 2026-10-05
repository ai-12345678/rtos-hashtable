#include "dstr.h"
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
extern dstr dict_other_string(void);
int main(void) {
    tracker t = {0};
    dstr_allocator a = {tracked_alloc, tracked_free, &t};
    dstr_allocator invalid = {tracked_alloc, NULL, &t};
    char binary[] = {'a', '\0', 'b'};
    dstr s, copy, empty, old, other;
    size_t before;
    assert(dstr_len(NULL) == 0 && dstr_alloc_size(NULL) == 0);
    dstr_free(NULL, &a);
    assert(!dstr_new(NULL, &a));
    assert(!dstr_new("bad", &invalid));
    assert(!dstr_new_len(NULL, 1, &a));
    before = t.attempts;
    assert(!dstr_new_len("x", SIZE_MAX, &a));
    assert(!dstr_new_len("x", (size_t)INT_MAX + 1, &a));
    assert(t.attempts == before);
    empty = dstr_new_len(NULL, 0, &a);
    assert(empty && empty[0] == '\0' && dstr_len(empty) == 0);
    assert(dstr_alloc_size(empty) == sizeof(int) + 1);
    s = dstr_new_len(binary, sizeof(binary), &a);
    assert(s && dstr_len(s) == 3 && s[3] == '\0');
    assert(memcmp(s, binary, sizeof(binary)) == 0);
    copy = dstr_dup(s, &a);
    assert(copy && copy != s && dstr_compare(s, copy) == 0);
    assert(dstr_compare(empty, s) < 0 && dstr_compare(NULL, empty) == 0);
    assert(dstr_copy(&copy, "a", 1, &a));
    assert(dstr_compare(s, copy) > 0);
    assert(dstr_append(&copy, copy, dstr_len(copy), &a));
    assert(strcmp(copy, "aa") == 0 && dstr_len(copy) == 2);
    assert(dstr_copy(&copy, copy + 1, 1, &a));
    assert(strcmp(copy, "a") == 0);
    old = copy; before = t.bytes;
    t.fail_at = t.attempts + 1;
    assert(!dstr_append(&copy, "xyz", 3, &a));
    assert(copy == old && t.bytes == before && strcmp(copy, "a") == 0);
    t.fail_at = t.attempts + 1;
    assert(!dstr_copy(&copy, "xyz", 3, &a));
    assert(copy == old && t.bytes == before);
    assert(!dstr_append(&copy, "x", SIZE_MAX, &a) && copy == old);
    assert(!dstr_append(&copy, NULL, 1, &a) && copy == old);
    assert(dstr_append(&copy, NULL, 0, &a) && copy == old);
    t.fail_at = t.attempts + 1;
    assert(!dstr_new("OOM", &a) && t.bytes == before);
    t.fail_at = 0;
    assert(t.bytes == dstr_alloc_size(s) + dstr_alloc_size(copy) + dstr_alloc_size(empty));
    dstr_free(s, &a); dstr_free(copy, &a); dstr_free(empty, &a);
    assert(t.bytes == 0 && t.blocks == 0);
    other = dict_other_string();
    assert(other && strcmp(other, "other-tu") == 0 && dstr_len(other) == 8);
    dstr_free(other, NULL);
    s = NULL;
    assert(dstr_append(&s, "new", 3, NULL));
    assert(strcmp(s, "new") == 0);
    dstr_free(s, NULL);
    puts("dstr tests passed: binary data, aliasing, OOM, overflow, accounting, multiple TUs.");
    return 0;
}
