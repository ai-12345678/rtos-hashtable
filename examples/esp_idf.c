/* Pure C, table operations have no built-in locks. Call from a single task (or serialize externally).
 * Copy into an ESP-IDF component; add ../include to INCLUDE_DIRS.
 * Table access still needs external serialization; the stats mux only protects
 * shared accounting. Include esp_idf_dict_config.h before dict.h in all TUs. */
#include "esp_idf_dict_config.h"
#define DICT_MEMORY_STATS_IMPLEMENTATION
#include "dict.h"
#include "sds.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#if DICT_ENABLE_MEMORY_STATS
portMUX_TYPE RTOS_SYMBOL(dict_stats_mux) = portMUX_INITIALIZER_UNLOCKED;
#endif

static void *idf_malloc(size_t bytes, void *ctx) {
    (void)ctx;
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void idf_free(void *ptr, void *ctx) {
    (void)ctx;
    heap_caps_free(ptr);
}
void app_main(void) {
    DICT_T sensors;
    DICT_CONFIG_T config = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    SDS_ALLOCATOR_T strings = {idf_malloc, idf_free, NULL};
    SDS_T key = NULL;
    SDS_T temperature = NULL;
    void *value = NULL;
    DICT_STATUS_T status;
    config.alloc = idf_malloc;
    config.free = idf_free;
    config.ctx = &strings;
    status = DICT_INIT(&sensors, &config);
    if (status != DICT_OK) return;
    /* Preallocate buckets. Nodes still use the custom allocator per insert. */
    status = DICT_RESERVE(&sensors, 16);
    if (status != DICT_OK) goto done;
    /* User objects use the same custom allocator callbacks as internal storage.
     * Source objects remain caller-owned even after copying succeeds. */
    key = SDS_NEW("temperature", &strings);
    temperature = SDS_NEW("25", &strings);
    if (!key || !temperature) {
        status = DICT_OOM;
        goto done;
    }
    status = DICT_PUT_SDS(&sensors, key, temperature);
    if (status != DICT_ADDED && status != DICT_REPLACED) goto done;
    /* Dict owns independent heap copies; release both input objects now. */
    SDS_FREE(key, &strings);
    SDS_FREE(temperature, &strings);
    key = NULL;
    temperature = NULL;
    if (DICT_GET(&sensors, "temperature", &value))
        ESP_LOGI("dict", "temperature=%s", (const char *)value);
    status = DICT_REMOVE(&sensors, "temperature"); /* frees both stored copies */
done:
    /* NULL after successful input cleanup; otherwise release caller-owned objects,
     * including partial allocation or insertion failures. */
    SDS_FREE(key, &strings);
    SDS_FREE(temperature, &strings);
    if (status < 0) ESP_LOGE("dict", "Operation failed: %d", (int)status);
    status = DICT_DESTROY(&sensors);
    if (status == DICT_OK) {
        DICT_MEMORY_STATS_T stats = DICT_MEMORY_STATS_GET();
        ESP_LOGI("dict", "global live=%u bytes/%u blocks, peak=%u bytes",
                 (unsigned)stats.live_bytes, (unsigned)stats.live_blocks,
                 (unsigned)stats.peak_bytes);
        /* Zero only when ALL tables have been cleared/destroyed. */
    }
}
