#include "command_ht.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

// Both are optimally packed
struct command_ht_elem {
  const char *command_name;
  command_func command;
  unsigned int command_name_length;
  union command_preload_strategies preload;
  bool preload_func;
};

struct command_ht {
  struct command_ht_elem *elements;
  unsigned int size;
};

#pragma GCC diagnostic pop

struct command_ht *command_ht_init(unsigned int size) {
  struct command_ht *ht = malloc(sizeof(struct command_ht));
  if (ht == NULL)
    return NULL;
  ht->size = size;
  ht->elements = malloc(sizeof(struct command_ht_elem) * size);
  if (ht->elements == NULL) {
    free(ht);
    return NULL;
  }
  for (unsigned int i = 0; i < size; i++) {
    ht->elements[i].command_name = NULL;
    ht->elements[i].command_name_length = 0;
    ht->elements[i].command = NULL;
  }
  return ht;
}

// https://stackoverflow.com/a/7666577/4948732
static unsigned long
hash(const unsigned char *str)
{
    unsigned long hash = 5381;
    int c;

    while ((c = *str++))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdisabled-macro-expansion"
        hash = ((hash << 5) + hash) + (unsigned)tolower(c); /* hash * 33 + c */
#pragma GCC diagnostic pop

    return hash;
}

enum format_string_access_type {
  FSTRING_READONLY,
  FSTRING_MODIFY_NOCREATE,
  FSTRING_MODIFY_CREATE,
  FSTRING_CREATE_MODIFY,
  FSTRING_STRING
};

struct format_string_ll {
  enum format_string_access_type access_type;
  int types_list_length;
  void **types_list;
  struct format_string_ll *next;
};

static int parse_format_string(const char *str, const int len) {
  int stridx = 0;
  struct format_string_ll *head = NULL;
  struct format_string_ll *fstring_node;
  void *typeargs_end;
  while (stridx < len) {
    fstring_node = malloc(sizeof(struct format_string_ll))
    switch (str[stridx]) {
      case 'R' : {
        fstring_node->access_type = FSTRING_READONLY;
        stridx += 1;
        break;
      }
      case 'C' : {
        fstring_node->access_type = FSTRING_CREATE;
        stridx += 1;
        break;
      }
      case 'W' : {
        fstring_node->access_type = FSTRING_MODIFY_CREATE;
        stridx += 1;
        break;
      }
      case 'M' : {
        fstring_node->access_type = FSTRING_MODIFY_NOCREATE;
        stridx += 1;
        break;
      }
      case 'S' : {
        fstring_node->access_type = FSTRING_STRING;
        stridx += 1;
        break;
      }
      default : {
        return -1;
      }
    }
    if (str[stridx] != '{')
      return -2;
    stridx += 1;
    typeargs_end = memchr(str + stridx, '}', len - stridx);
  }
}

int command_ht_store(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, command_func command, union command_preload_strategies preload, bool preload_func) {
  unsigned int base_index = hash((const unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command != NULL) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return 1;
  }
  ht->elements[index].command_name = command_name;
  ht->elements[index].command_name_length = command_name_length;
  ht->elements[index].command = command;
  ht->elements[index].preload = preload;
  ht->elements[index].preload_func = preload_func;
  return 0;
}

command_func command_ht_fetch_command(struct command_ht *ht, char *command_name, const unsigned int command_name_length) {
  unsigned int base_index = hash((unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command != NULL && ht->elements[index].command_name_length != command_name_length && strncasecmp(command_name, ht->elements[index].command_name, command_name_length) != 0) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return NULL;
  }
  return ht->elements[index].command;
}

union command_preload_strategies command_ht_fetch_preload(struct command_ht *ht, char *command_name, const unsigned int command_name_length, bool *preload_strat) {
  unsigned int base_index = hash((unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command != NULL && ht->elements[index].command_name_length != command_name_length && strncasecmp(command_name, ht->elements[index].command_name, command_name_length) != 0) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return (union command_preload_strategies){.ptr = NULL };
  }
  *preload_strat = ht->elements[index].preload_func;
  return ht->elements[index].preload;
}
