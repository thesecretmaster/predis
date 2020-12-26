#include "command_ht.h"
#include "type_ht.h"
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
  struct format_string *fstring;
};

struct command_ht {
  struct command_ht_elem *elements;
  struct type_ht *type_ht;
  unsigned int size;
};

#pragma GCC diagnostic pop

struct command_ht *command_ht_init(unsigned int size, struct type_ht *type_ht) {
  struct command_ht *ht = malloc(sizeof(struct command_ht));
  if (ht == NULL)
    return NULL;
  ht->size = size;
  ht->type_ht = type_ht;
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

struct format_string_ll {
  struct format_string_node node;
  struct format_string_ll *next;
};

static int parse_format_string(struct type_ht *type_ht, const char *str, const unsigned int len, struct format_string **fstring) {
  if (len == 0) {
    *fstring = &(struct format_string){.length = 0, .contents = NULL};
    return 0; // empty fstring wtf
  }
  unsigned int stridx = 0;
  struct format_string_ll *fstring_node;
  char *typeargs_end;
  struct format_string_ll *prev = NULL;
  struct format_string_ll *head = NULL;
  unsigned int fstring_length = 0;
  bool expect_type;
  // First, convert the fstring to a linked list
  while (stridx < len) {
    fstring_length++;
    if ((fstring_node = malloc(sizeof(struct format_string_ll))) == NULL)
      return -4;
    if (head == NULL)
      head = fstring_node;
    if (prev != NULL)
      prev->next = fstring_node;
    fstring_node->next = NULL;
    expect_type = true;
    switch (str[stridx]) {
      case 'R' : {
        fstring_node->node.access_type = FSTRING_READONLY;
        stridx += 1;
        break;
      }
      case 'C' : {
        fstring_node->node.access_type = FSTRING_CREATE;
        stridx += 1;
        break;
      }
      case 'W' : {
        fstring_node->node.access_type = FSTRING_MODIFY_CREATE;
        stridx += 1;
        break;
      }
      case 'M' : {
        fstring_node->node.access_type = FSTRING_MODIFY_NOCREATE;
        stridx += 1;
        break;
      }
      case 'S' : {
        fstring_node->node.access_type = FSTRING_STRING;
        stridx += 1;
        expect_type = false;
        break;
      }
      default : {
        return -1;
      }
    }
    if (expect_type) {
      if (str[stridx] != '{')
        return -2;
      stridx += 1;
      typeargs_end = memchr(str + stridx, '}', (unsigned)(len - stridx));
      if (type_ht_fetch(type_ht, str + stridx, (unsigned)(typeargs_end - (str + stridx)), &(fstring_node->node.type)) != 0) {
        return -3;
      }
      stridx = (typeargs_end - str) + 1;
    } else {
      fstring_node->node.type = NULL;
    }
    prev = fstring_node;
  }
  // Now that we have a linked list, we're going to turn it into an array, since we don't
  // really care about function load time performance and we do care a lot about runtime
  // access performance
  struct format_string_ll *next = (struct format_string_ll *)0x1; // Silence uninitialized var warning, it is gauranteed to be initialized.
  struct format_string_ll *curr = head;
  struct format_string_node *parsed_fstring = malloc(sizeof(struct format_string_node) * fstring_length);
  if (parsed_fstring == NULL) {
    return -9;
  }
  for (unsigned int i = 0; i < fstring_length; i++) {
    parsed_fstring[i].type = curr->node.type;
    parsed_fstring[i].access_type = curr->node.access_type;
    next = curr->next;
    free(curr);
    curr = next;
  }
  if (next != NULL) {
    return -5;
  }
  struct format_string *fstring_final = malloc(sizeof(struct format_string));
  if (fstring_final == NULL) {
    return -6;
  }
  fstring_final->length = fstring_length;
  fstring_final->contents = parsed_fstring;
  *fstring = fstring_final;
  return 0;
}

int command_ht_store(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *fstring_string, const unsigned int fstring_string_length) {
  unsigned int base_index = hash((const unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command != NULL) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return 1;
  }
  struct format_string *fstring;
  int pfs_rval;
  if ((pfs_rval = parse_format_string(ht->type_ht, fstring_string, fstring_string_length, &fstring)) != 0) {
    return -1;
  }
  ht->elements[index].command_name = command_name;
  ht->elements[index].command_name_length = command_name_length;
  ht->elements[index].command = command;
  ht->elements[index].fstring = fstring;
  return 0;
}

int command_ht_fetch(struct command_ht *ht, char *command_name, const unsigned int command_name_length, struct format_string **fstring, command_func *command_func) {
  unsigned int base_index = hash((unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command_name != NULL && ht->elements[index].command_name_length != command_name_length && strncasecmp(command_name, ht->elements[index].command_name, command_name_length) != 0) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return 1;
  }
  if (ht->elements[index].command_name == NULL)
    return 2;
  *fstring = ht->elements[index].fstring;
  *command_func = ht->elements[index].command;
  return 0;
}

// command_func command_ht_fetch_command(struct command_ht *ht, char *command_name, const unsigned int command_name_length) {
//   unsigned int base_index = hash((unsigned char*)command_name) % ht->size;
//   unsigned int index = base_index;
//   while (ht->elements[index].command != NULL && ht->elements[index].command_name_length != command_name_length && strncasecmp(command_name, ht->elements[index].command_name, command_name_length) != 0) {
//     index = index + 1 % ht->size;
//     if (index == base_index)
//       return NULL;
//   }
//   return ht->elements[index].command;
// }
//
// union command_preload_strategies command_ht_fetch_preload(struct command_ht *ht, char *command_name, const unsigned int command_name_length, bool *preload_strat) {
//   unsigned int base_index = hash((unsigned char*)command_name) % ht->size;
//   unsigned int index = base_index;
//   while (ht->elements[index].command != NULL && ht->elements[index].command_name_length != command_name_length && strncasecmp(command_name, ht->elements[index].command_name, command_name_length) != 0) {
//     index = index + 1 % ht->size;
//     if (index == base_index)
//       return (union command_preload_strategies){.ptr = NULL };
//   }
//   *preload_strat = ht->elements[index].preload_func;
//   return ht->elements[index].preload;
// }
