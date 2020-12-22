#ifndef LIB_COMMAND_HT
#define LIB_COMMAND_HT

#include <stdbool.h>
#include "../commands.h"

struct command_ht;

struct command_ht *command_ht_init(unsigned int size);

union __attribute__ ((__transparent_union__)) command_preload_strategies {
  // command_preload_func preload_func;
  const char *format_string;
  void *ptr;
};

int command_ht_store(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, command_func command, union command_preload_strategies, bool);

command_func command_ht_fetch_command(struct command_ht *ht, char *command_name, const unsigned int command_name_length);
union command_preload_strategies command_ht_fetch_preload(struct command_ht *ht, char *command_name, const unsigned int command_name_length, bool *preload_func);

#endif
