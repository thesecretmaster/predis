#ifndef H_PREDIS_CTX
#define H_PREDIS_CTX

// #include "lib/command_ht.h"
// #include "lib/type_ht.h"
#include <stdbool.h>
#include "lib/1r1w_queue.h"

#define PREDIS_CTX_CHAR_BUF_SIZE 512

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

// Optmially packed already
struct predis_ctx {
  struct queue *sending_queue;
  struct command_ht *command_ht;
  struct type_ht *type_ht;
  char *reply_buf;
  struct send_queue *send_queue;
  unsigned int send_queue_ptr;
  int reply_fd;
  bool needs_reply;
};

#pragma GCC diagnostic pop

#endif
