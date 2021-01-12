#include "commands_types.h"
#include "../lib/command_ht_pub.h"

enum command_errors {
  WRONG_ARG_COUNT = 1,
  DLOPEN_FAILED = 3,
  PREDIS_SUCCESS = 0,
  INVALID_TYPE = 2,
  PREDIS_FAILURE = 4
};

int register_command(struct predis_ctx *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *data_str, const unsigned int);

void **predis_arg_get(struct predis_arg*, unsigned int idx);
bool predis_arg_try_initialize(struct predis_arg*, unsigned int idx, void ***val, void *initializer_args);
int predis_arg_try_commit(struct predis_arg *arg, unsigned int idx, void ***val);
bool predis_arg_requires_initialization(struct predis_arg *arg, unsigned int idx);

int replyBulkString(struct predis_ctx *ctx, const char *ss, long ss_len);
int replySimpleString(struct predis_ctx *ctx, const char *ss);
int replyInt(struct predis_ctx *ctx, const long i);
int replyError(struct predis_ctx *ctx, const char *err);
