#define DICT_MEMORY_STATS_IMPLEMENTATION
#include "dict.h"
int dict_other_translation_unit(void) {
    dict h;
    dict_config c = dict_config_default(dict_hash_string, dict_equal_string);
    if (dict_init(&h, &c) != DICT_OK) return 1;
    return dict_destroy(&h) == DICT_OK ? 0 : 1;
}

/* Keep a table alive across calls to verify process-wide accounting. */
static dict other_table;
static char other_key[] = "other-tu";
static int other_value;
int dict_other_table_create(void) {
    dict_config c = DICT_CONFIG_DEFAULT(DICT_HASH_STRING, DICT_EQUAL_STRING);
    if (DICT_INIT(&other_table, &c) != DICT_OK) return 1;
    return DICT_PUT(&other_table, other_key, &other_value) == DICT_ADDED ? 0 : 1;
}
int dict_other_table_destroy(void) {
    return DICT_DESTROY(&other_table) == DICT_OK ? 0 : 1;
}
dict_memory_stats dict_other_memory_stats(void) {
    return DICT_MEMORY_STATS_GET();
}
