#include <stdio.h>
#include "hash.h"
#include "../public/types.h"
#include "../lib/hashtable.h"

static const char hash_type_name[] = "hash";

static int initialize_hash(void **ht_ptr, void *_hargs) {
  if (_hargs != NULL)
    printf("YOU CANT PASS ARGS TO A HT SORRY\n");
  struct ht_table *ht = ht_init();
  *ht_ptr = ht;
  return 0;
}

static int free_hash(void *_ht) {
  struct ht_table *ht = _ht;
  ht_free(ht, NULL);
  return 0;
}

int hash_store(struct hash *table, const char *key, const unsigned int key_length, char *value) {
  void *value_dup = value;
  return ht_store((struct ht_table*)table, key, key_length, &value_dup);
}

int hash_find(struct hash *table, const char *key, const unsigned int key_length, char **value) {
  void *ht_value;
  switch (ht_find((struct ht_table*)table, key, key_length, &ht_value)) {
    case HT_GOOD:
    case HT_DUPLICATE_KEY: {
      *value = *((char**)ht_value);
      return 0;
    }
    case HT_OOM:
    case HT_BADARGS: {
      return -2;
    }
    case HT_NOT_FOUND: {
      return -1;
    }
  }
}

int hash_del(struct hash *table, const char *key, const unsigned int key_length, char **value) {
  return ht_del((struct ht_table*)table, key, key_length, (void**)value);
}

int predis_init(void *magic_obj) {
  register_type(magic_obj, hash_type_name, sizeof(hash_type_name) - 1, &initialize_hash, &free_hash);
  return 0;
}
