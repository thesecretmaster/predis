#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdlib.h>
#include "lib/1r1w_queue.h"

typedef int (*predis_init_func)(void*);

enum command_errors {
  WRONG_ARG_COUNT = 1,
  DLOPEN_FAILED = 3,
  PREDIS_SUCCESS = 0,
  INVALID_TYPE = 2
};

struct predis_ctx;

typedef long argv_length_t;
// typedef int (*command_preload_func)(command_bitmap bm, struct predis_data**cmd, char**argv, argv_length_t *argv_lengths, int argc);
#include "lib/command_ht.h"
#include "lib/type_ht.h"

struct predis_arg;

int register_command(struct predis_ctx *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *data_str, const unsigned int);
int register_type(struct predis_ctx *ctx, const char *type_name, unsigned int type_name_length, type_init_func tinit, type_free_func tfree);
int predis_init(void *magic_obj);
void *predis_arg_get(struct predis_arg*, unsigned int idx);
void *predis_arg_try_initialize(struct predis_arg*, unsigned int idx);
int replyBulkString(struct predis_ctx *ctx, const char *ss, long ss_len);
int replySimpleString(struct predis_ctx *ctx, const char *ss);
int replyInt(struct predis_ctx *ctx, const long i);
int replyError(struct predis_ctx *ctx, const char *err);
#endif
