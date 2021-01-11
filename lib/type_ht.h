#ifndef LIB_TYPE_HT
#define LIB_TYPE_HT

#include <stdbool.h>

struct type_ht;

typedef int (*type_init_func)(void**);
typedef int (*type_free_func)(void*);

struct type_ht_raw {
  type_init_func init;
  type_free_func free;
};

struct type_ht *type_ht_init(unsigned int size);

int type_ht_store(struct type_ht *ht, const char *type_name, const unsigned int type_name_length, const struct type_ht_raw*);

int type_ht_fetch(struct type_ht *ht, const char *type_name, const unsigned int type_name_length, struct type_ht_raw**);

#endif
