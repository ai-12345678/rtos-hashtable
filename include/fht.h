#ifndef FHT_H
#define FHT_H

/* Compatibility facade. New code should include dict.h and use DICT_*.
 * Both APIs operate on the same dictionary and global accounting object. */
#include "dict.h"

#ifndef FHT_ENABLE_MEMORY_STATS
#define FHT_ENABLE_MEMORY_STATS DICT_ENABLE_MEMORY_STATS
#endif

typedef dict fht;
typedef dict_hash_fn fht_hash_fn;
typedef dict_equal_fn fht_equal_fn;
typedef dict_destroy_fn fht_destroy_fn;
typedef dict_alloc_fn fht_alloc_fn;
typedef dict_free_fn fht_free_fn;
typedef dict_status fht_status;
typedef dict_config fht_config;
typedef dict_entry fht_entry;
typedef dict_table fht_table;
typedef dict_memory_stats fht_memory_stats;
typedef dict_visit_fn fht_visit_fn;

#define FHT_OK DICT_OK
#define FHT_ADDED DICT_ADDED
#define FHT_REPLACED DICT_REPLACED
#define FHT_INVALID DICT_INVALID
#define FHT_OOM DICT_OOM
#define FHT_OVERFLOW DICT_OVERFLOW
#define FHT_BUSY DICT_BUSY
#define FHT_NOT_FOUND DICT_NOT_FOUND
#define FHT_CONFIG_DEFAULT DICT_CONFIG_DEFAULT
#define FHT_INIT DICT_INIT
#define FHT_SIZE DICT_SIZE
#define FHT_IS_REHASHING DICT_IS_REHASHING
#define FHT_CAPACITY DICT_CAPACITY
#define FHT_REHASH_STEP DICT_REHASH_STEP
#define FHT_RESERVE DICT_RESERVE
#define FHT_GET DICT_GET
#define FHT_PUT DICT_PUT
#define FHT_REMOVE DICT_REMOVE
#define FHT_TAKE DICT_TAKE
#define FHT_FOREACH DICT_FOREACH
#define FHT_CLEAR DICT_CLEAR
#define FHT_DESTROY DICT_DESTROY
#define FHT_HASH_STRING DICT_HASH_STRING
#define FHT_EQUAL_STRING DICT_EQUAL_STRING
#define FHT_MEMORY_STATS_GET DICT_MEMORY_STATS_GET
#define FHT_MEMORY_STATS_RESET DICT_MEMORY_STATS_RESET

#define fht_config_default dict_config_default
#define fht_init dict_init
#define fht_size dict_size
#define fht_is_rehashing dict_is_rehashing
#define fht_capacity dict_capacity
#define fht_rehash_step dict_rehash_step
#define fht_reserve dict_reserve
#define fht_get dict_get
#define fht_put dict_put
#define fht_remove dict_remove
#define fht_take dict_take
#define fht_foreach dict_foreach
#define fht_clear dict_clear
#define fht_destroy dict_destroy
#define fht_hash_string dict_hash_string
#define fht_equal_string dict_equal_string

#endif /* FHT_H */
