#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
typedef char* ht_value_type;
#include "hashtable.h"

enum RESIZE_STATUS {
  RESIZE_STATUS_IN_PROGRESS,
  RESIZE_STATUS_STATIC
};

struct ht_node {
  char *key;
  struct ht_node * volatile next;
  unsigned int key_hash;
  ht_value_type value;
};

struct ht_table {
  struct ht_node ** volatile * volatile buckets;
  volatile bool buckets_lock;
  volatile int buckets_semaphor;
  volatile unsigned int element_count;
  volatile unsigned int bitlen;
  volatile enum RESIZE_STATUS resize_status;
};

struct ht_table *ht_init() {
  struct ht_table *table = malloc(sizeof(struct ht_table));
  table->bitlen = 1;
  table->resize_status = RESIZE_STATUS_STATIC;
  table->buckets_lock = false;
  table->buckets_semaphor = 0;
  table->element_count = 0;
  unsigned int table_size = 0x1 << table->bitlen;
  table->buckets = malloc(sizeof(struct ht_node*) * table_size);
  table->buckets_fallback = NULL;
  volatile struct ht_node *node = malloc(sizeof(struct ht_node));
  struct ht_node *end_node = malloc(sizeof(struct ht_node));
  end_node->key = NULL;
  end_node->value = NULL;
  end_node->next = NULL;
  end_node->key_hash = (unsigned int)~0x0;
  for (unsigned int i = 0; i < table_size; i++) {
    node->key = NULL;
    node->key_hash = ~(((unsigned int)~0x0) >> i);
    table->buckets[i] = node;
    if (i != table_size - 1) {
      node->next = malloc(sizeof(struct ht_node));
      node = node->next;
    } else {
      node->next = end_node;
    }
  }
  return table;
}

static unsigned int ht_hash(const char *str) {
  unsigned int hash = 5381;
  char c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + (unsigned int)c; /* hash * 33 + c */
  }
  return hash;
}

static inline unsigned int reverse_bits(unsigned int b) {
  unsigned int r = 0;
  const unsigned int highest_bit_bitmask = ~(((unsigned int)~0x0) >> 1);
  for (int i = 1; i < (int)sizeof(unsigned int) * CHAR_BIT; i++) {
    r = r | (b & highest_bit_bitmask);
    r = r >> 1;
    b = b << 1;
  }
  return r;
}

// static inline unsigned int ht_get_parent(unsigned int idx, unsigned int idepth) {
//   unsigned int mask = ((unsigned int)~0x0) << idepth;
//   // unsigned int omask = mask;
//   while ((idx & mask) != 0) {
//     // omask = mask;
//     mask = mask << 1;
//   }
//   return idx & (mask >> 1);
// }
/*
parent = 0010
offest = 0001
parent = 0001
*/
static inline unsigned int ht_find_msb(unsigned int idx) {
  unsigned int omask = ((unsigned int)~0x0);
  unsigned int mask = omask;
  int shift = 0;
  while ((idx & mask) != 0) {
    shift++;
    mask = mask << 1;
  }
  shift = shift == 0 ? 0 : shift - 1;
  return idx & (omask << shift);
}

static inline struct ht_node *ht_get_index(struct ht_table *table, unsigned int idx) {
  struct ht_node *n = table->buckets[idx];
  struct ht_node **fallback = table->buckets_fallback;
  unsigned int fallback_capacity = 0x1 << (table->bitlen - 1);
  if (n == NULL && fallback != NULL && idx < fallback_capacity) {
    n = fallback[idx];
  }
  return n;
}

static inline struct ht_node *ht_get_prev(struct ht_table *table, unsigned int idx) {
  unsigned int parent = ht_find_msb(idx);
  unsigned int offset = idx & ~parent;
  // if (parent == 1 && offset == 0) {
  //   offset = 1;
  //   parent = 0;
  // }
  unsigned int table_capacity = 0x1 << table->bitlen;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  // printf("Parent: %u (/ %u) -> %u + %u\n", idx, table_capacity, parent, offset);
  while (offset > table_capacity || ht_get_index(table, offset) == NULL) {
    parent = ht_find_msb(offset);
    offset = offset & ~parent;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
  }
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  return ht_get_index(table, offset);
}

static inline void ht_get_bucket_reader_lock(struct ht_table *table) {
  while (true) {
    if (!__atomic_load_n(&table->buckets_lock, __ATOMIC_SEQ_CST)) {
      __atomic_add_fetch(&table->buckets_semaphor, 1, __ATOMIC_SEQ_CST);
      if (!__atomic_load_n(&table->buckets_lock, __ATOMIC_SEQ_CST)) {
        break;
      } else {
        __atomic_add_fetch(&table->buckets_semaphor, -1, __ATOMIC_SEQ_CST);
      }
    }
  }
}

static inline void ht_get_bucket_writer_lock(struct ht_table *table) {
  __atomic_store_n(&table->buckets_lock, true, __ATOMIC_SEQ_CST);
  while (__atomic_load_n(&table->buckets_semaphor, __ATOMIC_SEQ_CST) != 0) {}
}

static inline void ht_release_bucket_writer_lock(struct ht_table *table) {
  __atomic_store_n(&table->buckets_lock, false, __ATOMIC_SEQ_CST);
}

static inline void ht_release_bucket_reader_lock(struct ht_table *table) {
  __atomic_add_fetch(&table->buckets_semaphor, -1, __ATOMIC_SEQ_CST);
}

#include <assert.h>
static inline struct ht_node *ht_get_sentinel(struct ht_table *table, unsigned int key_hash, bool create) {
  ht_get_bucket_reader_lock(table);
  unsigned int idx = reverse_bits(key_hash) & ~(((unsigned int)~0x0) << table->bitlen);
  struct ht_node *nnode;
  struct ht_node *prev;
  struct ht_node *next;
  struct ht_node *snext;
  struct ht_node *snnode;
  struct ht_node *null_node;
  struct ht_node *ret_ptr;
  int foobar1 = 999;
  int foobar2 = 0;
  retry: ;
  foobar2++;
  // unsigned int parent = ht_get_parent(idx, table->bitlen);
  // unsigned int oparent = idx % parent;
  // unsigned int table_capacity = 0x1 << table->bitlen;
  // prev = table->buckets[oparent - parent];
  // while (prev == NULL) {
  //   oparent = parent;
  //   parent = ht_get_parent(parent, table->bitlen);
  //   prev = table->buckets[oparent - parent];
  // }
  prev = ht_get_prev(table, idx);
  assert(prev != NULL);
  if (create && ht_get_index(table, idx) == NULL) {
    nnode = malloc(sizeof(struct ht_node));
    nnode->key = NULL;
    nnode->key_hash = reverse_bits(idx);
    assert(nnode->key_hash != 0);
    // printf("Creating a new sentiel %d (%u)\n", idx, nnode->key_hash);
    next = prev->next;
    while (next->key_hash < nnode->key_hash) {
      prev = next;
      next = __atomic_load_n(&prev->next, __ATOMIC_SEQ_CST);;
    }
    // printf("Inserting %d (%u) between %u (previdx = %u) and %u\n", idx, nnode->key_hash, prev->key_hash, oparent - parent, next->key_hash);
    assert(next != NULL);
    nnode->next = next;
    snext = next;
    if (__atomic_compare_exchange_n(&prev->next, &snext, nnode, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      null_node = NULL;
      if (!(foobar1 = __atomic_compare_exchange_n(&table->buckets[idx], &null_node, nnode, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))) {
        snnode = nnode;
        if (!__atomic_compare_exchange_n(&prev->next, &snnode, next, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
          printf("Well... there's an extra node in the list which kinda sucks :/\n");
        }
        printf("CAS Failed\n");
        free(nnode);
      }
    } else {
      goto retry;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    ret_ptr = ht_get_index(table, idx);
    assert(ret_ptr != NULL);
    ht_release_bucket_reader_lock(table);
    return ret_ptr;
  } else {
    ret_ptr = ht_get_index(table, idx);
    ht_release_bucket_reader_lock(table);
    return ret_ptr == NULL ? prev : ret_ptr;
  }
}

static int ht_resize(struct ht_table *table) {
  enum RESIZE_STATUS rstatus;
  do {
    rstatus = RESIZE_STATUS_STATIC;
  } while (!__atomic_compare_exchange_n(&table->resize_status, &rstatus, RESIZE_STATUS_IN_PROGRESS, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
  unsigned int bitlen = __atomic_load_n(&table->bitlen, __ATOMIC_SEQ_CST);
  unsigned int old_table_size = 0x1 << bitlen;
  unsigned int table_size = 0x1 << (bitlen + 1);
  struct ht_node **new_buckets = malloc(sizeof(struct ht_node*) * table_size);
  memset(new_buckets, 0, sizeof(struct ht_node*) * table_size);
  ht_get_bucket_writer_lock(table);
  __atomic_store_n(&table->buckets_fallback, table->buckets, __ATOMIC_SEQ_CST);
  __atomic_store_n(&table->buckets, new_buckets, __ATOMIC_SEQ_CST);
  // Make sure no writers are touching fallback anymore
  ht_release_bucket_writer_lock(table);
  __atomic_add_fetch(&table->bitlen, 1, __ATOMIC_SEQ_CST);
  struct ht_node *null_node;
  for (unsigned int i = 0; i < old_table_size; i++) {
    null_node = NULL;
    __atomic_compare_exchange_n(&new_buckets[i], &null_node, table->buckets_fallback[i], false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  }
  struct ht_node **old_fallback = table->buckets_fallback;
  ht_get_bucket_writer_lock(table);
  __atomic_store_n(&table->buckets_fallback, NULL, __ATOMIC_SEQ_CST);
  // Make sure no readers are touching fallback anymore
  ht_release_bucket_writer_lock(table);
  __atomic_store_n(&table->resize_status, RESIZE_STATUS_STATIC, __ATOMIC_SEQ_CST);
  free(old_fallback);
  return 0;
}

int ht_store(struct ht_table *table, char *key, ht_value_type value) {
  if (table->element_count > (0x1 << table->bitlen) * 10) {
    printf("Resizing table...\n");
    ht_resize(table);
  }
  unsigned int key_hash = ht_hash(key);
  assert(key_hash != 0);
  struct ht_node *op = ht_get_sentinel(table, key_hash, true);
  retry:;
  struct ht_node *p = op;
  struct ht_node *n = p->next;
  while (key_hash > n->key_hash) {
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && strcmp(key, n->key) == 0) {
      return 1;
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  struct ht_node *new_node = malloc(sizeof(struct ht_node));
  new_node->key = key;
  new_node->value = value;
  new_node->key_hash = key_hash;
  new_node->next = n;
  // printf("INSERT\n%u / %X\n%u / %X\n%u / %X\n", p->key_hash, p->key_hash, new_node->key_hash, new_node->key_hash, n->key_hash, n->key_hash);
  if (!__atomic_compare_exchange_n(&p->next, &n, new_node, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    goto retry;
  }
  __atomic_fetch_add(&table->element_count, 1, __ATOMIC_SEQ_CST);
  return 0;
}

int ht_find(struct ht_table *table, char *key, ht_value_type *value) {
  unsigned int key_hash = ht_hash(key);
  struct ht_node *p = ht_get_sentinel(table, key_hash, false);
  struct ht_node *n = p->next;
  while (key_hash > n->key_hash) {
    // printf("%X > %X\n", key_hash, n->key_hash);
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && strcmp(key, n->key) == 0) {
      *value = n->value;
      return 0;
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  return -1;
}

// static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$%^&*()_+-=[]{}\\|/.,<>?;'\":`~";
//
// static char *random_string() {
//   unsigned int max_len = 32;
//   unsigned int min_len = 5;
//   unsigned int len = ((unsigned int)rand() % (max_len - min_len)) + min_len;
//   char *str = malloc(len + 1);
//   str[len] = '\0';
//   for (unsigned int i = 0; i < len; i++) {
//     str[i] = alpha[(unsigned int)rand() % sizeof(alpha)];
//   }
//   return str;
// }
//
// struct kv_pair {
//   char *k;
//   ht_value_type v;
// };
//
// int main(int argc, char *argv[]) {
//   long trial_count = 100;
//   if (argc >= 2) {
//     trial_count = strtol(argv[1], NULL, 10);
//   }
//   struct ht_table *t = ht_init();
//   struct kv_pair *pairs = malloc(sizeof(struct kv_pair) * (unsigned long)trial_count);
//   char *k;
//   ht_value_type v;
//   bool is_dupe;
//   for (int i = 0; i < trial_count; i++) {
//     do {
//       is_dupe = false;
//       k = random_string();
//       for (int j = 0; j < i; j++) {
//         if (strcmp(pairs[j].k, k) == 0) {
//           is_dupe = true;
//           break;
//         }
//       }
//     } while (is_dupe);
//     v = rand();
//     if (ht_store(t, k, v) != 0) {
//       printf("Store neq 0\n");
//     }
//     pairs[i].k = k;
//     pairs[i].v = v;
//   }
//
//   int rval;
//   int good_count = 0;
//   int error_count = 0;
//   for (int i = 0; i < trial_count * 2; i++) {
//     if (i % 2 == 0) {
//       k = pairs[i/2].k;
//     } else {
//       do {
//         is_dupe = false;
//         k = random_string();
//         for (int j = 0; j < trial_count; j++) {
//           if (strcmp(pairs[j].k, k) == 0) {
//             is_dupe = true;
//             break;
//           }
//         }
//       } while (is_dupe);
//     }
//     rval = ht_find(t, k, &v);
//     if ((i % 2 == 0 && pairs[i / 2].v == v && rval == 0) || (i % 2 == 1 && rval == -1)) {
//       good_count++;
//     } else {
//       error_count++;
//       printf("Error (expected %s)\n", i % 2 == 0 ? "hit" : "miss");
//     }
//   }
//   printf("Good count: %d\nError count: %d\n", good_count, error_count);
//   return 0;
// }

#include "../../predis/tests/parallel-test-template.c"

static inline void *initialize_interface() {
  return NULL;
}

static inline bool run_store(const char *key, char *value, void *_ctx, void *_table) {
  struct ht_table *table = _table;
  int rval = ht_store(table, key, value);
  return (rval == 0);
}

static inline ht_value_type run_fetch(const char *key, void *_ctx, void *_table) {
  struct ht_table *table = _table;
  ht_value_type v;
  int status = ht_find(table, key, &v);
  return v;
}

static inline void *initialize_data() {
  return (void*)ht_init();
}
