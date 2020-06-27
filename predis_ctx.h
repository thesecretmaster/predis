#include "lib/command_ht.h"
#include <stdbool.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

// Optmially packed already
struct predis_ctx {
  struct command_ht *command_ht;
  int reply_fd;
  bool needs_reply;
};

#pragma GCC diagnostic pop
