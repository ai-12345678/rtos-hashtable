/* Host demo: SDS key/value copies and TWO dictionaries share one allocator/ctx.
 * Single-threaded: protect shared accounting separately if used by many tasks.
 * Run with --test to fail every allocation point and check cleanup.
 * DICT's internal counters overlap this allocator's counters; do not add them.
 * The allocator below counts its requested malloc bytes, including its header,
 * but cannot count libc's own metadata/alignment overhead. */
#define DICT_MEMORY_STATS_IMPLEMENTATION
#include "dict.h"
#include "sds.h"
#include <assert.h>
#include <stdio.h>

typedef struct memory_tracker {
    size_t live_bytes, live_blocks, peak_bytes;
    size_t attempts, fail_at;
} memory_tracker;
static memory_tracker global_memory;

/* C99 alignment for this example's ordinary objects (dict/SDS/char). The header
 * records sizes because a free callback receives only ptr and ctx. */
typedef union allocation_header {
    size_t total_bytes;
    long double align_float;
    long long align_integer;
    void *align_pointer;
} allocation_header;

static void *shared_malloc(size_t bytes, void *ctx) {
    memory_tracker *t = (memory_tracker *)ctx;
    allocation_header *header;
    size_t total;
    if (++t->attempts == t->fail_at) return NULL;
    if (bytes > SIZE_MAX - sizeof(*header)) return NULL;
    total = sizeof(*header) + bytes;
    if (total > SIZE_MAX - t->live_bytes || t->live_blocks == SIZE_MAX) return NULL;
    header = (allocation_header *)malloc(total);
    if (!header) return NULL;
    header->total_bytes = total;
    t->live_bytes += total;
    ++t->live_blocks;
    if (t->live_bytes > t->peak_bytes) t->peak_bytes = t->live_bytes;
    return header + 1;
}
static void shared_free(void *ptr, void *ctx) {
    memory_tracker *t = (memory_tracker *)ctx;
    allocation_header *header;
    if (!ptr) return;
    header = (allocation_header *)ptr - 1;
    assert(t->live_blocks && t->live_bytes >= header->total_bytes);
    t->live_bytes -= header->total_bytes;
    --t->live_blocks;
    free(header);
}
/* Example 1: heap-allocated ordinary char* key AND value. No SDS header. */
static int put_char_example(DICT_T *d, memory_tracker *t) {
    char *key = (char *)shared_malloc(sizeof("temperature"), t);
    char *value = (char *)shared_malloc(sizeof("25"), t);
    DICT_STATUS_T status = DICT_OOM;
    if (key && value) {
        strcpy(key, "temperature");
        strcpy(value, "25");
        status = DICT_PUT(d, key, value);
    }
    /* Dict stores independent SDS copies. Always free caller inputs. */
    shared_free(key, t);
    shared_free(value, t);
    return status == DICT_ADDED || status == DICT_REPLACED;
}
/* Example 2: heap-allocated SDS key AND value, with int length headers. */
static int put_sds_example(DICT_T *d, memory_tracker *t) {
    SDS_ALLOCATOR_T allocator = {shared_malloc, shared_free, t};
    SDS_T key = SDS_NEW("humidity", &allocator);
    SDS_T value = SDS_NEW("60", &allocator);
    DICT_STATUS_T status = DICT_OOM;
    if (key && value) status = DICT_PUT_SDS(d, key, value);
    /* Never shared_free an SDS content pointer; SDS_FREE recovers its base. */
    SDS_FREE(key, &allocator);
    SDS_FREE(value, &allocator);
    return status == DICT_ADDED || status == DICT_REPLACED;
}
static int run_demo(size_t fail_at, int verbose) {
    DICT_T first, second;
    DICT_CONFIG_T config = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    int first_ready = 0, second_ready = 0, ok = 0;
    void *value;
    if (global_memory.live_bytes || global_memory.live_blocks) return 0;
    memset(&global_memory, 0, sizeof(global_memory));
    global_memory.fail_at = fail_at;
    config.alloc = shared_malloc;
    config.free = shared_free;
    config.ctx = &global_memory; /* Same ctx for input strings and internal SDS copies. */
    if (DICT_INIT(&first, &config) != DICT_OK) goto done;
    first_ready = 1;
    if (DICT_INIT(&second, &config) != DICT_OK) goto done;
    second_ready = 1;
    if (DICT_RESERVE(&first, 4) != DICT_OK || DICT_RESERVE(&second, 4) != DICT_OK) goto done;
    if (!put_char_example(&first, &global_memory) ||
        !put_sds_example(&second, &global_memory)) goto done;
    if (!DICT_GET(&first, "temperature", &value) || strcmp((const char *)value, "25") != 0) goto done;
    if (!DICT_GET(&second, "humidity", &value) || strcmp((const char *)value, "60") != 0) goto done;
    if (verbose)
        printf("Two dicts + SDS keys + values: %zu bytes / %zu blocks\n",
               global_memory.live_bytes, global_memory.live_blocks);
    /* REMOVE frees the stored key/value copies and node; buckets remain. */
    if (DICT_REMOVE(&first, "temperature") != DICT_OK) goto done;
    assert(!DICT_GET(&first, "temperature", &value));
    if (DICT_DESTROY(&first) != DICT_OK) goto done;
    first_ready = 0;
    assert(global_memory.live_bytes && global_memory.live_blocks); /* second lives */
    if (verbose)
        printf("After first dict destroyed: %zu bytes / %zu blocks\n",
               global_memory.live_bytes, global_memory.live_blocks);
    ok = 1;
done:
    if (first_ready && DICT_DESTROY(&first) != DICT_OK) ok = 0;
    if (second_ready && DICT_DESTROY(&second) != DICT_OK) ok = 0;
    if (verbose)
        printf("After all cleanup: %zu bytes / %zu blocks; peak=%zu bytes\n",
               global_memory.live_bytes, global_memory.live_blocks, global_memory.peak_bytes);
    assert(global_memory.live_bytes == 0 && global_memory.live_blocks == 0);
    assert(DICT_MEMORY_STATS_GET().live_bytes == 0);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    return ok;
}
int main(int argc, char **argv) {
    size_t attempts, failure;
    int test = argc == 2 && strcmp(argv[1], "--test") == 0;
    if (!run_demo(0, !test)) return 1;
    attempts = global_memory.attempts;
    if (test) {
        for (failure = 1; failure <= attempts; ++failure)
            if (run_demo(failure, 0)) return 1; /* injected OOM must be observed */
        puts("Shared allocator: all allocation failures cleaned; total bytes/blocks zero.");
    }
    return 0;
}
