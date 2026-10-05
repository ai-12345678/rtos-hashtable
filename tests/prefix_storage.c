#define DICT_MEMORY_STATS_IMPLEMENTATION
#include "dict.h"
#include "sds.h"
#include <assert.h>

/* This file is compiled twice and linked into one program. */
int RTOS_SYMBOL(prefix_roundtrip)(void) {
    RTOS_SYMBOL(dict) h;
    RTOS_SYMBOL(dict_config) config = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    RTOS_SYMBOL(sds) key = SDS_NEW("prefixed", NULL);
    size_t (*length)(const char *) = RTOS_SYMBOL(sds_len);
    int value = 17;
    void *out;
    assert(key && length(key) == 8);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    assert(DICT_INIT(&h, &config) == DICT_OK);
    assert(DICT_PUT(&h, key, &value) == DICT_ADDED);
    assert(DICT_GET(&h, "prefixed", &out) && out == &value);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 2);
    assert(DICT_DESTROY(&h) == DICT_OK);
    assert(DICT_MEMORY_STATS_GET().live_bytes == 0);
    assert(DICT_MEMORY_STATS_GET().live_blocks == 0);
    SDS_FREE(key, NULL);
    return 0;
}
