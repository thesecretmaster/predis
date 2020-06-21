typedef int ht_value_type;
struct ht_table;
struct ht_table *ht_init(void);
int ht_store(struct ht_table *table, char *key, ht_value_type value);
int ht_find(struct ht_table *table, char *key, ht_value_type *value);
int ht_find(struct ht_table *table, char *key, ht_value_type *value);
