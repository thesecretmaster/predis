SHELL = bash
CC = clang # gcc
WARN_FLAGS = -Wall -Wpedantic -Weverything
SPEED_FLAGS = -Ofast -march=native
DEBUG_FLAGS = -g -ggdb -pg
CFLAGS = -fshort-enums # -Rpass='[^(licm|gvn)]' -Rpass-missed="inline"

all: testing test

testing: network_parser.c lib/netwrap.c lib/resp_parser.c
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(SPEED_FLAGS) $(WARN_FLAGS) -pthread $^ -o $@

strsearch: strsearch.c
	$(CC) $(DEBUG_FLAGS) $(SPEED_FLAGS) -Wall $^ -o $@

test: test.c
	$(CC) $(CFLAGS) $^ -o $@
