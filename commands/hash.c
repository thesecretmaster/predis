#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../commands.h"

static int hash_hset(struct predis_ctx *ctx, struct predis_data **_data, char **_argv, argv_length_t *_argv_lengths, int argc) {
  printf("Called!\n");
  if (argc != 3)
    return WRONG_ARG_COUNT;

  replySimpleString(ctx, "hiiiiii");
  return 0;
}

// static int print1cmd_preload(command_bitmap bm, void *_data, char **_argv, int _argc) {
//   return 0;
// }

static const char hhset[] = "hset";

int predis_init(void *magic_obj) {
  register_command(magic_obj, hhset, sizeof(hhset), &hash_hset, "Rcc", 3);
  return 0;
}
