#include "lib/command_ht.h"
#include "lib/type_ht.h"
#include "commands.h"
#include "predis_ctx.h"
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "commands_data_types.h"

#include "lib/1r1w_queue.h"

#include "predis_arg_impl.c"
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

void *predis_arg_get(struct predis_arg *args, unsigned int idx) {
  if (args[idx].needs_initialization) {
    printf("ERROR: Tried to get uninitialized arg\n");
    return NULL;
  }
  return args[idx].data->data;
}

void *predis_arg_try_initialize(struct predis_arg *arg, unsigned int idx) {
  if (!arg[idx].needs_initialization) {
    printf("WARNING: Tried to get initialize an arg that didn't need initialization (this is fine if we're in a modify/create, it just means we hit the modify option of that)\n");
    return NULL;
  }
  arg[idx].data->type->init(&arg[idx].data->data);
  arg[idx].needs_initialization = false;
  __atomic_store_n((void**)arg[idx].ht_value, arg[idx].data, __ATOMIC_SEQ_CST);
  return arg[idx].ht_value;
}


int register_command(struct predis_ctx *ctx, const char *command_name, const unsigned int command_name_length, command_func command, const char *format, const unsigned int format_length) {
  int r = command_ht_store(ctx->command_ht, command_name, command_name_length, command, format, format_length);
  printf("Stored command %.*s (%d)\n", command_name_length, command_name, r);
  return 0;
}

int register_type(struct predis_ctx *ctx, const char *type_name, unsigned int type_name_length, type_init_func tinit, type_free_func tfree) {
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
