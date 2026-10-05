#ifndef SDS_H
#define SDS_H

/* C99 exact-sized strings: [int length][length bytes][NUL]. The public pointer
 * addresses the bytes, not the allocation base. Only pass strings created here
 * to len/size/free; do not pass literals or ordinary malloc strings. Length is
 * bytes, excludes the terminator, and may include embedded NULs. No capacity,
 * refcounts or thread safety. This is SDS-inspired, not Redis SDS compatible. */
#include <limits.h>
#include <stddef.h>
#include "rtos_namespace.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char *RTOS_SYMBOL(sds);
typedef struct RTOS_SYMBOL(sds_allocator) {
    void *(*alloc)(size_t bytes, void *ctx);
    void (*free)(void *ptr, void *ctx);
    void *ctx;
} RTOS_SYMBOL(sds_allocator);

/* NULL allocator selects malloc/free; otherwise both callbacks are required.
 * Use the same allocator/context for the entire lifetime of a string. */
static inline int RTOS_SYMBOL(sds_allocator_valid_impl)(const RTOS_SYMBOL(sds_allocator) *a) {
    return !a || (a->alloc && a->free);
}
static inline size_t RTOS_SYMBOL(sds_len)(const char *s) {
    int length;
    if (!s) return 0;
    memcpy(&length, s - sizeof(int), sizeof(length));
    return (size_t)length;
}
/* Requested allocation bytes including the length header and terminator;
 * excludes heap metadata/alignment overhead. NULL uses no storage. */
static inline size_t RTOS_SYMBOL(sds_alloc_size)(const char *s) {
    return s ? sizeof(int) + RTOS_SYMBOL(sds_len)(s) + 1 : 0;
}
static inline RTOS_SYMBOL(sds) RTOS_SYMBOL(sds_new_len)(const void *bytes, size_t length,
                              const RTOS_SYMBOL(sds_allocator) *a) {
    char *base;
    int stored;
    size_t total;
    if (!RTOS_SYMBOL(sds_allocator_valid_impl)(a) || (!bytes && length) ||
        length > INT_MAX || length > SIZE_MAX - sizeof(int) - 1) return NULL;
    total = sizeof(int) + length + 1;
    base = (char *)(a ? a->alloc(total, a->ctx) : malloc(total));
    if (!base) return NULL;
    stored = (int)length;
    memcpy(base, &stored, sizeof(stored));
    if (length) memcpy(base + sizeof(int), bytes, length);
    base[sizeof(int) + length] = '\0';
    return base + sizeof(int);
}
static inline RTOS_SYMBOL(sds) RTOS_SYMBOL(sds_new)(const char *text, const RTOS_SYMBOL(sds_allocator) *a) {
    return text ? RTOS_SYMBOL(sds_new_len)(text, strlen(text), a) : NULL;
}
static inline RTOS_SYMBOL(sds) RTOS_SYMBOL(sds_dup)(const char *s, const RTOS_SYMBOL(sds_allocator) *a) {
    return s ? RTOS_SYMBOL(sds_new_len)(s, RTOS_SYMBOL(sds_len)(s), a) : NULL;
}
static inline void RTOS_SYMBOL(sds_free)(RTOS_SYMBOL(sds) s, const RTOS_SYMBOL(sds_allocator) *a) {
    void *base;
    if (!s) return;
    base = s - sizeof(int);
    if (a) a->free(base, a->ctx);
    else free(base);
}
/* Allocate first, then replace. OOM/invalid input leaves *s unchanged.
 * The source may alias *s. Successful copy/append invalidates old pointers. */
static inline int RTOS_SYMBOL(sds_copy)(RTOS_SYMBOL(sds) *s, const void *bytes, size_t length,
                            const RTOS_SYMBOL(sds_allocator) *a) {
    RTOS_SYMBOL(sds) next;
    if (!s) return 0;
    next = RTOS_SYMBOL(sds_new_len)(bytes, length, a);
    if (!next) return 0;
    RTOS_SYMBOL(sds_free)(*s, a);
    *s = next;
    return 1;
}
static inline int RTOS_SYMBOL(sds_append)(RTOS_SYMBOL(sds) *s, const void *bytes, size_t length,
                              const RTOS_SYMBOL(sds_allocator) *a) {
    size_t old_length, total;
    int stored;
    char *base;
    if (!s || !RTOS_SYMBOL(sds_allocator_valid_impl)(a) || (!bytes && length)) return 0;
    old_length = RTOS_SYMBOL(sds_len)(*s);
    if (length > INT_MAX || old_length > (size_t)INT_MAX - length) return 0;
    if (!length) return 1;
    if (old_length + length > SIZE_MAX - sizeof(int) - 1) return 0;
    stored = (int)(old_length + length);
    total = sizeof(int) + (size_t)stored + 1;
    base = (char *)(a ? a->alloc(total, a->ctx) : malloc(total));
    if (!base) return 0;
    memcpy(base, &stored, sizeof(stored));
    if (old_length) memcpy(base + sizeof(int), *s, old_length);
    memcpy(base + sizeof(int) + old_length, bytes, length);
    base[sizeof(int) + (size_t)stored] = '\0';
    RTOS_SYMBOL(sds_free)(*s, a);
    *s = base + sizeof(int);
    return 1;
}
/* Binary lexicographic comparison, including bytes after embedded NULs.
 * NULL compares as an empty string. */
static inline int RTOS_SYMBOL(sds_compare)(const char *a, const char *b) {
    size_t na = RTOS_SYMBOL(sds_len)(a), nb = RTOS_SYMBOL(sds_len)(b), n = na < nb ? na : nb;
    int result = n ? memcmp(a, b, n) : 0;
    return result ? result : (na > nb) - (na < nb);
}

/* Public operation macros: each argument is evaluated exactly once. */
#define SDS_NEW(text, allocator) \
    (RTOS_SYMBOL(sds_new)((text), (allocator)))
#define SDS_NEW_LEN(bytes, length, allocator) \
    (RTOS_SYMBOL(sds_new_len)((bytes), (length), (allocator)))
#define SDS_LEN(string) (RTOS_SYMBOL(sds_len)((string)))
#define SDS_ALLOC_SIZE(string) (RTOS_SYMBOL(sds_alloc_size)((string)))
#define SDS_DUP(string, allocator) \
    (RTOS_SYMBOL(sds_dup)((string), (allocator)))
#define SDS_FREE(string, allocator) \
    (RTOS_SYMBOL(sds_free)((string), (allocator)))
#define SDS_COPY(string_ptr, bytes, length, allocator) \
    (RTOS_SYMBOL(sds_copy)((string_ptr), (bytes), (length), (allocator)))
#define SDS_APPEND(string_ptr, bytes, length, allocator) \
    (RTOS_SYMBOL(sds_append)((string_ptr), (bytes), (length), (allocator)))
#define SDS_COMPARE(a, b) (RTOS_SYMBOL(sds_compare)((a), (b)))

#ifdef __cplusplus
}
#endif
#endif /* SDS_H */
