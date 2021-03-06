#include <stdlib.h>
#include "../public/types.h"
#include "string.h"

struct string {
  long length;
  const char *data;
};

static const char string_type_name[] = "string";

static int initialize_string(void **str_ptr, void *_str_args) {
  struct string *str = malloc(sizeof(struct string));
  struct string_args *str_args = _str_args;
  if (str_args == NULL) {
    str->length = -1;
    str->data = NULL;
  } else {
    str->length = str_args->len;
    str->data = str_args->str;
  }
  __atomic_store_n(str_ptr, str, __ATOMIC_SEQ_CST);
  return 0;
}

static int free_string(void *_str) {
  struct string *str = _str;
  free(str->data);
  free(str);
  return 0;
}

int string_set(struct string **data_loc, const char *str_raw, const long length) {
  struct string *str = malloc(sizeof(struct string));
  if (str == NULL)
    return -1;
  str->data = str_raw;
  str->length = length;
  __atomic_store_n(data_loc, str, __ATOMIC_SEQ_CST);
  return 0;
}

int string_get(struct string *data_loc, const char **str_raw, long *length) {
  struct string *str = __atomic_load_n(&data_loc, __ATOMIC_SEQ_CST);
  *str_raw = str->data;
  *length = str->length;
  return 0;
}

int string_exchange(struct string **data_loc, const char **old_cts, long *old_length, char *new_cts, long new_length) {
  struct string *str = malloc(sizeof(struct string));
  if (str == NULL)
    return -1;
  str->data = new_cts;
  str->length = new_length;
  struct string *old_str;
  __atomic_exchange(data_loc, &str, &old_str, __ATOMIC_SEQ_CST);
  *old_cts = old_str->data;
  *old_length = old_str->length;
  return 0;
}

// static const struct predis_type string_type = {
//   .init = &initialize_string,
//   .free = &free_string
// };

int predis_init(void *magic_obj) {
  register_type(magic_obj, string_type_name, sizeof(string_type_name) - 1, &initialize_string, &free_string);
  return 0;
}
