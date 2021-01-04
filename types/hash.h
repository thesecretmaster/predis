struct hash;
int hash_store(struct hash *table, const char *key, const unsigned int key_length, void *value);
int hash_find(struct hash *table, const char *key, const unsigned int key_length, void **value);
int hash_del(struct hash *table, const char *key, const unsigned int key_length, void **value);
