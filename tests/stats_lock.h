#ifndef DICT_TEST_STATS_LOCK_H
#define DICT_TEST_STATS_LOCK_H
void dict_test_stats_lock(void);
void dict_test_stats_unlock(void);
#define DICT_MEMORY_STATS_LOCK() dict_test_stats_lock()
#define DICT_MEMORY_STATS_UNLOCK() dict_test_stats_unlock()
#endif
