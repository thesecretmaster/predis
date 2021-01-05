#include <stdio.h>
#include "hash.h"
#include "../commands.h"
#define TYPE_HASH
#include "../lib/hashtable.h"
#undef TYPE_HASH

static const char hash_type_name[] = "hash";

static int initialize_hash(void **ht_ptr) {
  struct ht_table *ht = ht_init();
  *ht_ptr = ht;
  return 0;
}

static int free_hash(void *_ht) {
  printf("lol can't free hashes\n");
  return 0;
}

int hash_store(struct hash *table, const char *key, const unsigned int key_length, char *value) {
  void **tmp_val = (void**)&value;
  return ht_store((struct ht_table*)table, key, key_length, tmp_val, true);
}

int hash_find(struct hash *table, const char *key, const unsigned int key_length, char **value) {
  return ht_find((struct ht_table*)table, key, key_length, (void**)value);
}

int hash_del(struct hash *table, const char *key, const unsigned int key_length, char **value) {
  return ht_del((struct ht_table*)table, key, key_length, (void**)value);
}

int predis_init(void *magic_obj) {
  register_type(magic_obj, hash_type_name, sizeof(hash_type_name) - 1, &initialize_hash, &free_hash);
  return 0;
}
