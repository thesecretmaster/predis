#include "lib/command_bitmap_lib.h"
#include "lib/command_ht.h"

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
int register_command(struct predis_ctx *ht, const char *command_name, command_func command, const char *data_str);
int replySimpleString(struct predis_ctx *ctx, const char *ss);
int predis_init(void *magic_obj);
