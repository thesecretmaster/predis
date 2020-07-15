#include "lib/command_ht.h"
#include <stdbool.h>

#define PREDIS_CTX_CHAR_BUF_SIZE 512

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

// Optmially packed already
struct predis_ctx {
  struct command_ht *command_ht;
  char *reply_buf;
  int reply_fd;
  bool needs_reply;
};

#pragma GCC diagnostic pop
