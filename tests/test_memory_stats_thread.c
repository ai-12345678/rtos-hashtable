/* Internal engine tests exercise borrowed/owned insertion directly.
 * Public callers use Dict_put (copy-only); see test_dict_copy.c. */
#include "dict.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
void dict_test_stats_lock(void) { assert(pthread_mutex_lock(&stats_mutex) == 0); }
void dict_test_stats_unlock(void) { assert(pthread_mutex_unlock(&stats_mutex) == 0); }
/* These queries would deadlock if callbacks ran under the counter lock. */
static void *thread_alloc(size_t n, void *ctx) {
    (void)ctx; (void)DICT_MEMORY_STATS_GET(); return malloc(n);
}
static void thread_free(void *p, void *ctx) {
    (void)ctx; (void)DICT_MEMORY_STATS_GET(); free(p);
}
static void *worker(void *unused) {
    size_t iteration;
    (void)unused;
    for (iteration = 0; iteration < 1000; ++iteration) {
        dict h;
        dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
        char keys[5][2] = {"a", "b", "c", "d", "e"};
        size_t i;
        c.alloc = thread_alloc; c.free = thread_free;
        assert(DICT_INIT(&h, &c) == DICT_OK);
        for (i = 0; i < 5; ++i) assert(RTOS_SYMBOL(dict_put_owned_impl)(&h, keys[i], NULL) == DICT_ADDED);
        while (DICT_IS_REHASHING(&h)) (void)DICT_REHASH_STEP(&h, 1);
        assert(DICT_RESERVE(&h, 32) == DICT_OK);
        while (DICT_IS_REHASHING(&h)) (void)DICT_REHASH_STEP(&h, 1);
        assert(DICT_REMOVE(&h, keys[0]) == DICT_OK);
        assert(DICT_DESTROY(&h) == DICT_OK);
    }
    return NULL;
}
int main(void) {
    pthread_t threads[4];
    size_t i;
    dict_memory_stats s;
    assert(DICT_MEMORY_STATS_RESET() == DICT_OK);
    for (i = 0; i < 4; ++i) assert(pthread_create(&threads[i], NULL, worker, NULL) == 0);
    for (i = 0; i < 4; ++i) assert(pthread_join(threads[i], NULL) == 0);
    s = DICT_MEMORY_STATS_GET();
    assert(s.live_bytes == 0 && s.live_blocks == 0 && s.peak_bytes != 0);
    puts("Concurrent global accounting tests passed (4 independent tables).");
    return 0;
}
