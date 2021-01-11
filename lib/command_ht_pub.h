#ifndef H_COMMAND_HT_PUB
#define H_COMMAND_HT_PUB
#include "../predis_ctx.h"
struct predis_arg;
typedef long argv_length_t;
typedef int (*command_func)(struct predis_ctx *ctx, struct predis_arg *argv_data, char **argv_strings, argv_length_t *argv_string_lengths, int argc);
typedef int (*meta_command_func)(struct predis_ctx *ctx, void *global_ht_table /* I know this should be type checked but tbh this is a bit of a hack */, char **argv, argv_length_t *argv_lengths, int argc);

#endif
