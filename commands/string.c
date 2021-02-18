#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../public/commands.h"
#include "../types/string.h"
#include "redis/string.c"

static int command_set(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 2)
    return WRONG_ARG_COUNT;

  if (argv_lengths[1] < 0)
    return -100; // uhhhh use a real error next time ok?
  struct string **str;
  struct string_args str_args = {.len = argv_lengths[1], .str = malloc(sizeof(char) * argv_lengths[1])};
  memcpy(str_args.str, argv[1], argv_lengths[1]);
  retry:
  if (predis_arg_requires_initialization(data, 0)) {
    predis_arg_try_initialize(data, 0, (void***)&str, &str_args);
    if (predis_arg_try_commit(data, 0, (void***)&str) != 0) {
      goto retry;
    }
  } else {
    string_set(predis_arg_get(data, 0), argv[1], argv_lengths[1]);
  }
  return PREDIS_SUCCESS;
}

static int command_get(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 1)
    return WRONG_ARG_COUNT;

  char *str;
  long length;
  string_get(*predis_arg_get(data, 0), &str, &length);
  replyBulkString(ctx, str, length);
  return PREDIS_SUCCESS;
}

static int command_bitcount(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 1 && argc != 3)
    return WRONG_ARG_COUNT;
  char *str;
  long length;
  string_get(*predis_arg_get(data, 0), &str, &length);

  // long start = 0;
  // long end = length;
  // if (argc == 3) {
  //   start = strtol(argv[1], NULL, 10);
  //   end = strtol(argv[2], NULL, 10);
  //   if (start < 0) { start = length + start + 1; }
  //   if (end < 0) { end = length + end + 1; }
  //   if (start < 0 || end < 0 || start > length || end > length || start > end) {
  //     replySimpleString(ctx, "0");
  //     return 0;
  //   }
  // }
  // unsigned int set_bits = 0;
  // for (long i = start; i < end; i++) {
  //   set_bits += (unsigned int)__builtin_popcount((unsigned int)str[i]);
  // }
  size_t set_bits = redisPopcount(str, length);
  int output_strlen = snprintf(NULL, 0, "%zu", set_bits);
  char *output = malloc(sizeof(char) * ((unsigned long)output_strlen + 1));
  snprintf(output, (unsigned long)output_strlen + 1, "%zu", set_bits);
  replySimpleString(ctx, output);
  return PREDIS_SUCCESS;
}

static int command_bitpos(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  char *str;
  long length;
  string_get(*predis_arg_get(data, 0), &str, &length);
  long bp = redisBitpos(str, (unsigned long)length, (int)strtol(argv[1], NULL, 10));
  replyInt(ctx, (int)bp);
  return PREDIS_SUCCESS;
}

static int command_getset(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 2)
    return WRONG_ARG_COUNT;

  if (argv_lengths[1] < 0)
    return -100; // uhhhh use a real error next time ok?

  char *str_raw;
  long length;
  string_exchange(predis_arg_get(data, 0), &str_raw, &length, argv[1], argv_lengths[1]);
  replyBulkString(ctx, str_raw, length);
  return PREDIS_SUCCESS;
}

static int command_strlen(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  char *str;
  long length;
  string_get(*predis_arg_get(data, 0), &str, &length);
  replyInt(ctx, length);
  return 0;
}

static int command_getrange(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  char *str;
  long length;
  string_get(*predis_arg_get(data, 0), &str, &length);
  long start = strtol(argv[1], NULL, 10);
  long end = strtol(argv[2], NULL, 10);
  if (start < 0)
    start = length + start + 1;
  if (end < 0)
    end = length + end + 1;
  if (start < 0 || end < 0 || end < start || end > length) {
    replyError(ctx, "ERR invalid range");
    return PREDIS_FAILURE;
  } else {
    replyBulkString(ctx, str + start, end - start);
    return PREDIS_SUCCESS;
  }
}

static const char sset[] = "SET";
static const char sset_format[] = "W{string}S";
static const char sget[] = "GET";
static const char sget_format[] = "R{string}";
static const char sgetset[] = "GETSET";
static const char sgetset_format[] = "M{string}S";
static const char sbitcount[] = "BITCOUNT";
static const char sbitcount_format[] = "R{string}";
static const char sbitpos[] = "BITPOS";
static const char sbitpos_format[] = "R{string}S";
static const char sstrlen[] = "STRLEN";
static const char sstrlen_format[] = "R{string}";
static const char smget[] = "MGET";
static const char smget_format[] = "S|r{string}|S";
static const char sgetrange[] = "GETRANGE";
static const char sgetrange_format[] = "R{string}SS";

static int command_mget(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  printf("MGET called\n");
  for (int i = 1; i < argc - 1; i++) {
    printf("Mget field: %.*s\n", (int)argv_lengths[i], argv[i]);
  }
  printf("First: %.*s\nLast: %.*s\n", (int)argv_lengths[0], argv[0], (int)argv_lengths[argc - 1], argv[argc - 1]);
  return PREDIS_SUCCESS;
}

/*
TODO:

append
bitfield
bitop
decr
decrby
getbit
getrange
incr
indrby
instrbyfloat
mget
mset
msetnx
psetex
setbit
setex
setnx
setrange
stralgo
*/

/*
a|foobar|b foobar is looped
w = write, existance optional
W = write, existance mandatory
r = read, existance optional
R = read, existance mandatory
c = write, non-existance mandatory
*/

int predis_init(void *magic_obj) {
  register_command(magic_obj, sset, sizeof(sset) - 1, &command_set, sset_format, sizeof(sset_format) - 1);
  register_command(magic_obj, sget, sizeof(sget) - 1, &command_get, sget_format, sizeof(sget_format) - 1);
  register_command(magic_obj, sgetset, sizeof(sgetset) - 1, &command_getset, sgetset_format, sizeof(sgetset_format) - 1);
  register_command(magic_obj, sbitcount, sizeof(sbitcount) - 1, &command_bitcount, sbitcount_format, sizeof(sbitcount_format) - 1);
  register_command(magic_obj, sbitpos, sizeof(sbitpos) - 1, &command_bitpos, sbitpos_format, sizeof(sbitpos_format) - 1);
  register_command(magic_obj, sstrlen, sizeof(sstrlen) - 1, &command_strlen, sstrlen_format, sizeof(sstrlen_format) - 1);
  register_command(magic_obj, smget, sizeof(smget) - 1, &command_mget, smget_format, sizeof(smget_format) - 1);
  register_command(magic_obj, sgetrange, sizeof(sgetrange) - 1, &command_getrange, sgetrange_format, sizeof(sgetrange_format) - 1);
  return 0;
}

/*
Structure ideas:

Preload allows a) store prelim data and b) store a list of keys
Command then getsd a) prelim data and b) a list of actual key pointers

Both preload and command get argc and argv cuz why not

(what happens if a key is deleted in the middle? possible to terminate? what
about invalid keys?)

Commands can also request additional keys but this is challenging because they
can't really get exclusive access since they haven't been scheduled for that key



*/
