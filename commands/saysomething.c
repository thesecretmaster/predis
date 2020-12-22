#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../commands.h"

static int print1cmd(struct predis_ctx *ctx, struct predis_data **_data, char **_argv, argv_length_t *_argv_lengths, int _argc) {
  for (int i = 0; i < _argc; i++) {
    printf("%s ", _argv[i]);
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
  register_command(magic_obj, cmd_name, sizeof(cmd_name), &print1cmd, "");
  return 0;
}
