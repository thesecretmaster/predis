#include "lib/command_ht.h"
#include "lib/type_ht.h"
#include "commands.h"
#include "predis_ctx.h"
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "lib/1r1w_queue.h"


/*
a|foobar|b foobar is looped
w = write, existance optional
W = write, existance mandatory
r = read, existance optional
R = read, existance mandatory
c = write, non-existance mandatory

s = string
i = int
*/
int register_command(struct predis_ctx *ctx, const char *command_name, const unsigned int command_name_length, command_func command, const char *format, const unsigned int format_length) {
  int r = command_ht_store(ctx->command_ht, command_name, command_name_length, command, format, format_length);
  printf("Stored command %.*s (%d)\n", command_name_length, command_name, r);
  return 0;
}

int register_type(struct predis_ctx *ctx, const char *type_name, unsigned int type_name_length, type_init_func tinit, type_free_func tfree) {
  printf("Storing type %.*s\n", type_name_length, type_name);
  int r = type_ht_store(ctx->type_ht, type_name, type_name_length, &((struct type_ht_raw){tinit, tfree}));
  printf("Stored type %.*s (%d)\n", type_name_length, type_name, r);
  return 0;
}

int replySimpleString(struct predis_ctx *ctx, const char *ss) {
  if (!ctx->needs_reply)
    return 1;

  struct pre_send pre_send = {
    .type = PRE_SEND_SS,
    .data.ss = ss
  };
  queue_push(ctx->sending_queue, &pre_send);
  ctx->needs_reply = false;
  return 0;
}

int replyError(struct predis_ctx *ctx, const char *err) {
  if (!ctx->needs_reply)
    return 1;

  struct pre_send pre_send = {
    .type = PRE_SEND_ERR,
    .data.ss = err
  };
  queue_push(ctx->sending_queue, &pre_send);
  ctx->needs_reply = false;
  return 0;
}

int replyInt(struct predis_ctx *ctx, const long i) {
  if (!ctx->needs_reply)
    return 1;

  struct pre_send pre_send = {
    .type = PRE_SEND_NUM,
    .data.num = i
  };
  queue_push(ctx->sending_queue, &pre_send);
  ctx->needs_reply = false;
  return 0;
}

int replyBulkString(struct predis_ctx *ctx, const char *ss, long ss_len) {
  if (!ctx->needs_reply)
    return 1;

  struct pre_send pre_send = {
    .type = PRE_SEND_BS,
    .data.bs = {
      .length = ss_len,
      .contents = ss
    }
  };
  queue_push(ctx->sending_queue, &pre_send);
  ctx->needs_reply = false;
  return 0;
}
