#ifndef DICT_ESP_IDF_STATS_CONFIG_H
#define DICT_ESP_IDF_STATS_CONFIG_H
/* Include this common configuration BEFORE dict.h in every component source
 * using the table. Set DICT_ENABLE_MEMORY_STATS=0 project-wide to disable it. */
#ifndef DICT_ENABLE_MEMORY_STATS
#define DICT_ENABLE_MEMORY_STATS 1
#endif
#if DICT_ENABLE_MEMORY_STATS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern portMUX_TYPE dict_stats_mux;
/* Only the short counter update/snapshot is inside this critical section.
 * Table operations, allocation and free remain outside it. Task-only API. */
#define DICT_MEMORY_STATS_LOCK() portENTER_CRITICAL(&dict_stats_mux)
#define DICT_MEMORY_STATS_UNLOCK() portEXIT_CRITICAL(&dict_stats_mux)
#endif
#endif
