#include "../lib/hashtable.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890!@#$%^&*()_+-=[]{}\\|/.,<>?;'\":`~";

static char *random_string() {
  unsigned int max_len = 32;
  unsigned int min_len = 5;
  unsigned int len = ((unsigned int)rand() % (max_len - min_len)) + min_len;
  char *str = malloc(len + 1);
  if (str == NULL)
    return NULL;
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

#include <time.h>

int main(int argc, char *argv[]) {
  long trial_count = 100;
  if (argc >= 2) {
    trial_count = strtol(argv[1], NULL, 10);
  }
  srand((unsigned int)time(0));
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
    v = (unsigned int)rand();
    if (ht_store(t, k, v) != 0) {
      printf("Store neq 0\n");
    }
    pairs[i].k = k;
    pairs[i].v = v;
  }

  int rval;
  int good_count = 0;
  int error_count = 0;
  unsigned int key_hash;
  struct ht_node *node;
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
    if ((i % 2 == 0 && pairs[i / 2].v == v && rval == 0) || (i % 2 == 1 && rval == HT_NOT_FOUND)) {
      good_count++;
    } else {
      error_count++;
      printf("Error (expected %s)\n", i % 2 == 0 ? "hit" : "miss");
      if (i % 2 == 0) {
        key_hash = ht_hash(k);
        node = ht_first_node(t);
        while (node != NULL && ht_key_hash(node) != key_hash) {
          node = ht_next(node);
        }
        if (node == NULL) {
          printf("Sadsiah :'(\n");
        }
      }
    }
  }
  printf("Good count: %d\nError count: %d\n", good_count, error_count);
  struct ht_node *p = ht_first_node(t);
  unsigned int offset;
  unsigned int parent;
  unsigned int rbits;
  struct ht_node *n = ht_next(p);
  while (n != NULL) {
    assert(ht_key_hash(p) <= ht_key_hash(n));
    // printf("%X", p->key_hash);
    if (ht_key(p) == NULL) {
      // printf(" (S)");
      rbits = ht_reverse_bits(ht_key_hash(p));
      offset = rbits & ~ht_find_msb(rbits, &parent);
      // printf("%*s%X is %X (%u / %u)\n", parent * 2, "", p->key_hash, t->buckets[parent][offset] == NULL ? 0x0 : t->buckets[parent][offset]->key_hash, parent, offset);
      // assert(p == t->buckets[parent][offset]);
    }
    // printf("\n");
    p = n;
    n = ht_next(n);
  }
  return 0;
}
