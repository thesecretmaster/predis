const char string_type_name[] = "string";

struct string {
  long length;
  char *cts;
};

void *initialize_string() {
  struct string *str = malloc(sizeof(struct string));
  str->length = -1;
  str->cts = NULL;
  return str;
}

void free_string(void *str) {
  free(str);
}

const struct predis_type string_type = {
  .initialize = &initialize_string,
  .free = &free_string
}

int predis_init_type(void *magic_obj) {
  register_type(magic_obj, string_type_name, sizeof(string_type_name), &string_type, "cs");
  return 0;
}
