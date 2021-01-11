#include "type_ht.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

struct type_ht_elem {
  const char *type_name;
  struct type_ht_raw type_raw;
  unsigned int type_name_length;
};

struct type_ht {
  struct type_ht_elem *elements;
  unsigned int size;
};

#pragma GCC diagnostic pop

struct type_ht *type_ht_init(unsigned int size) {
  struct type_ht *ht = malloc(sizeof(struct type_ht));
  if (ht == NULL)
    return NULL;
  ht->size = size;
  ht->elements = malloc(sizeof(struct type_ht_elem) * size);
  if (ht->elements == NULL) {
    free(ht);
    return NULL;
  }
  for (unsigned int i = 0; i < size; i++) {
    ht->elements[i].type_name = NULL;
    ht->elements[i].type_name_length = 0;
    ht->elements[i].type_raw.init = NULL;
    ht->elements[i].type_raw.free = NULL;
  }
  return ht;
}

// https://stackoverflow.com/a/7666577/4948732
// Slightly modified to take length argument.
static unsigned long
hash(const char *str, unsigned int len)
{
    unsigned long hash = 5381;
    int c;

    while (len > 0) {
        c = *str;
        str++;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdisabled-macro-expansion"
        hash = ((hash << 5) + hash) + (unsigned)tolower(c); /* hash * 33 + c */
#pragma GCC diagnostic pop
        len -= 1;
    }

    return hash;
}

int type_ht_store(struct type_ht *ht, const char *type_name, const unsigned int type_name_length, const struct type_ht_raw *type_raw) {
  if (type_raw == NULL)
    return -1;
  unsigned int base_index = hash(type_name, type_name_length) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].type_name != NULL) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return 1;
  }
  ht->elements[index].type_name = type_name;
  ht->elements[index].type_name_length = type_name_length;
  ht->elements[index].type_raw.init = type_raw->init;
  ht->elements[index].type_raw.free = type_raw->free;
  return 0;
}

int type_ht_fetch(struct type_ht *ht, const char *type_name, unsigned int type_name_length, struct type_ht_raw **type_raw) {
  unsigned int base_index = hash(type_name, type_name_length) % ht->size;
  unsigned int index = base_index;
  while (ht->elements[index].type_name != NULL && (ht->elements[index].type_name_length != type_name_length || strncasecmp(type_name, ht->elements[index].type_name, type_name_length) != 0)) {
    index = index + 1 % ht->size;
    if (index == base_index)
      return -1;
  }
  if (ht->elements[index].type_name == NULL)
    return -1;
  *type_raw = &ht->elements[index].type_raw;
  return 0;
}
