SHELL = bash
CC = clang # gcc
WARN_FLAGS = -Wall -Wpedantic -Weverything -Wextra
LIGHT_WARN_FLAGS = -Wall
SPEED_FLAGS = -Ofast -march=native
DEBUG_FLAGS = -g -ggdb
GPROF_FLAG = -pg
CFLAGS = -fshort-enums # -Rpass='[^(licm|gvn)]' -Rpass-missed="inline"
ALL_FLAGS = $(CFLAGS) $(DEBUG_FLAGS) $(SPEED_FLAGS) $(WARN_FLAGS)

all: bin/server commands/string.so commands/config.so types/string.so types/hash.so commands/hash.so

# lib/command_ht.c: lib/command_type_ht.c lib/command_ht.h
# 	cp $< $@
#
# lib/command_ht.h: lib/command_type_ht.h
# 	cp $< $@

tmp/%_ht.o: lib/%_ht.c
	$(CC) $(ALL_FLAGS) -c $^ -o $@

tmp/commands.o: lib/commands.c
	$(CC) $(ALL_FLAGS) -c $^ -o $@

tmp/full_ht.o: lib/hashtable.c
	$(CC) $(ALL_FLAGS) -DHT_ITERABLE -c $^ -o $@

bin/server: network_parser.c lib/gc.c lib/netwrap.c lib/resp_parser.c tmp/command_ht.o tmp/commands.o tmp/type_ht.o tmp/full_ht.o lib/1r1w_queue.c lib/timer.c
	$(CC) $(ALL_FLAGS) -ldl -pthread $^ -o $@

commands/string.so: types/string.so
commands/hash.so: types/hash.so
commands/%.so: commands/%.c tmp/commands.o tmp/command_ht.o tmp/type_ht.o lib/1r1w_queue.c
	$(CC) $(ALL_FLAGS) -Wno-unused-parameter -fPIC $^ -shared -o $@

types/hash.so: tmp/type_hash_hashtable.o
types/%.so: types/%.c tmp/commands.o tmp/command_ht.o tmp/type_ht.o lib/1r1w_queue.c
	$(CC) $(ALL_FLAGS) -Wno-unused-parameter -fPIC $^ -shared -o $@

tmp/hashtable.%.o: lib/hashtable.c
	$(CC) $(ALL_FLAGS) -DHT_VALUE_TYPE="$*" -c $^ -o $@

tmp/type_hash_hashtable.o: lib/hashtable.c
	$(CC) $(ALL_FLAGS) -DTYPE_HASH -c $^ -o $@

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
	rm bin/* commands/*.so tmp/*.o types/*.so
