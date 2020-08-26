#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <hiredis/hiredis.h>
#include "../../predis/tests/parallel-test-template.c"

static inline void *initialize_interface() {
  redisContext *ctx = redisConnect("localhost", 8000);
  return (void*)ctx;
}

static inline bool run_store(const char *key, char *value, void *_ctx, void *_table) {
  redisContext *ctx = _ctx;
  redisReply *rep;
  rep = redisCommand(ctx, "SET %s %s", key, value);
  return (rep != NULL && rep->str != NULL && strcmp(rep->str, "OK") == 0);
}

static inline char *run_fetch(const char *key, void *_ctx, void *_table) {
  redisContext *ctx = _ctx;
  redisReply *rep;
  rep = redisCommand(ctx, "GET %s", key);
  return rep == NULL ? NULL : rep->str;
}

static inline void *initialize_data() {
  return NULL;
}
