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
    fht_config config = fht_config_default(fht_hash_string, fht_equal_string);
    char key[] = "temperature";
    int temperature = 25;
    void *value = NULL;
    fht_status status;
    config.alloc = idf_malloc;
    config.free = idf_free;
    /* Borrowed key/value; they must outlive their entries. */
    status = fht_init(&sensors, &config);
    if (status != FHT_OK) return;
    /* Preallocate buckets. Nodes still use the custom allocator per insert. */
    status = fht_reserve(&sensors, 16);
    if (status != FHT_OK) goto done;
    status = fht_put(&sensors, key, &temperature);
    if (status != FHT_ADDED) goto done;
    if (fht_get(&sensors, "temperature", &value))
        ESP_LOGI("fht", "temperature=%d", *(int *)value);
done:
    if (status < 0) ESP_LOGE("fht", "Operation failed: %d", (int)status);
    (void)fht_destroy(&sensors);
}
