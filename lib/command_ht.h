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
typedef int (*meta_command_func)(struct predis_ctx *ctx, void *global_ht_table /* I know this should be type checked but tbh this is a bit of a hack */, char **argv, argv_length_t *argv_lengths, int argc);

union command_ht_command_funcs {
  command_func normal;
  meta_command_func meta;
  void *ptr;
};

enum format_string_access_type {
  FSTRING_READONLY,
  FSTRING_READONLY_OPTIONAL,
  FSTRING_MODIFY_NOCREATE,
  FSTRING_MODIFY_CREATE,
  FSTRING_CREATE,
  FSTRING_STRING,
  FSTRING_JUMP
};

union format_string_details {
  struct type_ht_raw *type;
  unsigned int jump_target;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct format_string_node {
  union format_string_details details;
  enum format_string_access_type access_type;
};

struct format_string {
  struct format_string_node *contents;
  unsigned int length;
  unsigned long optional_argument_count;
};

#pragma GCC diagnostic pop

struct command_ht *command_ht_init(unsigned int size, struct type_ht *type_ht);

int command_ht_store(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *fstring, const unsigned int fstring_string_length);
int command_ht_store_meta(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, meta_command_func command);

int command_ht_fetch(struct command_ht *ht, char *command_name, const unsigned int command_name_length, struct format_string **fstring, union command_ht_command_funcs*, bool *is_meta);

void command_ht_print_commands(struct command_ht *ht);

#endif
