#include <stdlib.h>
#include <string.h>
#include "../commands.h"
#include "../types/hash.h"

static int hash_hset(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  predis_arg_try_initialize(data, 0);
  hash_store(predis_arg_get(data, 0), argv[1], argv_lengths[1], strdup(argv[2]));
  return 0;
}

static int hash_hget(struct predis_ctx *ctx, struct predis_arg *data, char **argv, argv_length_t *argv_lengths, int argc) {
  char *str;
  int rval = hash_find(predis_arg_get(data, 0), argv[1], argv_lengths[1], &str);
  if (rval == 0) {
    replyBulkString(ctx, str, strlen(str));
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
  register_command(magic_obj, hhget, sizeof(hhget), &hash_hget, hhget_format, sizeof(hhget_format) - 1);
  register_command(magic_obj, hhset, sizeof(hhset), &hash_hset, hhset_format, sizeof(hhset_format) - 1);
  return 0;
}
