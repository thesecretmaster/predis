#include "command_bitmap_lib.h"
#include <stdbool.h>

#ifndef LIB_COMMAND_HT
#define LIB_COMMAND_HT

struct predis_ctx;

struct predis_data;

typedef int (*command_preload_func)(command_bitmap bm, struct predis_data**cmd, char**argv, unsigned long *argv_lengths, int argc);
typedef int (*command_func)(struct predis_ctx *ctx, struct predis_data**cmd, char**argv, unsigned long *argv_lengths, int argc);

struct command_ht;

struct command_ht *command_ht_init(unsigned int size);

union __attribute__ ((__transparent_union__)) command_preload_strategies {
  command_preload_func preload_func;
  const char *format_string;
  void *ptr;
};

int command_ht_store(struct command_ht *ht, const char *command_name, command_func command, union command_preload_strategies, bool);

command_func command_ht_fetch_command(struct command_ht *ht, char *command_name);
union command_preload_strategies command_ht_fetch_preload(struct command_ht *ht, char *command_name, bool *preload_func);

#endif
