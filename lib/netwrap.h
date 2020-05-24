#include <stdlib.h>
#include <stdbool.h>

struct data_wrap;

struct data_wrap *dw_init(int fd, size_t allocation_size);
long dw_read(struct data_wrap *dw, void *buf, long size, const char *end_char, char **nocopy_start, bool *nocopy_again);
