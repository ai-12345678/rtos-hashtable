#ifndef FHT_TEST_STATS_LOCK_H
#define FHT_TEST_STATS_LOCK_H
void fht_test_stats_lock(void);
void fht_test_stats_unlock(void);
#define FHT_MEMORY_STATS_LOCK() fht_test_stats_lock()
#define FHT_MEMORY_STATS_UNLOCK() fht_test_stats_unlock()
#endif
