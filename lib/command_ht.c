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
  union command_ht_command_funcs command;
  struct format_string *fstring;
  unsigned int command_name_length;
  bool is_meta;
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
    ht->elements[i].command.ptr = NULL;
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
  unsigned long stridx = 0;
  struct format_string_ll *fstring_node;
  char *typeargs_end;
  struct format_string_ll *prev = NULL;
  struct format_string_ll *head = NULL;
  unsigned int fstring_length = 0;
  bool expect_type;
  long optargs_count_raw;
  unsigned long optargs_count = 0;
  char *optargs_end;
  int jmp_target = -1;
  bool allocate_node;
  bool seen_jump_node = false;
  enum format_string_access_type access_type;
  // First, convert the fstring to a linked list
  while (stridx < len) {
    expect_type = true;
    allocate_node = true;
    switch (str[stridx]) {
      case 'R' : {
        access_type = FSTRING_READONLY;
        stridx += 1;
        break;
      }
      case 'r' : {
        access_type = FSTRING_READONLY_OPTIONAL;
        stridx += 1;
        break;
      }
      case 'C' : {
        access_type = FSTRING_CREATE;
        stridx += 1;
        break;
      }
      case 'W' : {
        access_type = FSTRING_MODIFY_CREATE;
        stridx += 1;
        break;
      }
      case 'M' : {
        access_type = FSTRING_MODIFY_NOCREATE;
        stridx += 1;
        break;
      }
      case 'S' : {
        access_type = FSTRING_STRING;
        stridx += 1;
        expect_type = false;
        break;
      }
      case '|' : {
        if (seen_jump_node) {
          return -6; // Can only handle one jump
        }
        expect_type = false;
        if (jmp_target == -1) {
          allocate_node = false;
          jmp_target = (int)fstring_length;
        } else {
          seen_jump_node = true;
          access_type = FSTRING_JUMP;
        }
        stridx += 1;
        break;
      }
      case '?' : {
        if (seen_jump_node)
          return -7; // Can't have both a jump and optargs
        allocate_node = false;
        optargs_count_raw = strtol(str + stridx + 1, &optargs_end, 10);
        if (optargs_count_raw < 0)
          return -5;
        optargs_count = (unsigned long)optargs_count_raw;
        expect_type = false;
        if (optargs_end != str + stridx)
          return -4; // gotta end at the end
        break;
      }
      default : {
        return -1;
      }
    }
    if (allocate_node) {
      fstring_length++;
      if ((fstring_node = malloc(sizeof(struct format_string_ll))) == NULL)
        return -4;
      if (head == NULL)
        head = fstring_node;
      if (prev != NULL)
        prev->next = fstring_node;
      fstring_node->next = NULL;
      // Yes, it's uninitialized, but in all those cases allocate_node = false
      // so we don't hit this control path anyways
      fstring_node->node.access_type = access_type;
      if (access_type == FSTRING_JUMP) {
        fstring_node->node.details.jump_target = (unsigned int)jmp_target;
      } else if (expect_type) {
        if (str[stridx] != '{')
          return -2;
        stridx += 1;
        typeargs_end = memchr(str + stridx, '}', (unsigned)(len - stridx));
        if (type_ht_fetch(type_ht, str + stridx, (unsigned)(typeargs_end - (str + stridx)), &(fstring_node->node.details.type)) != 0) {
          return -3;
        }
        stridx = (unsigned long)(typeargs_end - str) + 1;
      } else {
        fstring_node->node.details.type = NULL;
      }
      prev = fstring_node;
    }
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
    parsed_fstring[i].details = curr->node.details;
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
  fstring_final->optional_argument_count = optargs_count;
  fstring_final->length = fstring_length;
  fstring_final->contents = parsed_fstring;
  *fstring = fstring_final;
  return 0;
}

int command_ht_store(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, command_func command, const char *fstring_string, const unsigned int fstring_string_length) {
  unsigned int base_index = hash((const unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command.ptr != NULL) {
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
  ht->elements[index].command.normal = command;
  ht->elements[index].is_meta = false;
  ht->elements[index].fstring = fstring;
  return 0;
}

int command_ht_store_meta(struct command_ht *ht, const char *command_name, const unsigned int command_name_length, meta_command_func command) {
  unsigned int base_index = hash((const unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command.ptr != NULL) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return 1;
  }
  ht->elements[index].command_name = command_name;
  ht->elements[index].command_name_length = command_name_length;
  ht->elements[index].command.meta = command;
  ht->elements[index].is_meta = true;
  ht->elements[index].fstring = NULL;
  return 0;
}

int command_ht_fetch(struct command_ht *ht, char *command_name, const unsigned int command_name_length, struct format_string **fstring, union command_ht_command_funcs *command_func, bool *is_meta) {
  unsigned int base_index = hash((unsigned char*)command_name) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].command_name != NULL && (ht->elements[index].command_name_length != command_name_length || strncasecmp(command_name, ht->elements[index].command_name, command_name_length) != 0)) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return 1;
  }
  if (ht->elements[index].command_name == NULL)
    return 2;
  *fstring = ht->elements[index].fstring;
  *command_func = ht->elements[index].command;
  *is_meta = ht->elements[index].is_meta;
  return 0;
}

#include <stdio.h>
void command_ht_print_commands(struct command_ht *ht) {
  for (unsigned int i = 0; i < ht->size; i++) {
    if (ht->elements[i].command.ptr != NULL) {
      printf("Command %.*s\n", ht->elements[i].command_name_length, ht->elements[i].command_name);
    }
  }
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
