#include "command_ht.h"
#include "type_ht.h"
#include "../public/commands.h"
#include "../public/types.h"
#include "../predis_ctx.h"
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../send_queue.h"
#include "send_queue.h"
#include "1r1w_queue.h"

#include "../predis_arg_impl.c"
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

void **predis_arg_get(struct predis_arg *args, unsigned int idx) {
  if (args[idx].needs_initialization) {
    printf("ERROR: Tried to get uninitialized or uncommitted arg\n");
    return NULL;
  }
  return &args[idx].data->data;
}

bool predis_arg_requires_initialization(struct predis_arg *arg, unsigned int idx) {
  return __atomic_load_n((void**)arg[idx].ht_value, __ATOMIC_SEQ_CST) == NULL;
}

bool predis_arg_try_initialize(struct predis_arg *arg, unsigned int idx, void ***value, void *initializer_args) {
  if (!arg[idx].needs_initialization) {
    printf("WARNING: Tried to initialize an arg that didn't need initialization (this is fine if we're in a modify/create, it just means we hit the modify option of that)\n");
    if (value != NULL)
      *value = &(arg[idx].data->data);
    return false;
  }
  arg[idx].needs_initialization = false;
  arg[idx].data->type->init(&(arg[idx].data->data), initializer_args);
  if (value != NULL)
    *value = &arg[idx].data->data;
  return true;
}

int predis_arg_try_commit(struct predis_arg *args, unsigned int idx, void ***value) {
  if (args[idx].needs_initialization || !args[idx].needs_commit) {
    printf("WARNING: Tried to commit an arg either did not need to be committed or had not been initialized\n");
    return -2;
  }
  void *nul = NULL;
  // printf("Committing value. Value is in %p\n", arg[idx].data);
  if (__atomic_compare_exchange_n((void**)args[idx].ht_value, &nul, args[idx].data, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    // printf("We committed it! Now it's in %p\n", *(void**)arg[idx].ht_value);
    // arg[idx].data = *(struct predis_typed_data**)arg[idx].ht_value;
    if (value != NULL)
      *value = &args[idx].data->data;
    return 0;
  } else if ((*(struct predis_typed_data**)(args[idx].ht_value))->type == args->data->type) {
    // printf("No. Bad\n");
    args[idx].data = *(struct predis_typed_data**)args[idx].ht_value;
    if (value != NULL)
      *value = &args[idx].data->data;
    return 1;
  } else {
    // printf("Very bad\n");
    return -1;
  }
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
  send_queue_commit(ctx->send_queue, ctx->send_queue_ptr, &pre_send);
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
  send_queue_commit(ctx->send_queue, ctx->send_queue_ptr, &pre_send);
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
  send_queue_commit(ctx->send_queue, ctx->send_queue_ptr, &pre_send);
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
  send_queue_commit(ctx->send_queue, ctx->send_queue_ptr, &pre_send);
  ctx->needs_reply = false;
  return 0;
}
