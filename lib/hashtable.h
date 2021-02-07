struct ht_table;

typedef void (*ht_free_func)(void*);

struct ht_table *ht_init(void);
void ht_free(struct ht_table *table, ht_free_func ff);

enum HT_RETURN_STATUS {
  HT_GOOD = 0,
  HT_OOM = 1,
  HT_DUPLICATE_KEY = 2,
  HT_NOT_FOUND = 3,
  HT_BADARGS = 4
};

/*
For both ht_find and ht_store, the `value` is a pointer to a part of the
hashtable node. So:
         ht node
         - stuff
value -> - void *value -> actual value (struct string or smth)
         - more stuff

HOWEVER for ht_del (void*)value -> actual value
*/
enum HT_RETURN_STATUS ht_store(struct ht_table *table, const char *key, const unsigned int key_length, void **value);
enum HT_RETURN_STATUS ht_find(struct ht_table *table, const char *key, const unsigned int key_length, void **value);
enum HT_RETURN_STATUS ht_del(struct ht_table *table, const char *key, const unsigned int key_length, void **value);

#ifdef HT_ITERABLE
struct ht_iterable;
struct ht_iterable *ht_head(struct ht_table *table);
struct ht_iterable *ht_next(struct ht_iterable *i);
void *ht_value(struct ht_iterable *i);
#endif

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
