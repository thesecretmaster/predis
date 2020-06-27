#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../commands.h"

struct string {
  char *data;
};

static const char *type_name = "string";

static int string_set(struct predis_ctx *ctx, struct predis_data **data, char **argv, int argc) {
  if (argc != 2)
    return WRONG_ARG_COUNT;

  __atomic_store_n(&data[0]->data, argv[1], __ATOMIC_SEQ_CST);
  __atomic_store_n(&data[0]->data_type, type_name, __ATOMIC_SEQ_CST);
  return PREDIS_SUCCESS;
}

static int string_get(struct predis_ctx *ctx, struct predis_data **data, char **argv, int argc) {
  if (argc != 1)
    return WRONG_ARG_COUNT;

  if (strcmp(data[0]->data_type, "string") != 0)
    return INVALID_TYPE;

  replySimpleString(ctx, __atomic_load_n(&data[0]->data, __ATOMIC_SEQ_CST));
  return PREDIS_SUCCESS;
}

static const char sset[] = "SET";
static const char sget[] = "GET";


/*
a|foobar|b foobar is looped
w = write, existance optional
W = write, existance mandatory
r = read, existance optional
R = read, existance mandatory
c = write, non-existance mandatory
*/
int predis_init(void *magic_obj) {
  register_command(magic_obj, sset, &string_set, "cs");
  register_command(magic_obj, sget, &string_get, "R");
  return 0;
}

/*
Structure ideas:

Preload allows a) store prelim data and b) store a list of keys
Command then getsd a) prelim data and b) a list of actual key pointers

Both preload and command get argc and argv cuz why not

(what happens if a key is deleted in the middle? possible to terminate? what
about invalid keys?)

Commands can also request additional keys but this is challenging because they
can't really get exclusive access since they haven't been scheduled for that key



*/
