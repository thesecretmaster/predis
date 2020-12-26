#include "../commands.h"
#include "string.h"

static const char string_type_name[] = "string";

static int initialize_string(void **str_ptr_v) {
  struct string **str_ptr = malloc(sizeof(struct string*));
  struct string *str = malloc(sizeof(struct string));
  str->length = -1;
  str->data = NULL;
  *str_ptr = str;
  *str_ptr_v = str_ptr;
  return 0;
}

static int free_string(void *_str) {
  struct string **str = _str;
  free((*str)->data);
  free(*str);
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
