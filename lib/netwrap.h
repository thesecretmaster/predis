#include <stdlib.h>
#include <stdbool.h>

struct data_wrap;

struct data_wrap *dw_init(int fd, size_t allocation_size);
long dw_read_copy(struct data_wrap *dw, char *buf, long size, const char *end_char);
long dw_read_nocopy(struct data_wrap *dw, long size, const char *end_char, char **start_ptr, bool *nocopy_again);
long dw_eat(struct data_wrap *dw, long size);
