CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic -Werror
SOURCES = tests/test_fht.c tests/other_tu.c

.PHONY: test sanitize clean

test: build/fht_test_release build/fht_test_stats build/fht_memory_off build/fht_memory_on build/fht_memory_thread
	./build/fht_test_release
	./build/fht_test_stats
	./build/fht_memory_off
	./build/fht_memory_on
	./build/fht_memory_thread

build/fht_test_release: $(SOURCES) include/fht.h include/dict.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG $(SOURCES) -o $@

build/fht_test_stats: $(SOURCES) include/fht.h include/dict.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 $(SOURCES) -o $@

build/fht_memory_off: tests/test_memory_stats.c tests/other_tu.c include/fht.h include/dict.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG tests/test_memory_stats.c tests/other_tu.c -o $@

build/fht_memory_on: tests/test_memory_stats.c tests/other_tu.c include/fht.h include/dict.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 tests/test_memory_stats.c tests/other_tu.c -o $@

build/fht_memory_thread: tests/test_memory_stats_thread.c tests/other_tu.c tests/stats_lock.h include/fht.h include/dict.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -include tests/stats_lock.h -pthread tests/test_memory_stats_thread.c tests/other_tu.c -o $@

sanitize:
	mkdir -p build
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/fht_test_sanitize
	./build/fht_test_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/fht_test_stats_sanitize
	./build/fht_test_stats_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_memory_stats.c tests/other_tu.c -o build/fht_memory_sanitize
	./build/fht_memory_sanitize

clean:
	rm -rf build
