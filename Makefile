CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic -Werror
SOURCES = tests/test_dict.c tests/other_tu.c

.PHONY: test sanitize clean

test: build/dict_copy_test build/dict_test_release build/dict_test_stats build/dict_memory_off build/dict_memory_on build/dict_memory_thread build/sds_test build/prefix_test build/shared_allocator build/shared_allocator_off build/shared_allocator_prefix
	./build/dict_copy_test
	./build/dict_test_release
	./build/dict_test_stats
	./build/dict_memory_off
	./build/dict_memory_on
	./build/dict_memory_thread
	./build/sds_test
	./build/prefix_test
	./build/shared_allocator --test
	./build/shared_allocator_off --test
	./build/shared_allocator_prefix --test

build/dict_copy_test: tests/test_dict_copy.c tests/other_tu.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 tests/test_dict_copy.c tests/other_tu.c -o $@

build/shared_allocator: examples/shared_allocator.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 $< -o $@

build/shared_allocator_off: examples/shared_allocator.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG $< -o $@

build/shared_allocator_prefix: examples/shared_allocator.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -DRTOS_PREFIX=app_ $< -o $@

build/prefix_default.o: tests/prefix_storage.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -c $< -o $@

build/prefix_custom.o: tests/prefix_storage.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -DRTOS_PREFIX=demo_ -c $< -o $@

build/prefix_test: tests/test_prefix.c build/prefix_default.o build/prefix_custom.o include/dict.h include/sds.h include/rtos_namespace.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -DRTOS_PREFIX=demo_ tests/test_prefix.c build/prefix_default.o build/prefix_custom.o -o $@

build/sds_test: tests/test_sds.c tests/other_tu.c include/sds.h include/dict.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG tests/test_sds.c tests/other_tu.c -o $@

build/dict_test_release: $(SOURCES) include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG $(SOURCES) -o $@

build/dict_test_stats: $(SOURCES) include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 $(SOURCES) -o $@

build/dict_memory_off: tests/test_memory_stats.c tests/other_tu.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG tests/test_memory_stats.c tests/other_tu.c -o $@

build/dict_memory_on: tests/test_memory_stats.c tests/other_tu.c include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 tests/test_memory_stats.c tests/other_tu.c -o $@

build/dict_memory_thread: tests/test_memory_stats_thread.c tests/other_tu.c tests/stats_lock.h include/dict.h include/sds.h include/rtos_namespace.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -include tests/stats_lock.h -pthread tests/test_memory_stats_thread.c tests/other_tu.c -o $@

sanitize:
	mkdir -p build
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_dict_copy.c tests/other_tu.c -o build/dict_copy_sanitize
	./build/dict_copy_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/dict_test_sanitize
	./build/dict_test_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/dict_test_stats_sanitize
	./build/dict_test_stats_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_memory_stats.c tests/other_tu.c -o build/dict_memory_sanitize
	./build/dict_memory_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_sds.c tests/other_tu.c -o build/sds_sanitize
	./build/sds_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -c tests/prefix_storage.c -o build/prefix_default_sanitize.o
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -DRTOS_PREFIX=demo_ -fsanitize=address,undefined -c tests/prefix_storage.c -o build/prefix_custom_sanitize.o
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -DRTOS_PREFIX=demo_ -fsanitize=address,undefined tests/test_prefix.c build/prefix_default_sanitize.o build/prefix_custom_sanitize.o -o build/prefix_sanitize
	./build/prefix_sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -fsanitize=address,undefined -fno-omit-frame-pointer examples/shared_allocator.c -o build/shared_allocator_sanitize
	./build/shared_allocator_sanitize --test
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -UNDEBUG -DDICT_ENABLE_MEMORY_STATS=1 -DRTOS_PREFIX=app_ -fsanitize=address,undefined -fno-omit-frame-pointer examples/shared_allocator.c -o build/shared_allocator_prefix_sanitize
	./build/shared_allocator_prefix_sanitize --test

clean:
	rm -rf build
