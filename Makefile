CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic -Werror
SOURCES = tests/test_fht.c tests/other_tu.c

.PHONY: test sanitize clean

test: build/fht_test_release
	./build/fht_test_release

build/fht_test_release: $(SOURCES) include/fht.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@

sanitize:
	mkdir -p build
	$(CC) $(CPPFLAGS) -std=c99 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(SOURCES) -o build/fht_test_sanitize
	./build/fht_test_sanitize

clean:
	rm -rf build
