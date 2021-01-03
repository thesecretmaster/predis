#include "type_ht.h"

struct ht_table;
struct ht_table *ht_init(void);

enum HT_RETURN_STATUS {
  HT_GOOD = 0,
  HT_OOM = 1,
  HT_DUPLICATE_KEY = 2,
  HT_NOT_FOUND = 3,
  HT_WRONGTYPE = 4
};

enum HT_RETURN_STATUS ht_store(struct ht_table *table, const char *key, const unsigned int key_length, void *value, struct type_ht_raw*);
enum HT_RETURN_STATUS ht_find(struct ht_table *table, const char *key, const unsigned int key_length, void **value, struct type_ht_raw*);
enum HT_RETURN_STATUS ht_del(struct ht_table *table, const char *key, const unsigned int key_length, void **value);

#ifdef HT_TEST_API
struct ht_node;
unsigned long ht_last_allocation_size(void);
unsigned int ht_key_hash(struct ht_node*);
struct ht_node *ht_next(struct ht_node*);
struct ht_node *ht_first_node(struct ht_table*);
unsigned int ht_hash(const char *str, const int str_length);
unsigned int ht_reverse_bits(unsigned int b);
unsigned int ht_find_msb(unsigned int idx, unsigned int *base_2_log);
const char *ht_key(struct ht_node *n, unsigned int *key_length);
#endif
