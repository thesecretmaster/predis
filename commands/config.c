#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../public/commands.h"

static int print1cmd(struct predis_ctx *ctx, struct predis_arg *_data, char **argv, argv_length_t *argv_lengths, int _argc) {
  if (strcmp(argv[0], "SET") == 0) {
    for (int i = 0; i < _argc; i++)
      printf("%s ", argv[i]);
    printf("\n");
  }
  return 0;
}


// static int print1cmd_preload(command_bitmap bm, void *_data, char **_argv, int _argc) {
//   return 0;
// }

static const char cmd_name[] = "CONFIG";
static const char cmd_format[] = "SS";

int predis_init(void *magic_obj) {
  register_command(magic_obj, cmd_name, sizeof(cmd_name) - 1, &print1cmd, cmd_format, sizeof(cmd_format) - 1);
  return 0;
}
