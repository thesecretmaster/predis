#include "../lib/hashtable.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../predis/tests/parallel-test-template.c"

static inline void *initialize_interface() {
  return NULL;
}

static inline bool run_store(const char *key, char *value, void *_ctx, void *_table) {
  struct ht_table *table = _table;
  int rval = ht_store(table, key, value);
  struct sysinfo info;
  if (rval == HT_OOM) {
    if (sysinfo(&info) == 0) {
      printf("%lu ram and %lu swap free units (%u)\n", info.freeram, info.freeswap, info.mem_unit);
    }
    printf("%lu OOM\n", ht_last_allocation_size());
  }
  return (rval == HT_GOOD);
}

static inline ht_value_type run_fetch(const char *key, void *_ctx, void *_table) {
  struct ht_table *table = _table;
  ht_value_type v = NULL;
  int status = ht_find(table, key, &v);
  if (status != HT_GOOD) {
    return NULL;
  }
  return v;
}

static inline void *initialize_data() {
  return (void*)ht_init();
}
