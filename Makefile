SHELL = bash
CC = clang # gcc
CFLAGS = -Wall -g -ggdb -pg -Ofast -march=native -Wpedantic

all: testing test

testing: network_parser.c lib/netwrap.c lib/resp_parser.c
	$(CC) $(CFLAGS) -pthread $^ -o $@

test: test.c
	$(CC) $(CFLAGS) $^ -o $@
