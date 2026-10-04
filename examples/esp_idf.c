/* Pure C, no locks. Call from a single task (or serialize externally).
 * Copy into an ESP-IDF component; add ../include to INCLUDE_DIRS. */
#include "fht.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static void *idf_malloc(size_t bytes, void *ctx) {
    (void)ctx;
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void idf_free(void *ptr, void *ctx) {
    (void)ctx;
    heap_caps_free(ptr);
}
void app_main(void) {
    fht sensors;
    fht_config config = FHT_CONFIG_DEFAULT(FHT_HASH_STRING, FHT_EQUAL_STRING);
    char key[] = "temperature";
    int temperature = 25;
    void *value = NULL;
    fht_status status;
    config.alloc = idf_malloc;
    config.free = idf_free;
    /* Borrowed key/value; they must outlive their entries. */
    status = FHT_INIT(&sensors, &config);
    if (status != FHT_OK) return;
    /* Preallocate buckets. Nodes still use the custom allocator per insert. */
    status = FHT_RESERVE(&sensors, 16);
    if (status != FHT_OK) goto done;
    status = FHT_PUT(&sensors, key, &temperature);
    if (status != FHT_ADDED) goto done;
    if (FHT_GET(&sensors, "temperature", &value))
        ESP_LOGI("fht", "temperature=%d", *(int *)value);
done:
    if (status < 0) ESP_LOGE("fht", "Operation failed: %d", (int)status);
    (void)FHT_DESTROY(&sensors);
}
