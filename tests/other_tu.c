#define FHT_MEMORY_STATS_IMPLEMENTATION
#include "fht.h"
int fht_other_translation_unit(void) {
    fht h;
    fht_config c = fht_config_default(fht_hash_string, fht_equal_string);
    if (fht_init(&h, &c) != FHT_OK) return 1;
    return fht_destroy(&h) == FHT_OK ? 0 : 1;
}

/* Keep a table alive across calls to verify process-wide accounting. */
static fht other_table;
static char other_key[] = "other-tu";
static int other_value;
int fht_other_table_create(void) {
    fht_config c = FHT_CONFIG_DEFAULT(FHT_HASH_STRING, FHT_EQUAL_STRING);
    if (FHT_INIT(&other_table, &c) != FHT_OK) return 1;
    return FHT_PUT(&other_table, other_key, &other_value) == FHT_ADDED ? 0 : 1;
}
int fht_other_table_destroy(void) {
    return FHT_DESTROY(&other_table) == FHT_OK ? 0 : 1;
}
fht_memory_stats fht_other_memory_stats(void) {
    return FHT_MEMORY_STATS_GET();
}
