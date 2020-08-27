SHELL = bash
CC = clang # gcc
WARN_FLAGS = -Wall -Wpedantic -Weverything -Wextra
LIGHT_WARN_FLAGS = -Wall
SPEED_FLAGS = -Ofast -march=native
DEBUG_FLAGS = -g -ggdb
GPROF_FLAG = -pg
CFLAGS = -fshort-enums # -Rpass='[^(licm|gvn)]' -Rpass-missed="inline"
ALL_FLAGS = $(CFLAGS) $(DEBUG_FLAGS) $(SPEED_FLAGS) $(WARN_FLAGS)

all: bin/server commands/saysomething.so commands/string.so commands/config.so bin/getset_test

# Command-shared.c shouldn't be in this list, it's just for temporaries
bin/server: network_parser.c lib/netwrap.c lib/resp_parser.c lib/command_ht.c lib/hashtable.c lib/1r1w_queue.c command-shared.c
	$(CC) $(ALL_FLAGS) -DHT_VALUE_TYPE="struct predis_data*" -ldl -pthread $^ -o $@

commands/%.so: commands/%.c command-shared.c lib/command_ht.c lib/1r1w_queue.c
	$(CC) $(ALL_FLAGS) -Wno-unused-parameter -fPIC $^ -shared -o $@

strsearch: strsearch.c
	$(CC) $(DEBUG_FLAGS) $(SPEED_FLAGS) -Wall $^ -o $@

test: test.c
	$(CC) $(CFLAGS) $^ -o $@

foobar: tests/new_reader.c lib/resp_parser.c lib/netwrap.c
	$(CC) -pthread $(DEBUG_FLAGS) $(SPEED_FLAGS) $(LIGHT_WARN_FLAGS) $^ -o $@

bin/ht_test: ht_test_normal ht_test_parallel

bin/ht_test_parallel: tests/hashtable_parallel.c lib/hashtable.c
	$(CC) $(ALL_FLAGS) -pthread -DHT_TEST_API -DHT_VALUE_TYPE="char*" $^ ../predis/lib/random_string.c -o $@

bin/getset_test: tests/getset_parallel.c
	$(CC) $(ALL_FLAGS) -pthread -lhiredis $^ ../predis/lib/random_string.c -o $@


ht_test_normal: tests/hashtable_serial.c lib/hashtable.c
	$(CC) $(ALL_FLAGS) -pthread -DHT_TEST_API -DHT_VALUE_TYPE="unsigned int" $^ -o $@

$PHONY: clean ht_test

clean:
	rm bin/* commands/*.so
