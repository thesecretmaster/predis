// typedef int ht_value_type;
struct ht_table;
struct ht_table *ht_init(void);

enum HT_RETURN_STATUS {
  HT_GOOD = 0,
  HT_OOM = 1,
  HT_DUPLICATE_KEY = 2,
  HT_NOT_FOUND = 3
};

#ifndef HT_VALUE_TYPE
#error "No ht_value_type specified when compiling hashtable"
#endif
typedef HT_VALUE_TYPE ht_value_type;

enum HT_RETURN_STATUS ht_store(struct ht_table *table, const char *key, ht_value_type value);
enum HT_RETURN_STATUS ht_find(struct ht_table *table, const char *key, ht_value_type *value);

#ifdef HT_TEST_API
struct ht_node;
unsigned long ht_last_allocation_size(void);
unsigned int ht_key_hash(struct ht_node*);
struct ht_node *ht_next(struct ht_node*);
struct ht_node *ht_first_node(struct ht_table*);
unsigned int ht_hash(const char *str);
unsigned int ht_reverse_bits(unsigned int b);
unsigned int ht_find_msb(unsigned int idx, unsigned int *base_2_log);
const char *ht_key(struct ht_node *n);
#endif
