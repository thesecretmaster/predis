#include <stdlib.h>
#include <string.h>
#include "../public/commands.h"
#include "../types/hash.h"

static int hash_hset(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  if (argv_lengths[1] < 0)
    return PREDIS_FAILURE;
  struct hash **hsh;
  predis_arg_try_initialize(data, 0, (void***)&hsh, NULL);
  predis_arg_try_commit(data, 0, (void***)&hsh);
  hash_store(*hsh, argv[1], (unsigned int)argv_lengths[1], strdup(argv[2]));
  return 0;
}

static int hash_hget(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  char *str;
  if (argv_lengths[1] < 0)
    return PREDIS_FAILURE;
  int rval = hash_find(*predis_arg_get(data, 0), argv[1], (unsigned int)argv_lengths[1], &str);
  if (rval == 0) {
    replyBulkString(ctx, str, (long)strlen(str));
  } else if (rval == -1) {
    replyBulkString(ctx, NULL, -1);
  } else {
    replyError(ctx, "Something went wrong");
  }
  return 0;
}

// static int print1cmd_preload(command_bitmap bm, void *_data, char **_argv, int _argc) {
//   return 0;
// }

static const char hhset[] = "hset";
static const char hhset_format[] = "W{hash}SS";
static const char hhget[] = "hget";
static const char hhget_format[] = "R{hash}S";

int predis_init(void *magic_obj) {
  register_command(magic_obj, hhget, sizeof(hhget) - 1, &hash_hget, hhget_format, sizeof(hhget_format) - 1);
  register_command(magic_obj, hhset, sizeof(hhset) - 1, &hash_hset, hhset_format, sizeof(hhset_format) - 1);
  return 0;
}
