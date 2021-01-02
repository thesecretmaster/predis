struct string;
int string_set(struct string **data_loc, char *str, const long length);
int string_get(struct string **data_loc, char **str, long *length);
int string_exchange(struct string **data_loc, char **old_cts, long *old_length, char *new_cts, long new_length);
