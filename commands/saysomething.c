#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../public/commands.h"

static int print1cmd(struct predis_ctx *ctx, void **_data, char **argv, argv_length_t *argv_lengths, int argc) {
  for (int i = 0; i < argc; i++) {
    printf("%.*s ", argv_lengths[i], argv[i]);
  }
  printf("\n");
  replySimpleString(ctx, "test reply");
  return 0;
}

// static int print1cmd_preload(command_bitmap bm, void *_data, char **_argv, int _argc) {
//   return 0;
// }

static const char cmd_name[] = "saysomething";

int predis_init(void *magic_obj) {
  register_command(magic_obj, cmd_name, sizeof(cmd_name), &print1cmd, "", 0);
  return 0;
}
