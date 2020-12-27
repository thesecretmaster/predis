#include <stdlib.h>
#include <string.h>
#include "../commands.h"
#include "../types/string.h"

static const char *type_name = "string";

// Thought processes: Hashtable holds a CONSTANT pointer (data[i])
// data[i] needs to be a (struct string**) because we can't change it

static int string_set(struct predis_ctx *ctx, void **data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 2)
    return WRONG_ARG_COUNT;

  if (argv_lengths[1] < 0)
    return -100; // uhhhh use a real error next time ok?

  struct string *str = malloc(sizeof(struct string));
  str->data = argv[1];
  str->length = argv_lengths[1];
  // printf("Using argv %.*s\n", argv_lengths[0], argv[0]);
  __atomic_store_n((struct string **)data[0], str, __ATOMIC_SEQ_CST);
  return PREDIS_SUCCESS;
}

static int string_get(struct predis_ctx *ctx, void **data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 1)
    return WRONG_ARG_COUNT;

  struct string *str = __atomic_load_n((struct string**)data[0], __ATOMIC_SEQ_CST);
  replyBulkString(ctx, str->data, str->length);
  return PREDIS_SUCCESS;
}

static int string_bitcount(struct predis_ctx *ctx, void **data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argc != 1 && argc != 3)
    return WRONG_ARG_COUNT;
  struct string *str = __atomic_load_n((struct string **)data[0], __ATOMIC_SEQ_CST);
  long start = 0;
  long end = str->length;
  if (argc == 3) {
    start = strtol(argv[1], NULL, 10);
    end = strtol(argv[2], NULL, 10);
    if (start < 0) { start = str->length + start + 1; }
    if (end < 0) { end = str->length + end + 1; }
    if (start < 0 || end < 0 || start > str->length || end > str->length || start > end) {
      replySimpleString(ctx, "0");
      return 0;
    }
  }
  unsigned int set_bits = 0;
  for (long i = start; i < end; i++) {
    set_bits += (unsigned int)__builtin_popcount((unsigned int)str->data[i]);
  }
  int output_strlen = snprintf(NULL, 0, "%u", set_bits);
  char *output = malloc(sizeof(char) * ((unsigned long)output_strlen + 1));
  snprintf(output, (unsigned long)output_strlen + 1, "%u", set_bits);
  replySimpleString(ctx, output);
  return PREDIS_SUCCESS;
}

static const char sset[] = "SET";
static const char sset_format[] = "W{string}S";
static const char sget[] = "GET";
static const char sget_format[] = "R{string}";
static const char sbitcount[] = "BITCOUNT";
static const char sbitcount_format[] = "R{string}ii";

/*
a|foobar|b foobar is looped
w = write, existance optional
W = write, existance mandatory
r = read, existance optional
R = read, existance mandatory
c = write, non-existance mandatory
*/

int predis_init(void *magic_obj) {
  register_command(magic_obj, sset, sizeof(sset), &string_set, sset_format, sizeof(sset_format) - 1);
  register_command(magic_obj, sget, sizeof(sget), &string_get, sget_format, sizeof(sget_format) - 1);
  register_command(magic_obj, sbitcount, sizeof(sbitcount), &string_bitcount, sbitcount_format, sizeof(sbitcount_format) - 1);
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
