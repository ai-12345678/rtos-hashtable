CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic -Werror
SOURCES = tests/test_dict.c tests/other_tu.c

.PHONY: test sanitize clean

test: build/dict_test_release build/dict_test_stats build/dict_memory_off build/dict_memory_on build/dict_memory_thread build/dstr_test
	./build/dict_test_release
	./build/dict_test_stats
	./build/dict_memory_off
	./build/dict_memory_on
	./build/dict_memory_thread
	./build/dstr_test

build/dstr_test: tests/test_dstr.c tests/other_tu.c include/dstr.h include/dict.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG tests/test_dstr.c tests/other_tu.c -o $@

build/dict_test_release: $(SOURCES) include/dict.h include/dstr.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG $(SOURCES) -o $@

build/dict_test_stats: $(SOURCES) include/dict.h include/dstr.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 $(SOURCES) -o $@

build/dict_memory_off: tests/test_memory_stats.c tests/other_tu.c include/dict.h include/dstr.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG tests/test_memory_stats.c tests/other_tu.c -o $@

build/dict_memory_on: tests/test_memory_stats.c tests/other_tu.c include/dict.h include/dstr.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 tests/test_memory_stats.c tests/other_tu.c -o $@

build/dict_memory_thread: tests/test_memory_stats_thread.c tests/other_tu.c tests/stats_lock.h include/dict.h include/dstr.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -include tests/stats_lock.h -pthread tests/test_memory_stats_thread.c tests/other_tu.c -o $@

sanitize:
	mkdir -p build
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/dict_test_sanitize
	./build/dict_test_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/dict_test_stats_sanitize
	./build/dict_test_stats_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_memory_stats.c tests/other_tu.c -o build/dict_memory_sanitize
	./build/dict_memory_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_dstr.c tests/other_tu.c -o build/dstr_sanitize
	./build/dstr_sanitize

clean:
	rm -rf build
