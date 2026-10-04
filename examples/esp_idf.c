/* Pure C, table operations have no built-in locks. Call from a single task (or serialize externally).
 * Copy into an ESP-IDF component; add ../include to INCLUDE_DIRS.
 * Table access still needs external serialization; the stats mux only protects
 * shared accounting. Include esp_idf_dict_config.h before dict.h in all TUs. */
#include "esp_idf_dict_config.h"
#define DICT_MEMORY_STATS_IMPLEMENTATION
#include "dict.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#if DICT_ENABLE_MEMORY_STATS
portMUX_TYPE dict_stats_mux = portMUX_INITIALIZER_UNLOCKED;
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
    dict sensors;
    dict_config config = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    char key[] = "temperature";
    int temperature = 25;
    void *value = NULL;
    dict_status status;
    config.alloc = idf_malloc;
    config.free = idf_free;
    /* Borrowed key/value; they must outlive their entries. */
    status = DICT_INIT(&sensors, &config);
    if (status != DICT_OK) return;
    /* Preallocate buckets. Nodes still use the custom allocator per insert. */
    status = DICT_RESERVE(&sensors, 16);
    if (status != DICT_OK) goto done;
    status = DICT_PUT(&sensors, key, &temperature);
    if (status != DICT_ADDED) goto done;
    if (DICT_GET(&sensors, "temperature", &value))
        ESP_LOGI("dict", "temperature=%d", *(int *)value);
done:
    if (status < 0) ESP_LOGE("dict", "Operation failed: %d", (int)status);
    status = DICT_DESTROY(&sensors);
    if (status == DICT_OK) {
        dict_memory_stats stats = DICT_MEMORY_STATS_GET();
        ESP_LOGI("dict", "global live=%u bytes/%u blocks, peak=%u bytes",
                 (unsigned)stats.live_bytes, (unsigned)stats.live_blocks,
                 (unsigned)stats.peak_bytes);
        /* Zero only when ALL tables have been cleared/destroyed. */
    }
}
