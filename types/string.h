struct string;

struct string_args {
  char *str;
  long len;
};

int string_set(struct string **data_loc, const char *str, const long length);
int string_get(struct string *data_loc, const char **str, long *length);
int string_exchange(struct string **data_loc, const char **old_cts, long *old_length, char *new_cts, long new_length);
int string_test(void);
