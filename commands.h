#include "lib/command_bitmap_lib.h"
#include "lib/command_ht.h"
#include <stdlib.h>
#include "lib/1r1w_queue.h"

typedef int (*predis_init_func)(void*);

enum command_errors {
  WRONG_ARG_COUNT,
  DLOPEN_FAILED,
  PREDIS_SUCCESS,
  INVALID_TYPE
};

struct predis_ctx;
struct predis_data {
  const char *data_type;
  void *data;
};
struct pre_send_data {
  size_t length;
  const char *msg;
};
int register_command(struct predis_ctx *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *data_str);
int replySimpleString(struct predis_ctx *ctx, const char *ss);
int predis_init(void *magic_obj);
int replyBulkString(struct predis_ctx *ctx, const char *ss, long ss_len);
