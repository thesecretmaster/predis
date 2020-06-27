#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <sys/sysinfo.h>
#include "hashtable.h"

static __thread unsigned long last_allocation_size = 0;

enum RESIZE_STATUS {
  RESIZE_STATUS_IN_PROGRESS,
  RESIZE_STATUS_STATIC
};

struct ht_node {
  struct ht_node * volatile next;
  const char *key;
  unsigned int key_hash;
  ht_value_type value;
};

struct ht_table {
  struct ht_node ** volatile * volatile buckets;
  volatile unsigned int element_count;
  volatile unsigned int bitlen;
  volatile unsigned int bitlen_internal;
};

#ifdef HT_TEST_API

unsigned long ht_last_allocation_size() {
  return last_allocation_size;
}

unsigned int ht_key_hash(struct ht_node *n) {
  return n->key_hash;
}

struct ht_node *ht_next(struct ht_node *n) {
  return n->next;
}

struct ht_node *ht_first_node(struct ht_table *n) {
  return n->buckets[0][0];
}

const char *ht_key(struct ht_node *n) {
  return n->key;
}
#endif

struct ht_table *ht_init() {
  struct ht_table *table = malloc(sizeof(struct ht_table));
  if (table == NULL)
    return NULL;
  table->bitlen = 1;
  table->bitlen_internal = table->bitlen;
  unsigned int bucket_count = CHAR_BIT * sizeof(struct ht_node*);
  table->buckets = malloc(sizeof(struct ht_node**) * bucket_count);
  if (table->buckets == NULL) {
    free(table);
    return NULL;
  }
  table->element_count = 0;
  // buckets = [2,2,4,8,16...]
  unsigned int bucket_0_size = 2;
  table->buckets[0] = malloc(sizeof(struct ht_node*) * bucket_0_size);
  if (table->buckets[0] == NULL) {
    free(table->buckets);
    free(table);
    return NULL;
  }
  for (unsigned int i = 1; i < bucket_count; i++) {
    table->buckets[i] = NULL;
  }
  struct ht_node *node = malloc(sizeof(struct ht_node));
  struct ht_node *end_node = malloc(sizeof(struct ht_node));
  if (node == NULL || end_node == NULL) {
    free(table->buckets[0]);
    free(table->buckets);
    free(table);
    free(node);
    return NULL;
  }
  end_node->key = NULL;
  end_node->next = NULL;
  end_node->key_hash = (unsigned int)~0x0;
  for (unsigned int i = 0; i < bucket_0_size; i++) {
    node->key = NULL;
    node->key_hash = ~(((unsigned int)~0x0) >> i);
    table->buckets[0][i] = node;
    if (i != bucket_0_size - 1) {
      node->next = malloc(sizeof(struct ht_node));
      if (node->next == NULL) {
        i = 0;
        while (i < bucket_0_size && table->buckets[0][i] != NULL) {
          free(table->buckets[0][i]);
          free(table->buckets[0]);
          free(table->buckets);
          free(end_node);
          free(table);
        }
      }
      node = node->next;
    } else {
      node->next = end_node;
    }
  }
  return table;
}

#ifndef HT_TEST_API
static
#endif
unsigned int ht_hash(const char *str) {
  unsigned int hash = 5381;
  char c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + (unsigned int)c; /* hash * 33 + c */
  }
  return hash;
}

#ifndef HT_TEST_API
static inline
#endif
unsigned int ht_reverse_bits(unsigned int b) {
  unsigned int r = 0;
  for (unsigned int i = 0; i < sizeof(unsigned int) * CHAR_BIT; i++) {
    r = r | (b & 0x1);
    if (i + 1 != sizeof(unsigned int) * CHAR_BIT)
      r = r << 1;
    b = b >> 1;
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
#ifndef HT_TEST_API
static inline
#endif
unsigned int ht_find_msb(unsigned int idx, unsigned int *base_2_log) {
  unsigned int omask = ((unsigned int)~0x0);
  unsigned int mask = omask;
  unsigned int shift = 0;
  while ((idx & mask) != 0) {
    shift++;
    mask = mask << 1;
  }
  shift = shift == 0 ? 0 : shift - 1;
  if (base_2_log != NULL)
    *base_2_log = shift;
  return idx & (omask << shift);
}

// static inline struct ht_node *ht_get_index(struct ht_table *table, unsigned int idx) {
//   struct ht_node *n = table->buckets[idx];
//   struct ht_node **fallback = table->buckets_fallback;
//   unsigned int fallback_capacity = 0x1 << (table->bitlen - 1);
//   if (n == NULL && fallback != NULL && idx < fallback_capacity) {
//     n = fallback[idx];
//   }
//   return n;
// }
//
// static inline struct ht_node *ht_get_prev(struct ht_table *table, unsigned int idx) {
//   unsigned int parent = ht_find_msb(idx);
//   unsigned int offset = idx & ~parent;
//   // if (parent == 1 && offset == 0) {
//   //   offset = 1;
//   //   parent = 0;
//   // }
//   unsigned int table_capacity = 0x1 << table->bitlen;
//   __atomic_thread_fence(__ATOMIC_SEQ_CST);
//   // printf("Parent: %u (/ %u) -> %u + %u\n", idx, table_capacity, parent, offset);
//   while (offset > table_capacity || ht_get_index(table, offset) == NULL) {
//     parent = ht_find_msb(offset);
//     offset = offset & ~parent;
//     __atomic_thread_fence(__ATOMIC_SEQ_CST);
//   }
//   __atomic_thread_fence(__ATOMIC_SEQ_CST);
//   return ht_get_index(table, offset);
// }
//
// static inline void ht_get_bucket_reader_lock(struct ht_table *table) {
//   while (true) {
//     if (!__atomic_load_n(&table->buckets_lock, __ATOMIC_SEQ_CST)) {
//       __atomic_add_fetch(&table->buckets_semaphor, 1, __ATOMIC_SEQ_CST);
//       if (!__atomic_load_n(&table->buckets_lock, __ATOMIC_SEQ_CST)) {
//         break;
//       } else {
//         __atomic_add_fetch(&table->buckets_semaphor, -1, __ATOMIC_SEQ_CST);
//       }
//     }
//   }
// }
//
// static inline void ht_get_bucket_writer_lock(struct ht_table *table) {
//   __atomic_store_n(&table->buckets_lock, true, __ATOMIC_SEQ_CST);
//   while (__atomic_load_n(&table->buckets_semaphor, __ATOMIC_SEQ_CST) != 0) {}
// }
//
// static inline void ht_release_bucket_writer_lock(struct ht_table *table) {
//   __atomic_store_n(&table->buckets_lock, false, __ATOMIC_SEQ_CST);
// }
//
// static inline void ht_release_bucket_reader_lock(struct ht_table *table) {
//   __atomic_add_fetch(&table->buckets_semaphor, -1, __ATOMIC_SEQ_CST);
// }

static inline struct ht_node *ht_get_sentinel(struct ht_table *table, unsigned int key_hash, bool create) {
  // ht_get_bucket_reader_lock(table);
  unsigned int idx = ht_reverse_bits(key_hash) & ~((~(unsigned int)0x0) << table->bitlen);
  unsigned int bucket_idx;
  unsigned int test = idx;
  unsigned int parent = ht_find_msb(idx, &bucket_idx);
  unsigned int offset = idx & ~parent;
  idx = test;
  unsigned int offset_parent;
  unsigned int offset_offset;
  // printf("Parent: %X / %u\nIdx: %X / %u\nOffset: %X / %u\n", parent, parent, idx, idx, offset, offset);
  struct ht_node *prev;
  struct ht_node *next;
  struct ht_node *nnode;
  struct ht_node *snext;
  struct ht_node *null_node;
  unsigned int foobar1;
  unsigned int foobar2;
  int iters;
  int steps_back = 0;
  int sentinels;
  // printf("Reading bucket %u (%p) at offset %u\n", parent, table->buckets[parent], offset);
  if (table->buckets[bucket_idx][offset] != NULL) {
    return table->buckets[bucket_idx][offset];
  }
  if (create && table->buckets[bucket_idx][offset] == NULL) {
    // printf("Created %X %X at buk %u/%u\n", idx, reverse_bits(idx), bucket_idx, offset);
    offset_offset = offset;
    // printf("Creating %u\n", (0x1 << bucket_idx) + offset_offset);
    do {
      // printf("Move status: %u / %u\n", offset_parent, offset_offset);
      steps_back++;
      foobar1 = offset_offset;
      if (offset_offset < 2) {
        offset_parent = 0;
      } else {
        offset_offset = offset_offset & ~(foobar2 = ht_find_msb(offset_offset, &offset_parent));
        if (foobar1 >= 2)
          assert(foobar2 == 0x1 << offset_parent);
        assert(foobar2 + offset_offset == foobar1);
      }
      prev = table->buckets[offset_parent][offset_offset];
      // printf("Trying %u (%u / %u) %s\n", foobar1, offset_parent, offset_offset, prev == NULL ? "it does not exist" : "it does exist");
    } while (prev == NULL);
    nnode = malloc(sizeof(struct ht_node));
    if (nnode == NULL) {
      if (errno != ENOMEM) {
        printf("uhhhhhhh wtf 3\n");
      }
      last_allocation_size = sizeof(struct ht_node);
      return NULL;
    }
    nnode->key = NULL;
    nnode->key_hash = ht_reverse_bits(idx);
    nnode->next = prev;
    null_node = NULL;
    if (__atomic_compare_exchange_n(&table->buckets[bucket_idx][offset], &null_node, nnode, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      // printf("Old: %u / %u\nNew: %u / %u\nPKH: %u\nKH:  %u\n", foobar1, foobar2, bucket_idx, offset, prev->key_hash, nnode->key_hash);
      assert(prev->key_hash <= nnode->key_hash);
      do {
        iters = 0;
        sentinels = 0;
        // printf("Creating a new sentiel %d (%u)\n", idx, nnode->key_hash);
        next = prev->next;
        // printf("Starting at %u\n", reverse_bits(prev->key_hash));
        while (next->key_hash < nnode->key_hash) {
          prev = next;
          iters++;
          if (prev->key == NULL) {
            // printf("Stepping through sentinel %u\n", reverse_bits(prev->key_hash));
            // assert(reverse_bits(prev->key_hash) > nnode->key_hash);
            sentinels++;
          }
          next = __atomic_load_n(&prev->next, __ATOMIC_SEQ_CST);
        }
        // printf("Reached %u\n", reverse_bits(nnode->key_hash));
        // printf("%d iters (%d sentinels) (%d steps)\n", iters, sentinels, steps_back);
        // printf("Inserting %d (%u) between %u (previdx = %u) and %u\n", idx, nnode->key_hash, prev->key_hash, oparent - parent, next->key_hash);
        assert(next != NULL);
        nnode->next = next;
        snext = next;
        assert(prev->key_hash <= nnode->key_hash);
        assert(nnode->key_hash <= next->key_hash);
      } while (!__atomic_compare_exchange_n(&prev->next, &snext, nnode, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    } else {
      free(nnode);
    }
    return table->buckets[bucket_idx][offset];
  } else {
    prev = table->buckets[bucket_idx][offset];
    offset_offset = offset;
    while (prev == NULL) {
      foobar1 = offset_offset;
      offset_offset = offset_offset & ~(foobar2 = ht_find_msb(offset_offset, &offset_parent));
      if (foobar1 >= 2)
      assert(foobar2 == 0x1 << offset_parent);
      assert(foobar2 + offset_offset == foobar1);
      prev = table->buckets[offset_parent][offset_offset];
    }
    return prev;
  }
}

static int ht_resize(struct ht_table *table) {
  unsigned int bitlen;
  unsigned int sbitlen;
  unsigned long bucket_size;
  // printf("Resizing to %u, bucket_size: %u....\n", bitlen, bucket_size);
  struct ht_node **new_buckets = NULL;
  do {
    free(new_buckets);
    bitlen = __atomic_load_n(&table->bitlen_internal, __ATOMIC_SEQ_CST) + 1;
    bucket_size = (unsigned long)0x1 << (bitlen - 1);
    new_buckets = malloc(sizeof(struct ht_node*) * bucket_size);
    if (new_buckets == NULL) {
      if (errno != ENOMEM) {
        printf("uhhhhhhh wtf\n");
      }
      printf("Lalloc size: %lu\n", bucket_size);
      last_allocation_size = sizeof(struct ht_node*) * bucket_size;
      return -1;
    }
    sbitlen = bitlen - 1;
  } while (!__atomic_compare_exchange_n(&table->bitlen_internal, &sbitlen, bitlen, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
  memset(new_buckets, 0, sizeof(struct ht_node*) * bucket_size);
  __atomic_store_n(&table->buckets[bitlen - 1], new_buckets, __ATOMIC_SEQ_CST);
  unsigned int old_bitlen;
  do {
    old_bitlen = bitlen - 1;
  } while (!__atomic_compare_exchange_n(&table->bitlen, &old_bitlen, bitlen, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
  printf("Current size: %lu\n", bucket_size);
  return 0;
}

enum HT_RETURN_STATUS ht_store(struct ht_table *table, const char *key, ht_value_type value) {
  if (table->element_count > (0x1 << table->bitlen)) {
    if (ht_resize(table) != 0) {
      return HT_OOM;
    }
  }
  unsigned int key_hash = ht_hash(key);
  assert(key_hash != 0);
  struct ht_node *op = ht_get_sentinel(table, key_hash, true);
  if (op == NULL)
    return HT_OOM;
  retry:;
  struct ht_node *p = op;
  struct ht_node *n = p->next;
  while (key_hash > n->key_hash) {
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && strcmp(key, n->key) == 0) {
      return HT_DUPLICATE_KEY;
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  struct ht_node *new_node = malloc(sizeof(struct ht_node));
  if (new_node == NULL) {
    if (errno != ENOMEM) {
      printf("uhhhhhhh wtf 2\n");
    }
    last_allocation_size = sizeof(struct ht_node);
    return HT_OOM;
  }
  new_node->key = key;
  new_node->value = value;
  new_node->key_hash = key_hash;
  new_node->next = n;
  assert(p->key_hash <= key_hash);
  assert(key_hash <= n->key_hash);
  // printf("INSERT\n%u / %X\n%u / %X\n%u / %X\n", p->key_hash, p->key_hash, new_node->key_hash, new_node->key_hash, n->key_hash, n->key_hash);
  if (!__atomic_compare_exchange_n(&p->next, &n, new_node, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    goto retry;
  }
  __atomic_fetch_add(&table->element_count, 1, __ATOMIC_SEQ_CST);
  return HT_GOOD;
}

enum HT_RETURN_STATUS ht_find(struct ht_table *table, const char *key, ht_value_type *value) {
  unsigned int key_hash = ht_hash(key);
  struct ht_node *p = ht_get_sentinel(table, key_hash, false);
  struct ht_node *n = p->next;
  while (key_hash > n->key_hash) {
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
    assert(n->key_hash >= p->key_hash);
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && strcmp(key, n->key) == 0) {
      *value = n->value;
      return HT_GOOD;
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  return HT_NOT_FOUND;
}
