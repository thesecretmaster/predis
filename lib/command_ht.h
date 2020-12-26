#ifndef LIB_COMMAND_HT
#define LIB_COMMAND_HT

#include <stdbool.h>
#include "../predis_ctx.h"
typedef long argv_length_t;
struct predis_data {
  const char *data_type;
  void *data;
};

struct command_ht;
typedef int (*command_func)(struct predis_ctx *ctx, void **cmd, char **argv, argv_length_t *argv_lengths, int argc);

enum format_string_access_type {
  FSTRING_READONLY,
  FSTRING_MODIFY_NOCREATE,
  FSTRING_MODIFY_CREATE,
  FSTRING_CREATE,
  FSTRING_STRING
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct format_string_node {
  struct type_ht_raw *type;
  enum format_string_access_type access_type;
};

struct format_string {
  struct format_string_node *contents;
  unsigned int length;
};

#pragma GCC diagnostic pop

struct command_ht *command_ht_init(unsigned int size, struct type_ht *type_ht);

int command_ht_store(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *fstring, const unsigned int fstring_string_length);

int command_ht_fetch(struct command_ht *ht, char *command_name, const unsigned int command_name_length, struct format_string **fstring, command_func *cf);

#endif
