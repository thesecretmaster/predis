#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <sys/sysinfo.h>
#include "hashtable.h"

#ifdef HT_TEST_API
#include <stdio.h>
#endif

enum HT_ALLOC_STATUS {
  HT_FREE,
  HT_REALLOC
};

struct ht_value {
  void *value;
  struct type_ht_raw *type;
};

union ht_value_or_status {
  enum HT_ALLOC_STATUS alloc_status;
  struct ht_value *value;
};

// Could be backed better, depending on size of ht_value_type. Honestly
// I'll just eat the 2 bytes per node cuz I don't want to worry about it.
// (ht_value_type will usually be a pointer and therefore 4 bytes)
struct ht_node {
  struct ht_node * volatile next;
  const char *key;
  struct type_ht_raw *type;
  unsigned int key_length;
  unsigned int key_hash;
  union ht_value_or_status contents;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpadded"

// Packed optimally
struct ht_table {
  struct ht_node ** volatile * volatile buckets;
  volatile unsigned int element_count;
  volatile unsigned int bitlen;
  volatile unsigned int bitlen_internal;
};

#pragma GCC diagnostic pop

#ifdef HT_TEST_API

static __thread unsigned long last_allocation_size = 0;

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
  struct ht_node ***buckets = malloc(sizeof(struct ht_node**) * bucket_count);
  if (buckets == NULL) {
    free(table);
    return NULL;
  }
  table->element_count = 0;
  // buckets = [2,2,4,8,16...]
  unsigned int bucket_0_size = 2;
  buckets[0] = malloc(sizeof(struct ht_node*) * bucket_0_size);
  if (buckets[0] == NULL) {
    free(buckets);
    free(table);
    return NULL;
  }
  for (unsigned int i = 1; i < bucket_count; i++) {
    buckets[i] = NULL;
  }
  struct ht_node *node = malloc(sizeof(struct ht_node));
  struct ht_node *end_node = malloc(sizeof(struct ht_node));
  if (node == NULL || end_node == NULL) {
    free(buckets[0]);
    free(buckets);
    free(table);
    free(node);
    return NULL;
  }
  end_node->key = NULL;
  end_node->next = NULL;
  end_node->contents.alloc_status = HT_FREE;
  end_node->key_hash = (unsigned int)~0x0;
  for (unsigned int i = 0; i < bucket_0_size; i++) {
    node->key = NULL;
    node->key_hash = ~(((unsigned int)~0x0) >> i);
    node->contents.alloc_status = HT_FREE;
    buckets[0][i] = node;
    if (i != bucket_0_size - 1) {
      node->next = malloc(sizeof(struct ht_node));
      if (node->next == NULL) {
        i = 0;
        while (i < bucket_0_size && buckets[0][i] != NULL) {
          free(buckets[0][i]);
          free(buckets[0]);
          free(buckets);
          free(end_node);
          free(table);
        }
      }
      node = node->next;
    } else {
      node->next = end_node;
    }
  }
  table->buckets = buckets;
  return table;
}

#ifndef HT_TEST_API
static
#endif
unsigned int ht_hash(const char *str, const unsigned int key_length) {
  unsigned int hash = 5381;
  char c;
  for (unsigned int i = 0; i < key_length; i++) {
    c = *str++;
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

static inline struct ht_node *ht_get_sentinel(struct ht_table *table, unsigned int key_hash, bool create, struct ht_node **new_node) {
  unsigned int idx = ht_reverse_bits(key_hash) & ~((~(unsigned int)0x0) << table->bitlen);
  unsigned int bucket_idx;
  unsigned int parent = ht_find_msb(idx, &bucket_idx);
  unsigned int offset = idx & ~parent;
  unsigned int offset_parent;
  unsigned int offset_offset;
  struct ht_node *prev;
  struct ht_node *next;
  struct ht_node *nnode;
  struct ht_node *snext;
  struct ht_node *null_node;
  if (table->buckets[bucket_idx][offset] != NULL) {
    return table->buckets[bucket_idx][offset];
  }
  if (create && table->buckets[bucket_idx][offset] == NULL) {
    offset_offset = offset;
    do {
      if (offset_offset < 2) {
        offset_parent = 0;
      } else {
        offset_offset = offset_offset & ~ht_find_msb(offset_offset, &offset_parent);
      }
      prev = table->buckets[offset_parent][offset_offset];
    } while (prev == NULL);
    nnode = malloc(sizeof(struct ht_node) * (new_node == NULL ? 1 : 2));
    if (new_node != NULL) {
      *new_node = nnode + 1;
      (*new_node)->contents.alloc_status = HT_REALLOC;
    }
    if (nnode == NULL) {
      #ifdef HT_TEST_API
      if (errno != ENOMEM) {
        printf("uhhhhhhh wtf 3\n");
      }
      last_allocation_size = sizeof(struct ht_node);
      #endif
      return NULL;
    }
    nnode->key = NULL;
    nnode->key_hash = ht_reverse_bits(idx);
    nnode->next = prev;
    null_node = NULL;
    if (__atomic_compare_exchange_n(&table->buckets[bucket_idx][offset], &null_node, nnode, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      assert(prev->key_hash <= nnode->key_hash);
      do {
        next = prev->next;
        while (next->key_hash < nnode->key_hash) {
          prev = next;
          next = __atomic_load_n(&prev->next, __ATOMIC_SEQ_CST);
        }
        nnode->next = next;
        snext = next;
      } while (!__atomic_compare_exchange_n(&prev->next, &snext, nnode, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    } else {
      free(nnode);
    }
    return table->buckets[bucket_idx][offset];
  } else {
    prev = table->buckets[bucket_idx][offset];
    offset_offset = offset;
    while (prev == NULL) {
      offset_offset = offset_offset & ~ht_find_msb(offset_offset, &offset_parent);
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
      #ifdef HT_TEST_API
      if (errno != ENOMEM) {
        printf("uhhhhhhh wtf\n");
      }
      printf("Lalloc size: %lu\n", bucket_size);
      last_allocation_size = sizeof(struct ht_node*) * bucket_size;
      #endif
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
  #ifdef HT_TEST_API
  printf("Current size: %lu\n", bucket_size);
  #endif
  return 0;
}

enum HT_RETURN_STATUS ht_store(struct ht_table *table, const char *key, const unsigned int key_length, void *value, struct type_ht_raw *type) {
  if (table->element_count > (0x1 << table->bitlen)) {
    if (ht_resize(table) != 0) {
      return HT_OOM;
    }
  }
  unsigned int key_hash = ht_hash(key, key_length);
  assert(key_hash != 0);
  struct ht_node *new_node = NULL;
  struct ht_node *op = ht_get_sentinel(table, key_hash, true, &new_node);
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
    if (n->key != NULL && key_length == n->key_length && memcmp(key, n->key, key_length) == 0) {
      if (n->type != type)
        return HT_WRONGTYPE;
      else
        return HT_DUPLICATE_KEY;
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  if (new_node == NULL) {
    new_node = malloc(sizeof(struct ht_node));
    if (new_node == NULL) {
      #ifdef HT_TEST_API
      if (errno != ENOMEM)
        printf("uhhhhhhh wtf 2\n");
      last_allocation_size = sizeof(struct ht_node);
      #endif
      return HT_OOM;
    }
    new_node->contents.alloc_status = HT_FREE;
  }
  char *key_copy = malloc(sizeof(char) * key_length);
  memcpy(key_copy, key, key_length);
  struct ht_value *value_wrap = malloc(sizeof(struct ht_value));
  value_wrap->value = value;
  value_wrap->type = type;
  new_node->key = key_copy;
  new_node->key_length = key_length;
  new_node->type = type;
  new_node->contents.value = value_wrap;
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

enum HT_RETURN_STATUS ht_find(struct ht_table *table, const char *key, const unsigned int key_length, void **value, struct type_ht_raw *type) {
  unsigned int key_hash = ht_hash(key, key_length);
  struct ht_node *p = ht_get_sentinel(table, key_hash, false, NULL);
  struct ht_node *n = p->next;
  struct ht_value *value_wrap;
  while (key_hash > n->key_hash) {
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
    assert(n->key_hash >= p->key_hash);
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && n->key_length == key_length && memcmp(key, n->key, key_length) == 0) {
      value_wrap = n->contents.value;
      if (type != value_wrap->type) {
        return HT_WRONGTYPE;
      }
      *value = value_wrap->value;
      return HT_GOOD;
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  return HT_NOT_FOUND;
}

enum HT_RETURN_STATUS ht_del(struct ht_table *table, const char *key, const unsigned int key_length, void **value) {
  unsigned int key_hash = ht_hash(key, key_length);
  struct ht_node *sentinel = ht_get_sentinel(table, key_hash, false, NULL);
  struct ht_node *p = sentinel;
  struct ht_node *n = p->next;
  retry:
  while (key_hash > n->key_hash) {
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
    assert(n->key_hash >= p->key_hash);
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && n->key_length == key_length && memcmp(key, n->key, key_length) == 0) {
      if (__atomic_compare_exchange_n(&(p->next), &n, n->next, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        *value = n->contents.value;
        // TODO: We can't actually free the node because something else might need it
        // Really we need to add it to the GC list
        // free(n->key);
        // free(n);
        return HT_GOOD;
      } else {
        p = sentinel;
        n = p->next;
        goto retry;
      }
    }
    p = n;
    n = __atomic_load_n(&n->next, __ATOMIC_SEQ_CST);
  }
  return HT_NOT_FOUND;
}
