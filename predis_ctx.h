#include "lib/command_ht.h"
#include <stdbool.h>

struct predis_ctx {
  struct command_ht *command_ht;
  int reply_fd;
  bool needs_reply;
};
