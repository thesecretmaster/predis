#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>
#include "hashtable.h"

struct ht_node {
  char *key;
  struct ht_node *next;
  unsigned int key_hash;
  ht_value_type value;
};

struct ht_table {
  struct ht_node **buckets;
  unsigned int bitlen;
  unsigned int element_count;
};

struct ht_table *ht_init() {
  printf("SIZE: %lu\n", sizeof(struct ht_node));
  struct ht_table *table = malloc(sizeof(struct ht_table));
  table->bitlen = 1;
  table->element_count = 0;
  unsigned int table_size = 0x1 << table->bitlen;
  table->buckets = malloc(sizeof(struct ht_node*) * table_size);
  struct ht_node *node = malloc(sizeof(struct ht_node));
  struct ht_node *end_node = malloc(sizeof(struct ht_node));
  end_node->key = NULL;
  end_node->value = 0;
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
  // printf("B: ");
  // printBits(sizeof(b), &b);
  for (int i = 1; i < (int)sizeof(unsigned int) * CHAR_BIT; i++) {
    r = r | (b & highest_bit_bitmask);
    r = r >> 1;
    b = b << 1;
  }
  // printf("R: ");
  // printBits(sizeof(r), &r);
  return r;
}

static inline unsigned int ht_get_parent(unsigned int idx, unsigned int idepth) {
  unsigned int mask = ((unsigned int)~0x0) << idepth;
  // unsigned int omask = mask;
  while ((idx & mask) != 0) {
    // omask = mask;
    mask = mask << 1;
  }
  return idx & (mask >> 1);
}

static inline struct ht_node *ht_get_sentinel(struct ht_table *table, char *key, bool create) {
  unsigned int idx = reverse_bits(ht_hash(key)) & ~(((unsigned int)~0x0) << table->bitlen);
  struct ht_node *nnode;
  struct ht_node *prev;
  struct ht_node *next;
  unsigned int parent = ht_get_parent(idx, table->bitlen);
  unsigned int oparent = idx;
  prev = table->buckets[oparent - parent];
  while (prev == NULL) {
    oparent = parent;
    parent = ht_get_parent(parent, table->bitlen);
    prev = table->buckets[oparent - parent];
  }
  if (create && table->buckets[idx] == NULL) {
    nnode = malloc(sizeof(struct ht_node));
    nnode->key = NULL;
    nnode->key_hash = reverse_bits((unsigned int)idx);
    // printf("Creating a new sentiel %d (%u)\n", idx, nnode->key_hash);
    next = prev->next;
    while (next->key_hash < nnode->key_hash) {
      prev = next;
      next = prev->next;
    }
    printf("Inserting %d (%u) between %u (previdx = %u) and %u\n", idx, nnode->key_hash, prev->key_hash, oparent - parent, next->key_hash);
    nnode->next = next;
    prev->next = nnode;
    table->buckets[idx] = nnode;
    return table->buckets[idx];
  } else {
    return table->buckets[idx] == NULL ? prev : table->buckets[idx];
  }
}

static int ht_resize(struct ht_table *table) {
  unsigned int old_table_size = 0x1 << table->bitlen;
  unsigned int table_size = 0x1 << (table->bitlen + 1);
  if (old_table_size *2 != table_size)
    printf("wtf r thess lies\n");
  printf("Resized to size %u\n", table_size);
  table->buckets = realloc(table->buckets, sizeof(struct ht_node*) * table_size);
  // memset(&table->buckets[old_table_size], 0, old_table_size);
  table->bitlen = table->bitlen + 1;
  for (unsigned int i = 0; i < old_table_size; i++) {
    table->buckets[i + old_table_size] = NULL;
  }
  return 0;
}

int ht_store(struct ht_table *table, char *key, ht_value_type value) {
  if (table->element_count > (0x1 << table->bitlen) * 2) {
    printf("Resizing table...\n");
    ht_resize(table);
  }
  struct ht_node *p = ht_get_sentinel(table, key, true);
  // printf("Indexed into sentinel %u / %X\n", p->key_hash, p->key_hash);
  unsigned int key_hash = ht_hash(key);
  struct ht_node *n = p->next;
  while (key_hash > n->key_hash) {
    // printf("%X > %X\n", key_hash, n->key_hash);
    p = n;
    n = n->next;
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && strcmp(key, n->key) == 0) {
      return 1;
    }
    p = n;
    n = n->next;
  }
  struct ht_node *new_node = malloc(sizeof(struct ht_node));
  new_node->key = key;
  new_node->value = value;
  new_node->key_hash = key_hash;
  new_node->next = n;
  // printf("INSERT\n%u / %X\n%u / %X\n%u / %X\n", p->key_hash, p->key_hash, new_node->key_hash, new_node->key_hash, n->key_hash, n->key_hash);
  p->next = new_node;
  table->element_count = table->element_count + 1;
  return 0;
}

int ht_find(struct ht_table *table, char *key, ht_value_type *value) {
  struct ht_node *p = ht_get_sentinel(table, key, false);
  unsigned int key_hash = ht_hash(key);
  struct ht_node *n = p->next;
  while (key_hash > n->key_hash) {
    // printf("%X > %X\n", key_hash, n->key_hash);
    p = n;
    n = n->next;
  }
  while (n->key_hash == key_hash) {
    if (n->key != NULL && strcmp(key, n->key) == 0) {
      *value = n->value;
      return 0;
    }
    p = n;
    n = n->next;
  }
  return -1;
}

static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$%^&*()_+-=[]{}\\|/.,<>?;'\":`~";

static char *random_string() {
  unsigned int max_len = 32;
  unsigned int min_len = 5;
  unsigned int len = ((unsigned int)rand() % (max_len - min_len)) + min_len;
  char *str = malloc(len + 1);
  str[len] = '\0';
  for (unsigned int i = 0; i < len; i++) {
    str[i] = alpha[(unsigned int)rand() % sizeof(alpha)];
  }
  return str;
}

struct kv_pair {
  char *k;
  ht_value_type v;
};

int main(int argc, char *argv[]) {
  long trial_count = 100;
  if (argc >= 2) {
    trial_count = strtol(argv[1], NULL, 10);
  }
  struct ht_table *t = ht_init();
  struct kv_pair *pairs = malloc(sizeof(struct kv_pair) * (unsigned long)trial_count);
  char *k;
  ht_value_type v;
  bool is_dupe;
  for (int i = 0; i < trial_count; i++) {
    do {
      is_dupe = false;
      k = random_string();
      for (int j = 0; j < i; j++) {
        if (strcmp(pairs[j].k, k) == 0) {
          is_dupe = true;
          break;
        }
      }
    } while (is_dupe);
    v = rand();
    if (ht_store(t, k, v) != 0) {
      printf("Store neq 0\n");
    }
    pairs[i].k = k;
    pairs[i].v = v;
  }

  int rval;
  int good_count = 0;
  int error_count = 0;
  for (int i = 0; i < trial_count * 2; i++) {
    if (i % 2 == 0) {
      k = pairs[i/2].k;
    } else {
      do {
        is_dupe = false;
        k = random_string();
        for (int j = 0; j < trial_count; j++) {
          if (strcmp(pairs[j].k, k) == 0) {
            is_dupe = true;
            break;
          }
        }
      } while (is_dupe);
    }
    rval = ht_find(t, k, &v);
    if ((i % 2 == 0 && pairs[i / 2].v == v && rval == 0) || (i % 2 == 1 && rval == -1)) {
      good_count++;
    } else {
      error_count++;
      printf("Error (expected %s)\n", i % 2 == 0 ? "hit" : "miss");
    }
  }
  printf("Good count: %d\nError count: %d\n", good_count, error_count);
  return 0;
}
